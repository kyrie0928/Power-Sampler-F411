#ifndef SVC_SAMPLER_H
#define SVC_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "board_pins.h"
#include "drv_shunt_range.h"
#include "drv_ads1220.h"

#define SVC_ADC_CHUNK_LEN           10U
#define SVC_ADC_CH_NUM              4U

/* Nominal current conversion scales (uA / LSB) for VDDA=3.000V, 12-bit ADC, INA190 Gain=50 */
#define SVC_NOMINAL_SCALE_LOW_UA    (BOARD_VDDA_REF_VOLT / BOARD_ADC12_FULL_SCALE_LSB / BOARD_INA190_GAIN_V_PER_V / BOARD_SHUNT_LOW_OHM  * 1000000.0f)
#define SVC_NOMINAL_SCALE_MID_UA    (BOARD_VDDA_REF_VOLT / BOARD_ADC12_FULL_SCALE_LSB / BOARD_INA190_GAIN_V_PER_V / BOARD_SHUNT_MID_OHM  * 1000000.0f)
#define SVC_NOMINAL_SCALE_HIGH_UA   (BOARD_VDDA_REF_VOLT / BOARD_ADC12_FULL_SCALE_LSB / BOARD_INA190_GAIN_V_PER_V / BOARD_SHUNT_HIGH_OHM * 1000000.0f)

/* Status bitmask in svc_sampler_snapshot_t.status_flags */
#define SVC_STATUS_ADS1220_ONLINE   (1U << 0)
#define SVC_STATUS_EEPROM_OK        (1U << 1)
#define SVC_STATUS_CALIB_LOADED     (1U << 2)
#define SVC_STATUS_OVERLOAD_WARN    (1U << 3)
#define SVC_STATUS_RANGE_SETTLING   (1U << 4)
#define SVC_STATUS_WINDOW_READY     (1U << 5)

typedef struct {
    uint64_t           timestamp_us;
    shunt_range_mode_t range_mode;
    shunt_range_t      active_range;
    uint8_t            status_flags;

    /* Smoothed 200 ms display / polling values */
    int64_t            current_10na;             /* 0.01 uA resolution */
    float              current_ua;
    int32_t            voltage_uv[ADS1220_CH_COUNT]; /* [0]=SENS4, [1..3]=V_IN1..3 (1 uV) */
    float              voltage_mv[ADS1220_CH_COUNT];
    int64_t            power_nw;                 /* 1 nW = 0.001 uW */
    float              power_mw;
    int64_t            energy_uj;                /* Cumulative energy in 1 uJ */

    /* Instantaneous raw ADC diagnostics for all 3 shunt amplifiers + 1V5_REF */
    uint16_t           raw_adc_low;
    uint16_t           raw_adc_mid;
    uint16_t           raw_adc_high;
    uint16_t           raw_adc_ref;
    int32_t            raw_ads24[ADS1220_CH_COUNT];
    uint32_t           range_switch_count;
} svc_sampler_snapshot_t;

/* Welford online statistics for bench precision evaluation of each range & voltage channel */
typedef struct {
    bool          active;
    bool          completed;
    bool          range_changed_during_run;
    shunt_range_t evaluated_range;
    uint32_t      target_duration_ms;
    uint32_t      elapsed_ms;

    uint32_t      cur_sample_count;
    double        cur_mean_ua;
    double        cur_stddev_ua;
    double        cur_min_ua;
    double        cur_max_ua;
    double        raw_cur_mean_lsb;
    double        raw_ref_mean_lsb;

    uint32_t      vol_sample_count[ADS1220_CH_COUNT];
    double        vol_mean_mv[ADS1220_CH_COUNT];
    double        vol_stddev_mv[ADS1220_CH_COUNT];
    double        vol_min_mv[ADS1220_CH_COUNT];
    double        vol_max_mv[ADS1220_CH_COUNT];
    double        raw_ads_mean_code[ADS1220_CH_COUNT];
} svc_precision_report_t;

typedef bool (*svc_range_apply_fn_t)(shunt_range_t new_range);

void svc_sampler_init(svc_range_apply_fn_t range_apply_cb);
void svc_sampler_set_hw_flags(bool ads_online, bool eeprom_ok, bool calib_loaded);

float svc_sampler_convert_current_ua(uint16_t cur_adc, uint16_t ref_adc, shunt_range_t range);
int32_t svc_sampler_convert_voltage_uv(int32_t raw_code24, ads1220_channel_t ch);

void svc_sampler_feed_adc_chunk(const uint16_t samples[SVC_ADC_CHUNK_LEN][SVC_ADC_CH_NUM],
                                uint64_t timestamp_us,
                                shunt_range_mode_t mode,
                                shunt_range_t current_hw_range);

void svc_sampler_feed_ads1220_sample(const ads1220_sample_t *sample);

void svc_sampler_get_snapshot(svc_sampler_snapshot_t *out_snapshot);
void svc_sampler_clear_energy(void);
void svc_sampler_notify_manual_range_change(shunt_range_t new_range);
void svc_sampler_notify_calib_changed(void);

/* Bench precision evaluation & interactive calibration helpers */
bool svc_sampler_start_precision_eval(uint32_t duration_ms, shunt_range_t current_range);
void svc_sampler_get_precision_report(svc_precision_report_t *out_report);

bool svc_sampler_tare_current_zero(shunt_range_t range, double measured_mean_ua);
bool svc_sampler_calibrate_current_span(shunt_range_t range, double measured_mean_ua, double reference_true_ua);
bool svc_sampler_calibrate_voltage_span(ads1220_channel_t ch, double measured_mean_mv, double reference_true_mv);

#ifdef __cplusplus
}
#endif

#endif /* SVC_SAMPLER_H */
