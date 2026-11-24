#include "svc_sampler.h"
#include "svc_calib_store.h"
#include "board_pins.h"
#include <math.h>
#include <string.h>

/* Auto-range hysteresis thresholds (12-bit ADC LSB relative to 1V5_REF ~2048) */
#define AUTORANGE_UP_DELTA_LSB          1800U /* ~527 uA on LOW, ~52.7 mA on MID */
#define AUTORANGE_DOWN_DELTA_LSB        15U   /* ~439 uA on MID, ~43.9 mA on HIGH */
#define AUTORANGE_RAIL_MARGIN_LSB       64U   /* Saturating near 0 or 4095 */

#define AUTORANGE_UP_CONFIRM_SAMPLES    3U    /* 300 us fast overload step-up */
#define AUTORANGE_DOWN_CONFIRM_SAMPLES  20U   /* 2.0 ms stable underload step-down */

/* Hardware gate RC settle blanking (chunks of 1 ms):
 *   Up-range turns ON Q1/Q2 via R21/R22(10k) into C21/C22(100nF): tau ~ 1 ms -> settle 5 ms
 *   Down-range turns OFF Q1/Q2 via R19/R20(100k) across C21/C22(100nF): tau ~ 10 ms -> settle 45 ms
 */
#define SETTLE_CHUNKS_UPRANGE           5U
#define SETTLE_CHUNKS_DOWNRANGE         45U
#define COOLDOWN_CHUNKS_AFTER_SETTLE    10U

/* 200 ms smoothing window (200 chunks * 10 samples = 2000 samples @ 10 kHz) */
#define WINDOW_TARGET_CHUNKS            200U

typedef struct {
    uint32_t count;
    double   mean;
    double   m2;
    double   min_val;
    double   max_val;
} welford_acc_t;

static svc_range_apply_fn_t   s_range_apply_cb = NULL;
static svc_sampler_snapshot_t s_snap;

/* Window accumulators */
static int64_t  s_win_cur_sum_10na = 0LL;
static uint32_t s_win_cur_samples  = 0UL;
static uint16_t s_win_chunk_cnt    = 0U;

static int64_t  s_vol_ema_uv[ADS1220_CH_COUNT];
static bool     s_vol_primed[ADS1220_CH_COUNT];

/* Energy accumulator in atto-watt-seconds / nanojoules to avoid quantization loss */
static double   s_energy_uj_acc = 0.0;

/* Auto-range FSM state */
static uint16_t s_up_confirm_cnt   = 0U;
static uint16_t s_down_confirm_cnt = 0U;
static uint16_t s_settle_chunks    = 0U;
static uint16_t s_cooldown_chunks  = 0U;
static shunt_range_t s_last_range  = SHUNT_RANGE_HIGH;

/* Precision evaluation state */
static struct {
    bool          active;
    bool          completed;
    bool          range_changed;
    shunt_range_t eval_range;
    uint32_t      target_chunks;
    uint32_t      elapsed_chunks;
    welford_acc_t cur_ua;
    welford_acc_t raw_cur;
    welford_acc_t raw_ref;
    welford_acc_t vol_mv[ADS1220_CH_COUNT];
    welford_acc_t raw_ads[ADS1220_CH_COUNT];
} s_prec;

static void welford_reset(welford_acc_t *acc)
{
    acc->count   = 0UL;
    acc->mean    = 0.0;
    acc->m2      = 0.0;
    acc->min_val = 0.0;
    acc->max_val = 0.0;
}

static void welford_push(welford_acc_t *acc, double x)
{
    acc->count++;
    if (acc->count == 1UL) {
        acc->min_val = x;
        acc->max_val = x;
    } else {
        if (x < acc->min_val) {
            acc->min_val = x;
        }
        if (x > acc->max_val) {
            acc->max_val = x;
        }
    }
    double delta  = x - acc->mean;
    acc->mean    += delta / (double)acc->count;
    double delta2 = x - acc->mean;
    acc->m2      += delta * delta2;
}

static double welford_stddev(const welford_acc_t *acc)
{
    if (acc->count < 2UL) {
        return 0.0;
    }
    double var = acc->m2 / (double)(acc->count - 1UL);
    return (var > 0.0) ? sqrt(var) : 0.0;
}

static uint16_t abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint8_t range_to_cal_idx(shunt_range_t range)
{
    switch (range) {
        case SHUNT_RANGE_LOW:  return 0U;
        case SHUNT_RANGE_MID:  return 1U;
        case SHUNT_RANGE_HIGH:
        default:               return 2U;
    }
}

static float range_nominal_scale_ua(shunt_range_t range)
{
    switch (range) {
        case SHUNT_RANGE_LOW:  return SVC_NOMINAL_SCALE_LOW_UA;
        case SHUNT_RANGE_MID:  return SVC_NOMINAL_SCALE_MID_UA;
        case SHUNT_RANGE_HIGH:
        default:               return SVC_NOMINAL_SCALE_HIGH_UA;
    }
}

void svc_sampler_init(svc_range_apply_fn_t range_apply_cb)
{
    memset(&s_snap, 0, sizeof(s_snap));
    memset(&s_prec, 0, sizeof(s_prec));
    memset(s_vol_ema_uv, 0, sizeof(s_vol_ema_uv));
    memset(s_vol_primed, 0, sizeof(s_vol_primed));

    s_range_apply_cb   = range_apply_cb;
    s_snap.active_range = SHUNT_RANGE_HIGH;
    s_snap.range_mode   = SHUNT_MODE_AUTO;
    s_last_range        = SHUNT_RANGE_HIGH;
    s_win_cur_sum_10na  = 0LL;
    s_win_cur_samples   = 0UL;
    s_win_chunk_cnt     = 0U;
    s_energy_uj_acc     = 0.0;
    s_up_confirm_cnt    = 0U;
    s_down_confirm_cnt  = 0U;
    s_settle_chunks     = 0U;
    s_cooldown_chunks   = 0U;
}

void svc_sampler_set_hw_flags(bool ads_online, bool eeprom_ok, bool calib_loaded)
{
    uint8_t clear_mask = (uint8_t)(SVC_STATUS_ADS1220_ONLINE | SVC_STATUS_EEPROM_OK | SVC_STATUS_CALIB_LOADED);
    uint8_t flags = (uint8_t)(s_snap.status_flags & (uint8_t)(~clear_mask));

    if (ads_online) {
        flags |= SVC_STATUS_ADS1220_ONLINE;
    }
    if (eeprom_ok) {
        flags |= SVC_STATUS_EEPROM_OK;
    }
    if (calib_loaded) {
        flags |= SVC_STATUS_CALIB_LOADED;
    }
    s_snap.status_flags = flags;
}

float svc_sampler_convert_current_ua(uint16_t cur_adc, uint16_t ref_adc, shunt_range_t range)
{
    svc_calib_data_t cal;
    svc_calib_get(&cal);

    uint8_t idx = range_to_cal_idx(range);
    float delta_lsb = (float)((int32_t)cur_adc - (int32_t)ref_adc);
    float scale     = range_nominal_scale_ua(range);

    /* Input-referred offset model: I = (delta_lsb * scale + offset_ua) * gain.
     * Ensures span gain calibration never disturbs a previously tared 0 A zero point.
     * Note: R2+R3 (49.2k) is on J1/SENS4 upstream of R16..R18, so zero divider current
     * flows through the shunts.
     */
    return ((delta_lsb * scale) + cal.cur_offset_ua[idx]) * cal.cur_gain[idx];
}

int32_t svc_sampler_convert_voltage_uv(int32_t raw_code24, ads1220_channel_t ch)
{
    if ((uint8_t)ch >= ADS1220_CH_COUNT) {
        return 0;
    }

    svc_calib_data_t cal;
    svc_calib_get(&cal);

    double adc_uv  = ((double)raw_code24 / BOARD_ADS1220_FULL_SCALE_CODE) * BOARD_ADS1220_VREF_UV;
    double bus_uv  = ((adc_uv * BOARD_VDIV_GAIN_NOMINAL) + ((double)cal.vol_offset_mv[ch] * 1000.0))
                   * (double)cal.vol_gain[ch];

    if (bus_uv > 2140000000.0) {
        bus_uv = 2140000000.0;
    } else if (bus_uv < -2140000000.0) {
        bus_uv = -2140000000.0;
    }
    return (int32_t)llround(bus_uv);
}

static shunt_range_t evaluate_autorange_sample(shunt_range_t active_range,
                                               uint16_t raw_low,
                                               uint16_t raw_mid,
                                               uint16_t raw_high,
                                               uint16_t raw_ref)
{
    switch (active_range) {
        case SHUNT_RANGE_LOW: {
            uint16_t delta = abs_diff_u16(raw_low, raw_ref);
            bool near_rail = (raw_low <= AUTORANGE_RAIL_MARGIN_LSB) ||
                             (raw_low >= (uint16_t)(4095U - AUTORANGE_RAIL_MARGIN_LSB));
            if (delta >= AUTORANGE_UP_DELTA_LSB || near_rail) {
                s_up_confirm_cnt++;
                if (s_up_confirm_cnt >= AUTORANGE_UP_CONFIRM_SAMPLES) {
                    return SHUNT_RANGE_MID;
                }
            } else {
                s_up_confirm_cnt = 0U;
            }
            s_down_confirm_cnt = 0U;
            break;
        }

        case SHUNT_RANGE_MID: {
            uint16_t delta = abs_diff_u16(raw_mid, raw_ref);
            bool near_rail = (raw_mid <= AUTORANGE_RAIL_MARGIN_LSB) ||
                             (raw_mid >= (uint16_t)(4095U - AUTORANGE_RAIL_MARGIN_LSB));
            if (delta >= AUTORANGE_UP_DELTA_LSB || near_rail) {
                s_up_confirm_cnt++;
                if (s_up_confirm_cnt >= AUTORANGE_UP_CONFIRM_SAMPLES) {
                    return SHUNT_RANGE_HIGH;
                }
            } else {
                s_up_confirm_cnt = 0U;
            }

            if (delta <= AUTORANGE_DOWN_DELTA_LSB) {
                s_down_confirm_cnt++;
                if (s_down_confirm_cnt >= AUTORANGE_DOWN_CONFIRM_SAMPLES) {
                    return SHUNT_RANGE_LOW;
                }
            } else {
                s_down_confirm_cnt = 0U;
            }
            break;
        }

        case SHUNT_RANGE_HIGH:
        default: {
            uint16_t delta = abs_diff_u16(raw_high, raw_ref);
            if (delta <= AUTORANGE_DOWN_DELTA_LSB) {
                s_down_confirm_cnt++;
                if (s_down_confirm_cnt >= AUTORANGE_DOWN_CONFIRM_SAMPLES) {
                    return SHUNT_RANGE_MID;
                }
            } else {
                s_down_confirm_cnt = 0U;
            }
            s_up_confirm_cnt = 0U;
            break;
        }
    }

    return active_range;
}

static void reset_current_window(void)
{
    s_win_cur_sum_10na   = 0LL;
    s_win_cur_samples    = 0UL;
    s_win_chunk_cnt      = 0U;
    s_snap.status_flags &= (uint8_t)(~SVC_STATUS_WINDOW_READY);
}

void svc_sampler_notify_manual_range_change(shunt_range_t new_range)
{
    s_up_confirm_cnt   = 0U;
    s_down_confirm_cnt = 0U;

    if (new_range == s_last_range) {
        return;
    }

    bool is_downrange = ((uint8_t)new_range < (uint8_t)s_last_range);
    s_settle_chunks   = is_downrange ? SETTLE_CHUNKS_DOWNRANGE : SETTLE_CHUNKS_UPRANGE;
    s_cooldown_chunks = COOLDOWN_CHUNKS_AFTER_SETTLE;
    s_last_range      = new_range;

    /* Reset current smoothing window so old range samples don't mix with new range */
    reset_current_window();

    if (s_prec.active) {
        s_prec.range_changed = true;
    }
}

void svc_sampler_notify_calib_changed(void)
{
    reset_current_window();
    memset(s_vol_primed, 0, sizeof(s_vol_primed));
}

void svc_sampler_feed_adc_chunk(const uint16_t samples[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM],
                                uint64_t timestamp_us,
                                shunt_range_mode_t mode,
                                shunt_range_t current_hw_range)
{
    if (samples == NULL) {
        return;
    }

    s_snap.timestamp_us = timestamp_us;
    s_snap.range_mode   = mode;
    s_snap.active_range = current_hw_range;

    /* Detect asynchronous ISR emergency switch to HIGH_CUR */
    if (current_hw_range != s_last_range) {
        bool is_down = ((uint8_t)current_hw_range < (uint8_t)s_last_range);
        s_settle_chunks    = is_down ? SETTLE_CHUNKS_DOWNRANGE : SETTLE_CHUNKS_UPRANGE;
        s_cooldown_chunks  = COOLDOWN_CHUNKS_AFTER_SETTLE;
        s_up_confirm_cnt   = 0U;
        s_down_confirm_cnt = 0U;
        s_last_range       = current_hw_range;
        s_snap.range_switch_count++;
        reset_current_window();
        if (s_prec.active) {
            s_prec.range_changed = true;
        }
    }

    /* Update raw ADC diagnostic snapshot from the latest sample of this chunk */
    const uint16_t *last_row = samples[SVC_ADC_CHUNK_LEN - 1U];
    s_snap.raw_adc_low  = last_row[0];
    s_snap.raw_adc_mid  = last_row[1];
    s_snap.raw_adc_high = last_row[2];
    s_snap.raw_adc_ref  = last_row[3];

    /* Skip transient samples while high-side MOSFET gate capacitance is settling */
    if (s_settle_chunks > 0U) {
        s_settle_chunks--;
        s_snap.status_flags |= SVC_STATUS_RANGE_SETTLING;
        return;
    }
    s_snap.status_flags &= (uint8_t)(~SVC_STATUS_RANGE_SETTLING);

    shunt_range_t next_range = current_hw_range;
    double chunk_cur_sum_ua  = 0.0;
    uint16_t valid_count     = 0U;

    for (uint16_t i = 0U; i < SVC_ADC_CHUNK_LEN; i++) {
        uint16_t raw_low  = samples[i][0];
        uint16_t raw_mid  = samples[i][1];
        uint16_t raw_high = samples[i][2];
        uint16_t raw_ref  = samples[i][3];

        if (mode == SHUNT_MODE_AUTO && s_cooldown_chunks == 0U) {
            next_range = evaluate_autorange_sample(current_hw_range,
                                                   raw_low,
                                                   raw_mid,
                                                   raw_high,
                                                   raw_ref);
            if (next_range != current_hw_range) {
                break;
            }
        }

        uint16_t active_raw = raw_high;
        if (current_hw_range == SHUNT_RANGE_LOW) {
            active_raw = raw_low;
        } else if (current_hw_range == SHUNT_RANGE_MID) {
            active_raw = raw_mid;
        }

        if (active_raw <= 16U || active_raw >= 4079U) {
            s_snap.status_flags |= SVC_STATUS_OVERLOAD_WARN;
        } else if (i == (SVC_ADC_CHUNK_LEN - 1U)) {
            s_snap.status_flags &= (uint8_t)(~SVC_STATUS_OVERLOAD_WARN);
        }

        float sample_ua = svc_sampler_convert_current_ua(active_raw, raw_ref, current_hw_range);
        int64_t sample_10na = (int64_t)llroundf(sample_ua * 100.0f);

        s_win_cur_sum_10na += sample_10na;
        s_win_cur_samples++;
        chunk_cur_sum_ua += (double)sample_ua;
        valid_count++;

        if (s_prec.active) {
            welford_push(&s_prec.cur_ua, (double)sample_ua);
            welford_push(&s_prec.raw_cur, (double)active_raw);
            welford_push(&s_prec.raw_ref, (double)raw_ref);
        }
    }

    /* Integrate energy over this 1 ms chunk (100 us per valid sample):
     * P_W = (V_uV * 1e-6) * (I_uA * 1e-6), dE_uJ = P_W * dt_s * 1e6 = V_uV * I_uA * dt_s * 1e-6
     */
    if (valid_count > 0U) {
        double mean_chunk_ua = chunk_cur_sum_ua / (double)valid_count;
        double v_main_uv     = (double)s_snap.voltage_uv[ADS1220_CH_MAIN_SENS4];
        double dt_sec        = (double)valid_count * (1.0 / 10000.0);
        s_energy_uj_acc     += (v_main_uv * mean_chunk_ua * dt_sec) * 1.0e-6;
        s_snap.energy_uj     = (int64_t)llround(s_energy_uj_acc);
    }

    /* Apply auto-range transition if triggered */
    if (mode == SHUNT_MODE_AUTO && next_range != current_hw_range) {
        if (s_range_apply_cb != NULL) {
            (void)s_range_apply_cb(next_range);
        }
        bool is_down       = ((uint8_t)next_range < (uint8_t)current_hw_range);
        s_settle_chunks    = is_down ? SETTLE_CHUNKS_DOWNRANGE : SETTLE_CHUNKS_UPRANGE;
        s_cooldown_chunks  = COOLDOWN_CHUNKS_AFTER_SETTLE;
        s_up_confirm_cnt   = 0U;
        s_down_confirm_cnt = 0U;
        s_last_range       = next_range;
        s_snap.active_range = next_range;
        s_snap.range_switch_count++;
        reset_current_window();
        if (s_prec.active) {
            s_prec.range_changed = true;
        }
        return;
    }

    if (s_prec.active) {
        s_prec.elapsed_chunks++;
        if (s_prec.elapsed_chunks >= s_prec.target_chunks) {
            s_prec.active    = false;
            s_prec.completed = true;
        }
    }

    if (s_cooldown_chunks > 0U) {
        s_cooldown_chunks--;
    }

    s_win_chunk_cnt++;
    if (s_win_chunk_cnt >= WINDOW_TARGET_CHUNKS && s_win_cur_samples > 0UL) {
        int64_t avg_10na = s_win_cur_sum_10na / (int64_t)s_win_cur_samples;
        s_snap.current_10na = avg_10na;
        s_snap.current_ua   = (float)avg_10na / 100.0f;

        /* P(nW) = V(uV) * I(uA) * 1e-3 = V(uV) * I(10nA) * 1e-5 */
        double p_nw = ((double)s_snap.voltage_uv[ADS1220_CH_MAIN_SENS4] * (double)avg_10na) * 1.0e-5;
        s_snap.power_nw = (int64_t)llround(p_nw);
        s_snap.power_mw = (float)(p_nw * 1.0e-6);
        s_snap.status_flags |= SVC_STATUS_WINDOW_READY;

        s_win_cur_sum_10na = 0LL;
        s_win_cur_samples  = 0UL;
        s_win_chunk_cnt    = 0U;
    }
}

void svc_sampler_feed_ads1220_sample(const ads1220_sample_t *sample)
{
    if (sample == NULL || (uint8_t)sample->channel >= ADS1220_CH_COUNT) {
        return;
    }

    uint8_t ch = (uint8_t)sample->channel;
    int32_t uv = svc_sampler_convert_voltage_uv(sample->raw_code24, sample->channel);

    s_snap.raw_ads24[ch] = sample->raw_code24;

    /* Light IIR smoothing (alpha = 1/4) for display stability without sluggish response */
    if (!s_vol_primed[ch]) {
        s_vol_ema_uv[ch] = uv;
        s_vol_primed[ch] = true;
    } else {
        s_vol_ema_uv[ch] = (s_vol_ema_uv[ch] * 3LL + (int64_t)uv) / 4LL;
    }

    s_snap.voltage_uv[ch] = (int32_t)s_vol_ema_uv[ch];
    s_snap.voltage_mv[ch] = (float)s_snap.voltage_uv[ch] / 1000.0f;

    if (ch == (uint8_t)ADS1220_CH_MAIN_SENS4) {
        double p_nw = ((double)s_snap.voltage_uv[0] * (double)s_snap.current_10na) * 1.0e-5;
        s_snap.power_nw = (int64_t)llround(p_nw);
        s_snap.power_mw = (float)(p_nw * 1.0e-6);
    }

    if (s_prec.active) {
        welford_push(&s_prec.vol_mv[ch], (double)uv / 1000.0);
        welford_push(&s_prec.raw_ads[ch], (double)sample->raw_code24);
    }
}

void svc_sampler_get_snapshot(svc_sampler_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }
    *out_snapshot = s_snap;
}

void svc_sampler_clear_energy(void)
{
    s_energy_uj_acc  = 0.0;
    s_snap.energy_uj = 0LL;
}

bool svc_sampler_start_precision_eval(uint32_t duration_ms, shunt_range_t current_range)
{
    if (duration_ms < 50UL || duration_ms > 60000UL) {
        return false;
    }

    s_prec.active         = false;
    s_prec.completed      = false;
    s_prec.range_changed  = false;
    s_prec.eval_range     = current_range;
    s_prec.target_chunks  = duration_ms; /* 1 chunk = 1 ms */
    s_prec.elapsed_chunks = 0UL;

    welford_reset(&s_prec.cur_ua);
    welford_reset(&s_prec.raw_cur);
    welford_reset(&s_prec.raw_ref);
    for (uint8_t i = 0U; i < ADS1220_CH_COUNT; i++) {
        welford_reset(&s_prec.vol_mv[i]);
        welford_reset(&s_prec.raw_ads[i]);
    }

    s_prec.active = true;
    return true;
}

void svc_sampler_get_precision_report(svc_precision_report_t *out_report)
{
    if (out_report == NULL) {
        return;
    }

    memset(out_report, 0, sizeof(*out_report));
    out_report->active                   = s_prec.active;
    out_report->completed                = s_prec.completed;
    out_report->range_changed_during_run = s_prec.range_changed;
    out_report->evaluated_range          = s_prec.eval_range;
    out_report->target_duration_ms       = s_prec.target_chunks;
    out_report->elapsed_ms               = s_prec.elapsed_chunks;

    out_report->cur_sample_count = s_prec.cur_ua.count;
    out_report->cur_mean_ua      = s_prec.cur_ua.mean;
    out_report->cur_stddev_ua    = welford_stddev(&s_prec.cur_ua);
    out_report->cur_min_ua       = s_prec.cur_ua.min_val;
    out_report->cur_max_ua       = s_prec.cur_ua.max_val;
    out_report->raw_cur_mean_lsb = s_prec.raw_cur.mean;
    out_report->raw_ref_mean_lsb = s_prec.raw_ref.mean;

    for (uint8_t i = 0U; i < ADS1220_CH_COUNT; i++) {
        out_report->vol_sample_count[i]  = s_prec.vol_mv[i].count;
        out_report->vol_mean_mv[i]       = s_prec.vol_mv[i].mean;
        out_report->vol_stddev_mv[i]     = welford_stddev(&s_prec.vol_mv[i]);
        out_report->vol_min_mv[i]        = s_prec.vol_mv[i].min_val;
        out_report->vol_max_mv[i]        = s_prec.vol_mv[i].max_val;
        out_report->raw_ads_mean_code[i] = s_prec.raw_ads[i].mean;
    }
}

bool svc_sampler_tare_current_zero(shunt_range_t range, double measured_mean_ua)
{
    svc_calib_data_t cal;
    svc_calib_get(&cal);

    uint8_t idx = range_to_cal_idx(range);
    if (fabsf(cal.cur_gain[idx]) < 1.0e-3f) {
        return false;
    }

    /* Since I_out = (I_raw + old_offset) * gain, at true 0 A:
     * new_offset = old_offset - (measured_mean_ua / gain)
     */
    double new_offset = (double)cal.cur_offset_ua[idx] - (measured_mean_ua / (double)cal.cur_gain[idx]);
    return svc_calib_set_current_range(idx, cal.cur_gain[idx], (float)new_offset);
}

bool svc_sampler_calibrate_current_span(shunt_range_t range,
                                        double measured_mean_ua,
                                        double reference_true_ua)
{
    svc_calib_data_t cal;
    svc_calib_get(&cal);

    uint8_t idx = range_to_cal_idx(range);
    if (fabs(measured_mean_ua) < 1.0e-3 || fabs(reference_true_ua) < 1.0e-3) {
        return false;
    }

    /* After zero-tare at 0 A, I_meas = (I_raw + offset) * old_gain -> scale gain directly */
    double new_gain = (double)cal.cur_gain[idx] * (reference_true_ua / measured_mean_ua);
    return svc_calib_set_current_range(idx, (float)new_gain, cal.cur_offset_ua[idx]);
}

bool svc_sampler_calibrate_voltage_span(ads1220_channel_t ch,
                                        double measured_mean_mv,
                                        double reference_true_mv)
{
    if ((uint8_t)ch >= ADS1220_CH_COUNT) {
        return false;
    }

    svc_calib_data_t cal;
    svc_calib_get(&cal);

    if (fabs(measured_mean_mv) < 1.0 || fabs(reference_true_mv) < 1.0) {
        return false;
    }

    double new_gain = (double)cal.vol_gain[ch] * (reference_true_mv / measured_mean_mv);
    return svc_calib_set_voltage_ch((uint8_t)ch, (float)new_gain, cal.vol_offset_mv[ch]);
}
