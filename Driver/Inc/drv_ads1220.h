#ifndef DRV_ADS1220_H
#define DRV_ADS1220_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define ADS1220_CH_COUNT 4U

typedef enum {
    ADS1220_CH_MAIN_SENS4 = 0, /* AIN0 - AVSS: SENS4 main bus voltage */
    ADS1220_CH_VIN1       = 1, /* AIN1 - AVSS: H1.1 V_IN1 */
    ADS1220_CH_VIN2       = 2, /* AIN2 - AVSS: H1.2 V_IN2 */
    ADS1220_CH_VIN3       = 3  /* AIN3 - AVSS: H1.3 V_IN3 */
} ads1220_channel_t;

typedef enum {
    ADS1220_MODE_SCAN_4CH       = 0, /* Single-shot round-robin across AIN0..AIN3 @ 330 SPS */
    ADS1220_MODE_FAST_MAIN_ONLY = 1, /* Continuous AIN0 @ 600 SPS for fast power tracking */
    ADS1220_MODE_PREC_20SPS_4CH = 2  /* 20 SPS with 50/60 Hz FIR rejection across AIN0..AIN3 */
} ads1220_work_mode_t;

typedef struct {
    ads1220_channel_t channel;
    int32_t           raw_code24; /* Sign-extended 24-bit code [-8388608 .. +8388607] */
    uint64_t          timestamp_us;
} ads1220_sample_t;

void drv_ads1220_init(ads1220_work_mode_t initial_mode);
bool drv_ads1220_set_mode(ads1220_work_mode_t mode);
ads1220_work_mode_t drv_ads1220_get_mode(void);

/* Non-blocking state machine step; returns true when a new conversion is read */
bool drv_ads1220_poll(ads1220_sample_t *out_sample);

bool drv_ads1220_is_online(void);
uint32_t drv_ads1220_get_error_count(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_ADS1220_H */
