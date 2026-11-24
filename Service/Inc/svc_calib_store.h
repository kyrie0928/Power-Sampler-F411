#ifndef SVC_CALIB_STORE_H
#define SVC_CALIB_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define SVC_CALIB_CUR_RANGE_COUNT   3U /* Index 0=LOW(50R), 1=MID(500mR), 2=HIGH(5mR) */
#define SVC_CALIB_VOL_CH_COUNT      4U /* Index 0=SENS4, 1=V_IN1, 2=V_IN2, 3=V_IN3 */

typedef struct {
    float    cur_gain[SVC_CALIB_CUR_RANGE_COUNT];
    float    cur_offset_ua[SVC_CALIB_CUR_RANGE_COUNT];
    float    vol_gain[SVC_CALIB_VOL_CH_COUNT];
    float    vol_offset_mv[SVC_CALIB_VOL_CH_COUNT];
    uint8_t  default_range_mode; /* shunt_range_mode_t */
    uint8_t  default_ads_mode;   /* ads1220_work_mode_t */
} svc_calib_data_t;

/* Injectable storage backend for host unit-testing and hardware decoupling */
typedef struct {
    bool (*read)(uint8_t addr, uint8_t *buf, uint16_t len);
    bool (*write)(uint8_t addr, const uint8_t *buf, uint16_t len);
} svc_calib_nv_ops_t;

uint16_t svc_crc16_ccitt(const uint8_t *data, uint16_t length);

void svc_calib_set_defaults(svc_calib_data_t *out_cal);
bool svc_calib_is_valid(const svc_calib_data_t *cal);

void svc_calib_store_init(const svc_calib_nv_ops_t *nv_ops);
bool svc_calib_store_load(void);
bool svc_calib_store_save(void);
bool svc_calib_store_reset_defaults(bool save_to_nv);

void svc_calib_get(svc_calib_data_t *out_cal);
bool svc_calib_set(const svc_calib_data_t *new_cal);
bool svc_calib_set_current_range(uint8_t range_idx, float gain, float offset_ua);
bool svc_calib_set_voltage_ch(uint8_t ch_idx, float gain, float offset_mv);
bool svc_calib_is_loaded_from_nv(void);

#ifdef __cplusplus
}
#endif

#endif /* SVC_CALIB_STORE_H */
