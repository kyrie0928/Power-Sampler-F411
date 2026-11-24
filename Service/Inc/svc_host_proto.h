#ifndef SVC_HOST_PROTO_H
#define SVC_HOST_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "svc_sampler.h"
#include "svc_calib_store.h"

/* =========================================================================
 * H4 (USART1) Binary Protocol for ESP32 (lvgl-demo) Master
 *
 * Frame Format (Little-Endian, CRC16-CCITT over [ver .. payload_end]):
 *   [0]      0xAA
 *   [1]      0x55
 *   [2]      version (0x01)
 *   [3]      cmd (Response has bit7=1, Stream event is 0x60)
 *   [4..5]   seq (uint16_t)
 *   [6..7]   payload_len (uint16_t)
 *   [8..N-3] payload
 *   [N-2..N-1] crc16_le (CRC16-CCITT 0x1021, init 0xFFFF)
 * ========================================================================= */
#define PROTO_SOF_0                 0xAAU
#define PROTO_SOF_1                 0x55U
#define PROTO_VERSION               0x01U
#define PROTO_RESPONSE_FLAG         0x80U
#define PROTO_MAX_PAYLOAD           80U
#define PROTO_FRAME_OVERHEAD        10U
#define PROTO_MAX_FRAME_LEN         (PROTO_FRAME_OVERHEAD + PROTO_MAX_PAYLOAD)

typedef enum {
    PROTO_CMD_PING          = 0x01,
    PROTO_CMD_GET_SNAPSHOT  = 0x02,
    PROTO_CMD_SET_RANGE     = 0x03,
    PROTO_CMD_SET_STREAM    = 0x04,
    PROTO_CMD_SET_ADS_MODE  = 0x05,
    PROTO_CMD_CLEAR_ENERGY  = 0x06,
    PROTO_CMD_CAL_GET       = 0x10,
    PROTO_CMD_CAL_SET_CUR   = 0x11,
    PROTO_CMD_CAL_SET_VOL   = 0x12,
    PROTO_CMD_CAL_RESET     = 0x13,
    PROTO_CMD_EXT_IO_CFG    = 0x20,
    PROTO_CMD_EXT_IO_WRITE  = 0x21,
    PROTO_CMD_EXT_IO_READ   = 0x22,
    PROTO_EVT_STREAM_SNAP   = 0x60
} proto_cmd_id_t;

typedef enum {
    PROTO_STATUS_OK             = 0x00,
    PROTO_STATUS_BAD_VERSION    = 0x01,
    PROTO_STATUS_UNKNOWN_CMD    = 0x02,
    PROTO_STATUS_BAD_LENGTH     = 0x03,
    PROTO_STATUS_BAD_PARAM      = 0x04,
    PROTO_STATUS_NV_ERROR       = 0x05,
    PROTO_STATUS_HW_ERROR       = 0x06
} proto_status_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t uptime_ms;
    uint8_t  status_flags;
    uint8_t  range_mode;
    uint8_t  active_range;
    uint8_t  ext_io_mask;
    int64_t  current_10na;    /* 0.01 uA integer units (e.g. 5205 = 52.05 uA) */
    int32_t  v_main_uv;       /* SENS4 main input voltage in 1 uV */
    int32_t  v_ext_uv[3];     /* H1 V_IN1..3 voltages in 1 uV */
    int64_t  power_nw;        /* Power in 1 nW */
    int64_t  energy_uj;       /* Accumulated energy in 1 uJ */
} proto_snapshot_payload_t;   /* Total: 48 bytes */
#pragma pack(pop)

typedef struct {
    uint32_t (*get_uptime_ms)(void);
    void     (*get_snapshot)(svc_sampler_snapshot_t *out_snap);
    bool     (*set_range_mode)(uint8_t mode);
    bool     (*set_ads_mode)(uint8_t ads_mode);
    void     (*clear_energy)(void);
    bool     (*ext_io_cfg)(uint8_t id, uint8_t mode, bool init_high);
    bool     (*ext_io_write)(uint8_t id, bool high);
    uint8_t  (*ext_io_read_mask)(void);
    uint8_t  (*ext_io_get_mode)(uint8_t id);
    size_t   (*uart_send)(const uint8_t *data, size_t len);
} svc_host_proto_ops_t;

void svc_host_proto_init(const svc_host_proto_ops_t *ops);
void svc_host_proto_feed_rx(const uint8_t *data, size_t len);
void svc_host_proto_poll_stream(uint32_t now_ms);

uint16_t svc_host_proto_get_stream_period_ms(void);
uint32_t svc_host_proto_get_crc_error_count(void);

/* Helper exposed for unit tests and ESP32 peer reference */
size_t svc_host_proto_encode_frame(uint8_t cmd,
                                   uint16_t seq,
                                   const uint8_t *payload,
                                   uint16_t payload_len,
                                   uint8_t *out_frame,
                                   size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* SVC_HOST_PROTO_H */
