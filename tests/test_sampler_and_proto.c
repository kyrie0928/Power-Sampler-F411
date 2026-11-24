#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "svc_calib_store.h"
#include "svc_sampler.h"
#include "svc_host_proto.h"

#define ASSERT_NEAR(actual, expected, tol)                                                     \
    do {                                                                                       \
        double _a = (double)(actual);                                                          \
        double _e = (double)(expected);                                                        \
        if (fabs(_a - _e) > (double)(tol)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: got %.6f, expected %.6f (tol %.6f)\n",                \
                    __FILE__, __LINE__, _a, _e, (double)(tol));                                \
            assert(0);                                                                         \
        }                                                                                      \
    } while (0)

/* =========================================================================
 * Mock EEPROM (256 bytes) & Mock Hardware State for Unit Tests
 * ========================================================================= */
static uint8_t            s_mock_eeprom[256];
static shunt_range_t      s_mock_hw_range   = SHUNT_RANGE_HIGH;
static shunt_range_mode_t s_mock_range_mode = SHUNT_MODE_AUTO;
static ads1220_work_mode_t s_mock_ads_mode  = ADS1220_MODE_SCAN_4CH;
static uint32_t           s_mock_uptime_ms  = 1234UL;
static uint8_t            s_mock_ext_modes[3];
static uint8_t            s_mock_ext_mask   = 0U;

static uint8_t            s_tx_capture[1024];
static size_t             s_tx_capture_len  = 0U;

static bool mock_nv_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    if ((uint16_t)addr + len > sizeof(s_mock_eeprom)) {
        return false;
    }
    memcpy(buf, &s_mock_eeprom[addr], len);
    return true;
}

static bool mock_nv_write(uint8_t addr, const uint8_t *buf, uint16_t len)
{
    if ((uint16_t)addr + len > sizeof(s_mock_eeprom)) {
        return false;
    }
    memcpy(&s_mock_eeprom[addr], buf, len);
    return true;
}

static bool mock_apply_range(shunt_range_t new_range)
{
    s_mock_hw_range = new_range;
    return true;
}

static void fill_uniform_chunk(uint16_t chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM],
                               uint16_t low,
                               uint16_t mid,
                               uint16_t high,
                               uint16_t ref)
{
    for (uint16_t i = 0U; i < SVC_ADC_CHUNK_LEN; i++) {
        chunk[i][0] = low;
        chunk[i][1] = mid;
        chunk[i][2] = high;
        chunk[i][3] = ref;
    }
}

static void test_3range_current_and_voltage_conversion(void)
{
    svc_calib_store_init(NULL);

    /* 1. Verify nominal scales:
     *    LOW (50R)   : 3.000 / 4096 / 50 / 50    * 1e6 = 0.29296875 uA/LSB
     *    MID (500mR) : 3.000 / 4096 / 50 / 0.5   * 1e6 = 29.296875 uA/LSB
     *    HIGH (5mR)  : 3.000 / 4096 / 50 / 0.005 * 1e6 = 2929.6875 uA/LSB
     */
    ASSERT_NEAR(svc_sampler_convert_current_ua(3048U, 2048U, SHUNT_RANGE_LOW),  292.96875,   0.001);
    ASSERT_NEAR(svc_sampler_convert_current_ua(3048U, 2048U, SHUNT_RANGE_MID),  29296.875,   0.05);
    ASSERT_NEAR(svc_sampler_convert_current_ua(3048U, 2048U, SHUNT_RANGE_HIGH), 2929687.5,   5.0);
    ASSERT_NEAR(svc_sampler_convert_current_ua(1848U, 2048U, SHUNT_RANGE_LOW),  -58.59375,   0.001);

    /* 2. Realistic two-point calibration on LOW_CUR:
     *    Suppose @ true 0 uA hardware has +4 LSB offset (cur_adc = 2052, ref_adc = 2048),
     *    and @ true 300.0 uA hardware reads cur_adc = 3052.
     */
    double zero_bias_ua = (double)svc_sampler_convert_current_ua(2052U, 2048U, SHUNT_RANGE_LOW);
    assert(svc_sampler_tare_current_zero(SHUNT_RANGE_LOW, zero_bias_ua));
    ASSERT_NEAR(svc_sampler_convert_current_ua(2052U, 2048U, SHUNT_RANGE_LOW), 0.0, 0.001);

    double meas_at_span = (double)svc_sampler_convert_current_ua(3052U, 2048U, SHUNT_RANGE_LOW);
    assert(svc_sampler_calibrate_current_span(SHUNT_RANGE_LOW, meas_at_span, 300.0));
    /* Both span point AND 0 A zero point must remain exact after span calibration! */
    ASSERT_NEAR(svc_sampler_convert_current_ua(3052U, 2048U, SHUNT_RANGE_LOW), 300.0, 0.002);
    ASSERT_NEAR(svc_sampler_convert_current_ua(2052U, 2048U, SHUNT_RANGE_LOW), 0.0,   0.001);

    /* Reject wildly invalid span (>30% error) */
    assert(!svc_sampler_calibrate_current_span(SHUNT_RANGE_MID, 10000.0, 20000.0));

    /* 3. ADS1220 24-bit voltage conversion across all 4 channels (47k/2.2k = 22.363636x) */
    /* Half positive full-scale = 4194304 -> ADC pin = 1.024 V -> Bus = 22.899999 V = 22900364 uV */
    int32_t v_half_fs_uv = svc_sampler_convert_voltage_uv(4194304L, ADS1220_CH_MAIN_SENS4);
    ASSERT_NEAR(v_half_fs_uv, 22900364.0, 2.0);

    /* Small negative zero-drift code (-100 LSB) must preserve negative sign */
    int32_t v_neg_uv = svc_sampler_convert_voltage_uv(-100L, ADS1220_CH_VIN2);
    assert(v_neg_uv < -500 && v_neg_uv > -600);

    /* Calibrate CH1 (V_IN1) to exact 5000.000 mV */
    int32_t code_5v = (int32_t)llround((5000000.0 / BOARD_VDIV_GAIN_NOMINAL / BOARD_ADS1220_VREF_UV) *
                                       BOARD_ADS1220_FULL_SCALE_CODE);
    double uncal_mv = (double)svc_sampler_convert_voltage_uv(code_5v, ADS1220_CH_VIN1) / 1000.0;
    assert(svc_sampler_calibrate_voltage_span(ADS1220_CH_VIN1, uncal_mv * 0.992, 5000.0));
    double cal_mv = (double)svc_sampler_convert_voltage_uv(code_5v, ADS1220_CH_VIN1) / 1000.0;
    ASSERT_NEAR(cal_mv, 5000.0 / 0.992, 0.05);

    printf("[PASS] test_3range_current_and_voltage_conversion\n");
}

static void test_autorange_and_45ms_gate_settle(void)
{
    uint16_t chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM];
    uint64_t ts_us = 0ULL;

    svc_calib_store_init(NULL);
    s_mock_hw_range = SHUNT_RANGE_HIGH;
    svc_sampler_init(mock_apply_range);

    /* Inject ~100 uA:
     *   LOW (50R)  delta = +341 LSB -> 2389
     *   MID (0.5R) delta = +3 LSB   -> 2051 (<= 15 LSB down-threshold)
     *   HIGH(5mR)  delta = 0 LSB    -> 2048 (<= 15 LSB down-threshold)
     */
    fill_uniform_chunk(chunk, 2389U, 2051U, 2048U, 2048U);

    /* Chunk 1 (10 samples): still HIGH_CUR */
    ts_us += 1000ULL;
    svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    assert(s_mock_hw_range == SHUNT_RANGE_HIGH);

    /* Chunk 2 (reaches 20 samples): steps down HIGH -> MID! */
    ts_us += 1000ULL;
    svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    assert(s_mock_hw_range == SHUNT_RANGE_MID);

    /* Verify 45 ms down-range blanking (C21/C22=100nF * R19/R20=100k turn-off bleed):
     * Even if huge transient spikes appear on ADC during the 45 chunks, they are blanked!
     */
    uint16_t spike_chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM];
    fill_uniform_chunk(spike_chunk, 4095U, 4095U, 4095U, 2048U);
    for (uint32_t c = 0U; c < 45U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(spike_chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
        svc_sampler_snapshot_t snap;
        svc_sampler_get_snapshot(&snap);
        assert((snap.status_flags & SVC_STATUS_RANGE_SETTLING) != 0U);
        assert(s_mock_hw_range == SHUNT_RANGE_MID);
    }

    /* Next 10 chunks = post-settle cooldown, then 2 chunks = 20 samples -> MID -> LOW */
    for (uint32_t c = 0U; c < 12U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    }
    assert(s_mock_hw_range == SHUNT_RANGE_LOW);

    /* Let LOW_CUR settle (45 chunks) + cooldown (10 chunks) */
    for (uint32_t c = 0U; c < 55U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    }

    /* Now step load to 200 mA: LOW & MID saturate at 4095 -> 1 chunk steps LOW -> MID */
    uint16_t high_load_chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM];
    fill_uniform_chunk(high_load_chunk, 4095U, 4095U, 2116U, 2048U);
    ts_us += 1000ULL;
    svc_sampler_feed_adc_chunk(high_load_chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    assert(s_mock_hw_range == SHUNT_RANGE_MID);

    /* Up-range blanking is only 5 chunks (5 ms) + 10 cooldown + 1 chunk -> MID -> HIGH */
    for (uint32_t c = 0U; c < 16U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(high_load_chunk, ts_us, SHUNT_MODE_AUTO, s_mock_hw_range);
    }
    assert(s_mock_hw_range == SHUNT_RANGE_HIGH);

    printf("[PASS] test_autorange_and_45ms_gate_settle\n");
}

static void test_welford_precision_stats(void)
{
    uint16_t chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM];
    uint64_t ts_us = 0ULL;

    svc_calib_store_init(NULL);
    s_mock_hw_range = SHUNT_RANGE_LOW;
    svc_sampler_init(mock_apply_range);
    svc_sampler_notify_manual_range_change(SHUNT_RANGE_LOW);

    /* Drain 45 ms down-range blanking */
    fill_uniform_chunk(chunk, 2389U, 2048U, 2048U, 2048U);
    for (uint32_t c = 0U; c < 45U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_LOCK_LOW, SHUNT_RANGE_LOW);
    }

    /* Alternate +340 LSB and +342 LSB around 2048 (mean delta = 341.0 LSB, +/-1 LSB dither) */
    for (uint16_t i = 0U; i < SVC_ADC_CHUNK_LEN; i++) {
        chunk[i][0] = (i % 2U == 0U) ? (2048U + 340U) : (2048U + 342U);
        chunk[i][1] = 2048U;
        chunk[i][2] = 2048U;
        chunk[i][3] = 2048U;
    }

    assert(svc_sampler_start_precision_eval(100UL, SHUNT_RANGE_LOW));

    for (uint32_t c = 0U; c < 100U; c++) {
        ts_us += 1000ULL;
        svc_sampler_feed_adc_chunk(chunk, ts_us, SHUNT_MODE_LOCK_LOW, SHUNT_RANGE_LOW);
        if ((c % 10U) == 0U) {
            ads1220_sample_t vs = {
                .channel      = ADS1220_CH_MAIN_SENS4,
                .raw_code24   = 915772L, /* 5000.00 mV @ 47k/2.2k & 2.048V ref */
                .timestamp_us = ts_us
            };
            svc_sampler_feed_ads1220_sample(&vs);
        }
    }

    svc_precision_report_t rep;
    svc_sampler_get_precision_report(&rep);
    assert(rep.completed && !rep.active);
    assert(!rep.range_changed_during_run);
    assert(rep.cur_sample_count == 1000UL);
    ASSERT_NEAR(rep.raw_cur_mean_lsb, 2389.0, 0.001);
    ASSERT_NEAR(rep.raw_ref_mean_lsb, 2048.0, 0.001);
    ASSERT_NEAR(rep.cur_mean_ua, 341.0 * 0.29296875, 0.002);
    /* Population of +/- 0.29296875 uA around mean (N=1000): sample stddev = 0.29296875 * sqrt(1000/999) */
    ASSERT_NEAR(rep.cur_stddev_ua, 0.29296875 * sqrt(1000.0 / 999.0), 0.002);
    assert(rep.vol_sample_count[0] == 10UL);
    ASSERT_NEAR(rep.vol_mean_mv[0], 5000.0, 0.1);

    printf("[PASS] test_welford_precision_stats\n");
}

static void test_eeprom_dual_copy_recovery(void)
{
    static const svc_calib_nv_ops_t nv_ops = {
        .read  = mock_nv_read,
        .write = mock_nv_write
    };

    memset(s_mock_eeprom, 0xFF, sizeof(s_mock_eeprom));
    svc_calib_store_init(&nv_ops);
    assert(!svc_calib_store_load());

    assert(svc_calib_set_current_range(0U, 1.0125f, -0.85f));
    assert(svc_calib_set_current_range(1U, 0.9940f, 12.5f));
    assert(svc_calib_set_current_range(2U, 1.0030f, -150.0f));
    assert(svc_calib_set_voltage_ch(0U, 1.0015f, -1.2f));
    assert(svc_calib_store_save());

    /* Corrupt primary copy at 0x00 and verify seamless fallback + self-heal from 0x80 backup */
    s_mock_eeprom[4] ^= 0x5AU;
    svc_calib_store_init(&nv_ops);
    assert(svc_calib_store_load());
    assert(svc_calib_is_loaded_from_nv());

    svc_calib_data_t loaded;
    svc_calib_get(&loaded);
    ASSERT_NEAR(loaded.cur_gain[0],      1.0125, 1e-5);
    ASSERT_NEAR(loaded.cur_offset_ua[0], -0.85,  1e-4);
    ASSERT_NEAR(loaded.cur_gain[1],      0.9940, 1e-5);
    ASSERT_NEAR(loaded.vol_gain[0],      1.0015, 1e-5);

    /* Primary should have been repaired automatically */
    assert(memcmp(&s_mock_eeprom[0x00], &s_mock_eeprom[0x80], 68U) == 0);

    /* Corrupt both copies -> safe factory defaults */
    s_mock_eeprom[0x00] = 0x00U;
    s_mock_eeprom[0x80] = 0x00U;
    assert(!svc_calib_store_load());
    svc_calib_get(&loaded);
    ASSERT_NEAR(loaded.cur_gain[0], 1.0, 1e-6);

    printf("[PASS] test_eeprom_dual_copy_recovery\n");
}

static uint32_t mock_get_uptime(void) { return s_mock_uptime_ms; }
static bool mock_proto_set_range(uint8_t m)
{
    if (m > 3U) {
        return false;
    }
    s_mock_range_mode = (shunt_range_mode_t)m;
    if (m != 0U) {
        s_mock_hw_range = (shunt_range_t)m;
    }
    return true;
}
static bool mock_proto_set_ads(uint8_t m)
{
    if (m > 2U) {
        return false;
    }
    s_mock_ads_mode = (ads1220_work_mode_t)m;
    return true;
}
static bool mock_ext_cfg(uint8_t id, uint8_t mode, bool hi)
{
    if (id >= 3U || mode > 4U) {
        return false;
    }
    s_mock_ext_modes[id] = mode;
    if (hi) {
        s_mock_ext_mask |= (uint8_t)(1U << id);
    } else {
        s_mock_ext_mask &= (uint8_t)(~(1U << id));
    }
    return true;
}
static bool mock_ext_write(uint8_t id, bool hi)
{
    if (id >= 3U || s_mock_ext_modes[id] < 3U) {
        return false;
    }
    if (hi) {
        s_mock_ext_mask |= (uint8_t)(1U << id);
    } else {
        s_mock_ext_mask &= (uint8_t)(~(1U << id));
    }
    return true;
}
static uint8_t mock_ext_read_mask(void) { return s_mock_ext_mask; }
static uint8_t mock_ext_get_mode(uint8_t id) { return (id < 3U) ? s_mock_ext_modes[id] : 0U; }
static size_t mock_uart_send(const uint8_t *data, size_t len)
{
    assert(s_tx_capture_len + len <= sizeof(s_tx_capture));
    memcpy(&s_tx_capture[s_tx_capture_len], data, len);
    s_tx_capture_len += len;
    return len;
}

static void test_host_uart_protocol_for_esp32(void)
{
    static const svc_calib_nv_ops_t nv_ops = {
        .read  = mock_nv_read,
        .write = mock_nv_write
    };
    static const svc_host_proto_ops_t proto_ops = {
        .get_uptime_ms    = mock_get_uptime,
        .get_snapshot     = svc_sampler_get_snapshot,
        .set_range_mode   = mock_proto_set_range,
        .set_ads_mode     = mock_proto_set_ads,
        .clear_energy     = svc_sampler_clear_energy,
        .ext_io_cfg       = mock_ext_cfg,
        .ext_io_write     = mock_ext_write,
        .ext_io_read_mask = mock_ext_read_mask,
        .ext_io_get_mode  = mock_ext_get_mode,
        .uart_send        = mock_uart_send
    };

    memset(s_mock_eeprom, 0xFF, sizeof(s_mock_eeprom));
    memset(s_mock_ext_modes, 0, sizeof(s_mock_ext_modes));
    s_mock_ext_mask = 0U;

    svc_calib_store_init(&nv_ops);
    s_mock_hw_range = SHUNT_RANGE_LOW;
    svc_sampler_init(mock_apply_range);
    svc_sampler_notify_manual_range_change(SHUNT_RANGE_LOW);
    svc_sampler_set_hw_flags(true, true, false);

    /* Feed 45 settle chunks + 200 measurement chunks so snapshot has known values */
    uint16_t chunk[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM];
    fill_uniform_chunk(chunk, 3048U, 2048U, 2048U, 2048U); /* +1000 LSB = 292.97 uA */
    ads1220_sample_t vs0 = { .channel = ADS1220_CH_MAIN_SENS4, .raw_code24 = 915772L, .timestamp_us = 1000ULL };
    ads1220_sample_t vs1 = { .channel = ADS1220_CH_VIN1,       .raw_code24 = 604410L, .timestamp_us = 2000ULL };
    svc_sampler_feed_ads1220_sample(&vs0);
    svc_sampler_feed_ads1220_sample(&vs1);

    for (uint32_t c = 0U; c < 245U; c++) {
        svc_sampler_feed_adc_chunk(chunk, (uint64_t)(c + 1U) * 1000ULL, SHUNT_MODE_LOCK_LOW, SHUNT_RANGE_LOW);
    }

    svc_host_proto_init(&proto_ops);

    /* 1. Feed garbage + corrupted CRC frame + valid PING + sticky GET_SNAPSHOT byte-by-byte */
    uint8_t ping_frame[32];
    uint8_t snap_frame[32];
    size_t ping_len = svc_host_proto_encode_frame(PROTO_CMD_PING, 0x0042U, NULL, 0U, ping_frame, sizeof(ping_frame));
    size_t snap_len = svc_host_proto_encode_frame(PROTO_CMD_GET_SNAPSHOT, 0x0043U, NULL, 0U, snap_frame, sizeof(snap_frame));
    assert(ping_len == 10U && snap_len == 10U);

    uint8_t bad_frame[10];
    memcpy(bad_frame, ping_frame, sizeof(bad_frame));
    bad_frame[9] ^= 0xFFU; /* Break CRC */

    const uint8_t noise[] = { 0x00U, 0xAAU, 0x11U, 0xFFU };
    s_tx_capture_len = 0U;
    svc_host_proto_feed_rx(noise, sizeof(noise));
    svc_host_proto_feed_rx(bad_frame, sizeof(bad_frame));
    assert(svc_host_proto_get_crc_error_count() == 1UL);
    assert(s_tx_capture_len == 0U);

    /* Sticky back-to-back PING + GET_SNAPSHOT fed 3 bytes at a time */
    uint8_t combined[20];
    memcpy(combined, ping_frame, 10U);
    memcpy(&combined[10], snap_frame, 10U);
    for (size_t i = 0U; i < sizeof(combined); i += 3U) {
        size_t n = (sizeof(combined) - i < 3U) ? (sizeof(combined) - i) : 3U;
        svc_host_proto_feed_rx(&combined[i], n);
    }

    /* Expect PING response (10 + 9 = 19 bytes) + SNAPSHOT response (10 + 49 = 59 bytes) = 78 bytes */
    assert(s_tx_capture_len == 78U);
    assert(s_tx_capture[3] == (uint8_t)(PROTO_CMD_PING | PROTO_RESPONSE_FLAG));
    assert(s_tx_capture[8] == PROTO_STATUS_OK);

    const uint8_t *snap_resp = &s_tx_capture[19];
    assert(snap_resp[3] == (uint8_t)(PROTO_CMD_GET_SNAPSHOT | PROTO_RESPONSE_FLAG));
    assert(snap_resp[8] == PROTO_STATUS_OK);

    proto_snapshot_payload_t parsed;
    memcpy(&parsed, &snap_resp[9], sizeof(parsed));
    assert(parsed.uptime_ms == 1234UL);
    assert(parsed.active_range == (uint8_t)SHUNT_RANGE_LOW);
    ASSERT_NEAR((double)parsed.current_10na / 100.0, 292.97, 0.05);
    ASSERT_NEAR((double)parsed.v_main_uv / 1000.0,   5000.0, 0.5);
    ASSERT_NEAR((double)parsed.v_ext_uv[0] / 1000.0, 3300.0, 0.5);

    /* 2. Test H1 EXT_IO configuration & output write via protocol */
    s_tx_capture_len = 0U;
    uint8_t io_cfg_pl[3] = { 1U /* PA11 */, 3U /* OUTPUT_PP */, 1U /* HIGH */ };
    uint8_t req_buf[32];
    size_t req_len = svc_host_proto_encode_frame(PROTO_CMD_EXT_IO_CFG, 10U, io_cfg_pl, 3U, req_buf, sizeof(req_buf));
    svc_host_proto_feed_rx(req_buf, req_len);
    assert(s_tx_capture_len == 11U && s_tx_capture[8] == PROTO_STATUS_OK);
    assert((s_mock_ext_mask & (1U << 1)) != 0U);

    /* 3. Test periodic stream event for ESP32 LVGL UI */
    s_tx_capture_len = 0U;
    uint8_t stream_pl[2] = { 100U, 0U }; /* 100 ms */
    req_len = svc_host_proto_encode_frame(PROTO_CMD_SET_STREAM, 11U, stream_pl, 2U, req_buf, sizeof(req_buf));
    svc_host_proto_feed_rx(req_buf, req_len);
    assert(svc_host_proto_get_stream_period_ms() == 100U);

    s_tx_capture_len = 0U;
    svc_host_proto_poll_stream(s_mock_uptime_ms + 50UL);
    assert(s_tx_capture_len == 0U);
    svc_host_proto_poll_stream(s_mock_uptime_ms + 100UL);
    assert(s_tx_capture_len == (PROTO_FRAME_OVERHEAD + sizeof(proto_snapshot_payload_t)));
    assert(s_tx_capture[3] == PROTO_EVT_STREAM_SNAP);

    printf("[PASS] test_host_uart_protocol_for_esp32\n");
}

int main(void)
{
    test_3range_current_and_voltage_conversion();
    test_autorange_and_45ms_gate_settle();
    test_welford_precision_stats();
    test_eeprom_dual_copy_recovery();
    test_host_uart_protocol_for_esp32();

    printf("\nAll Power-Sampler-F411 unit tests PASSED!\n");
    return 0;
}
