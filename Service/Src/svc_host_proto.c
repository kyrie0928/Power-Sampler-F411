#include "svc_host_proto.h"
#include <string.h>

#define RX_PARSE_BUF_SIZE (PROTO_MAX_FRAME_LEN * 2U)

/* Compile-time check that snapshot payload is 48 bytes packed */
typedef char proto_snap_size_check[(sizeof(proto_snapshot_payload_t) == 48U) ? 1 : -1];

static svc_host_proto_ops_t s_ops;
static uint8_t              s_rx_buf[RX_PARSE_BUF_SIZE];
static uint16_t             s_rx_len         = 0U;
static uint16_t             s_stream_ms      = 0U;
static uint32_t             s_last_stream_ms = 0UL;
static uint16_t             s_stream_seq     = 0U;
static uint32_t             s_crc_err_cnt    = 0UL;

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
}

static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static float read_f32_le(const uint8_t *p)
{
    uint32_t bits = (uint32_t)p[0]
                  | ((uint32_t)p[1] << 8U)
                  | ((uint32_t)p[2] << 16U)
                  | ((uint32_t)p[3] << 24U);
    float val = 0.0f;
    memcpy(&val, &bits, sizeof(val));
    return val;
}

static void write_f32_le(uint8_t *p, float val)
{
    uint32_t bits = 0UL;
    memcpy(&bits, &val, sizeof(bits));
    write_u32_le(p, bits);
}

size_t svc_host_proto_encode_frame(uint8_t cmd,
                                   uint16_t seq,
                                   const uint8_t *payload,
                                   uint16_t payload_len,
                                   uint8_t *out_frame,
                                   size_t out_capacity)
{
    size_t total_len = (size_t)PROTO_FRAME_OVERHEAD + payload_len;
    if (out_frame == NULL || payload_len > PROTO_MAX_PAYLOAD ||
        (payload_len > 0U && payload == NULL) || out_capacity < total_len) {
        return 0U;
    }

    out_frame[0] = PROTO_SOF_0;
    out_frame[1] = PROTO_SOF_1;
    out_frame[2] = PROTO_VERSION;
    out_frame[3] = cmd;
    write_u16_le(&out_frame[4], seq);
    write_u16_le(&out_frame[6], payload_len);
    if (payload_len > 0U) {
        memcpy(&out_frame[8], payload, payload_len);
    }

    uint16_t crc = svc_crc16_ccitt(&out_frame[2], (uint16_t)(6U + payload_len));
    write_u16_le(&out_frame[8U + payload_len], crc);
    return total_len;
}

static void build_snapshot_payload(proto_snapshot_payload_t *out_p)
{
    svc_sampler_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    if (s_ops.get_snapshot != NULL) {
        s_ops.get_snapshot(&snap);
    }

    out_p->uptime_ms     = (s_ops.get_uptime_ms != NULL) ? s_ops.get_uptime_ms() : 0UL;
    out_p->status_flags  = snap.status_flags;
    out_p->range_mode    = (uint8_t)snap.range_mode;
    out_p->active_range  = (uint8_t)snap.active_range;
    out_p->ext_io_mask   = (s_ops.ext_io_read_mask != NULL) ? s_ops.ext_io_read_mask() : 0U;
    out_p->current_10na  = snap.current_10na;
    out_p->v_main_uv     = snap.voltage_uv[0];
    out_p->v_ext_uv[0]   = snap.voltage_uv[1];
    out_p->v_ext_uv[1]   = snap.voltage_uv[2];
    out_p->v_ext_uv[2]   = snap.voltage_uv[3];
    out_p->power_nw      = snap.power_nw;
    out_p->energy_uj     = snap.energy_uj;
}

static void send_response(uint8_t req_cmd,
                          uint16_t seq,
                          proto_status_t status,
                          const uint8_t *body,
                          uint16_t body_len)
{
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint8_t tx_frame[PROTO_MAX_FRAME_LEN];

    if ((uint16_t)(body_len + 1U) > PROTO_MAX_PAYLOAD || s_ops.uart_send == NULL) {
        return;
    }

    payload[0] = (uint8_t)status;
    if (body_len > 0U && body != NULL) {
        memcpy(&payload[1], body, body_len);
    }

    size_t frame_len = svc_host_proto_encode_frame((uint8_t)(req_cmd | PROTO_RESPONSE_FLAG),
                                                   seq,
                                                   payload,
                                                   (uint16_t)(body_len + 1U),
                                                   tx_frame,
                                                   sizeof(tx_frame));
    if (frame_len > 0U) {
        (void)s_ops.uart_send(tx_frame, frame_len);
    }
}

static void handle_frame(uint8_t ver, uint8_t cmd, uint16_t seq, const uint8_t *pl, uint16_t len)
{
    uint8_t resp[PROTO_MAX_PAYLOAD - 1U];
    uint16_t resp_len = 0U;
    proto_status_t st = PROTO_STATUS_OK;

    if (ver != PROTO_VERSION) {
        send_response(cmd, seq, PROTO_STATUS_BAD_VERSION, NULL, 0U);
        return;
    }

    switch ((proto_cmd_id_t)cmd) {
        case PROTO_CMD_PING: {
            if (len != 0U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            svc_sampler_snapshot_t snap;
            memset(&snap, 0, sizeof(snap));
            if (s_ops.get_snapshot != NULL) {
                s_ops.get_snapshot(&snap);
            }
            uint32_t up = (s_ops.get_uptime_ms != NULL) ? s_ops.get_uptime_ms() : 0UL;
            resp[0] = 1U; /* FW major */
            resp[1] = 0U; /* FW minor */
            resp[2] = 0U; /* FW patch */
            resp[3] = snap.status_flags;
            write_u32_le(&resp[4], up);
            resp_len = 8U;
            break;
        }

        case PROTO_CMD_GET_SNAPSHOT: {
            if (len != 0U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            proto_snapshot_payload_t snap_pl;
            build_snapshot_payload(&snap_pl);
            memcpy(resp, &snap_pl, sizeof(snap_pl));
            resp_len = sizeof(snap_pl);
            break;
        }

        case PROTO_CMD_SET_RANGE: {
            if (len != 1U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (s_ops.set_range_mode == NULL || !s_ops.set_range_mode(pl[0])) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            resp[0] = pl[0];
            resp_len = 1U;
            break;
        }

        case PROTO_CMD_SET_STREAM: {
            if (len != 2U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            uint16_t period = read_u16_le(pl);
            if (period != 0U && (period < 10U || period > 10000U)) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            s_stream_ms      = period;
            s_last_stream_ms = (s_ops.get_uptime_ms != NULL) ? s_ops.get_uptime_ms() : 0UL;
            write_u16_le(resp, s_stream_ms);
            resp_len = 2U;
            break;
        }

        case PROTO_CMD_SET_ADS_MODE: {
            if (len != 1U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (s_ops.set_ads_mode == NULL || !s_ops.set_ads_mode(pl[0])) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            resp[0] = pl[0];
            resp_len = 1U;
            break;
        }

        case PROTO_CMD_CLEAR_ENERGY: {
            if (len != 0U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (s_ops.clear_energy != NULL) {
                s_ops.clear_energy();
            }
            break;
        }

        case PROTO_CMD_CAL_GET: {
            if (len != 0U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            svc_calib_data_t cal;
            svc_calib_get(&cal);
            uint16_t off = 0U;
            for (uint8_t i = 0U; i < SVC_CALIB_CUR_RANGE_COUNT; i++) {
                write_f32_le(&resp[off], cal.cur_gain[i]);      off = (uint16_t)(off + 4U);
                write_f32_le(&resp[off], cal.cur_offset_ua[i]); off = (uint16_t)(off + 4U);
            }
            for (uint8_t i = 0U; i < SVC_CALIB_VOL_CH_COUNT; i++) {
                write_f32_le(&resp[off], cal.vol_gain[i]);      off = (uint16_t)(off + 4U);
                write_f32_le(&resp[off], cal.vol_offset_mv[i]); off = (uint16_t)(off + 4U);
            }
            resp_len = off; /* 56 bytes */
            break;
        }

        case PROTO_CMD_CAL_SET_CUR: {
            if (len != 10U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            uint8_t range_num = pl[0]; /* 1=LOW, 2=MID, 3=HIGH */
            if (range_num < 1U || range_num > 3U) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            float gain   = read_f32_le(&pl[1]);
            float offset = read_f32_le(&pl[5]);
            bool save_nv = (pl[9] != 0U);
            if (!svc_calib_set_current_range((uint8_t)(range_num - 1U), gain, offset)) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            if (save_nv && !svc_calib_store_save()) {
                st = PROTO_STATUS_NV_ERROR;
                break;
            }
            break;
        }

        case PROTO_CMD_CAL_SET_VOL: {
            if (len != 10U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            uint8_t ch   = pl[0]; /* 0..3 */
            float gain   = read_f32_le(&pl[1]);
            float offset = read_f32_le(&pl[5]);
            bool save_nv = (pl[9] != 0U);
            if (!svc_calib_set_voltage_ch(ch, gain, offset)) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            if (save_nv && !svc_calib_store_save()) {
                st = PROTO_STATUS_NV_ERROR;
                break;
            }
            break;
        }

        case PROTO_CMD_CAL_RESET: {
            if (len != 2U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (pl[0] != 0xC3U || pl[1] != 0x3CU) {
                st = PROTO_STATUS_BAD_PARAM;
                break;
            }
            if (!svc_calib_store_reset_defaults(true)) {
                st = PROTO_STATUS_NV_ERROR;
            }
            break;
        }

        case PROTO_CMD_EXT_IO_CFG: {
            if (len != 3U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (s_ops.ext_io_cfg == NULL || !s_ops.ext_io_cfg(pl[0], pl[1], pl[2] != 0U)) {
                st = PROTO_STATUS_BAD_PARAM;
            }
            break;
        }

        case PROTO_CMD_EXT_IO_WRITE: {
            if (len != 2U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            if (s_ops.ext_io_write == NULL || !s_ops.ext_io_write(pl[0], pl[1] != 0U)) {
                st = PROTO_STATUS_BAD_PARAM;
            }
            break;
        }

        case PROTO_CMD_EXT_IO_READ: {
            if (len != 0U) {
                st = PROTO_STATUS_BAD_LENGTH;
                break;
            }
            resp[0] = (s_ops.ext_io_read_mask != NULL) ? s_ops.ext_io_read_mask() : 0U;
            for (uint8_t i = 0U; i < 3U; i++) {
                resp[1U + i] = (s_ops.ext_io_get_mode != NULL) ? s_ops.ext_io_get_mode(i) : 0U;
            }
            resp_len = 4U;
            break;
        }

        default:
            st = PROTO_STATUS_UNKNOWN_CMD;
            break;
    }

    send_response(cmd, seq, st, resp, resp_len);
}

static void drop_rx_prefix(uint16_t count)
{
    if (count >= s_rx_len) {
        s_rx_len = 0U;
        return;
    }
    s_rx_len = (uint16_t)(s_rx_len - count);
    memmove(s_rx_buf, &s_rx_buf[count], s_rx_len);
}

void svc_host_proto_init(const svc_host_proto_ops_t *ops)
{
    memset(&s_ops, 0, sizeof(s_ops));
    if (ops != NULL) {
        s_ops = *ops;
    }
    s_rx_len         = 0U;
    s_stream_ms      = 0U;
    s_last_stream_ms = 0UL;
    s_stream_seq     = 0U;
    s_crc_err_cnt    = 0UL;
}

void svc_host_proto_feed_rx(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U) {
        return;
    }

    size_t offset = 0U;
    while (offset < len) {
        if (s_rx_len == sizeof(s_rx_buf)) {
            drop_rx_prefix(1U);
        }
        size_t space = sizeof(s_rx_buf) - s_rx_len;
        size_t chunk = (len - offset < space) ? (len - offset) : space;
        memcpy(&s_rx_buf[s_rx_len], &data[offset], chunk);
        s_rx_len = (uint16_t)(s_rx_len + chunk);
        offset += chunk;

        while (s_rx_len >= 2U) {
            uint16_t sof_idx = s_rx_len;
            for (uint16_t i = 0U; (uint16_t)(i + 1U) < s_rx_len; i++) {
                if (s_rx_buf[i] == PROTO_SOF_0 && s_rx_buf[i + 1U] == PROTO_SOF_1) {
                    sof_idx = i;
                    break;
                }
            }

            if (sof_idx == s_rx_len) {
                uint16_t keep = (s_rx_buf[s_rx_len - 1U] == PROTO_SOF_0) ? 1U : 0U;
                drop_rx_prefix((uint16_t)(s_rx_len - keep));
                break;
            }
            if (sof_idx > 0U) {
                drop_rx_prefix(sof_idx);
            }
            if (s_rx_len < 8U) {
                break;
            }

            uint16_t payload_len = read_u16_le(&s_rx_buf[6]);
            if (payload_len > PROTO_MAX_PAYLOAD) {
                drop_rx_prefix(1U);
                continue;
            }

            uint16_t frame_len = (uint16_t)(PROTO_FRAME_OVERHEAD + payload_len);
            if (s_rx_len < frame_len) {
                break;
            }

            uint16_t rx_crc   = read_u16_le(&s_rx_buf[frame_len - 2U]);
            uint16_t calc_crc = svc_crc16_ccitt(&s_rx_buf[2], (uint16_t)(6U + payload_len));
            if (rx_crc != calc_crc) {
                s_crc_err_cnt++;
                drop_rx_prefix(1U);
                continue;
            }

            uint8_t  ver = s_rx_buf[2];
            uint8_t  cmd = s_rx_buf[3];
            uint16_t seq = read_u16_le(&s_rx_buf[4]);
            uint8_t  payload_copy[PROTO_MAX_PAYLOAD];
            if (payload_len > 0U) {
                memcpy(payload_copy, &s_rx_buf[8], payload_len);
            }
            drop_rx_prefix(frame_len);

            handle_frame(ver, cmd, seq, payload_copy, payload_len);
        }
    }
}

void svc_host_proto_poll_stream(uint32_t now_ms)
{
    if (s_stream_ms == 0U || s_ops.uart_send == NULL) {
        return;
    }
    if ((now_ms - s_last_stream_ms) < s_stream_ms) {
        return;
    }
    s_last_stream_ms = now_ms;

    proto_snapshot_payload_t snap_pl;
    uint8_t tx_frame[PROTO_MAX_FRAME_LEN];
    build_snapshot_payload(&snap_pl);

    size_t frame_len = svc_host_proto_encode_frame(PROTO_EVT_STREAM_SNAP,
                                                   s_stream_seq++,
                                                   (const uint8_t *)&snap_pl,
                                                   sizeof(snap_pl),
                                                   tx_frame,
                                                   sizeof(tx_frame));
    if (frame_len > 0U) {
        (void)s_ops.uart_send(tx_frame, frame_len);
    }
}

uint16_t svc_host_proto_get_stream_period_ms(void)
{
    return s_stream_ms;
}

uint32_t svc_host_proto_get_crc_error_count(void)
{
    return s_crc_err_cnt;
}
