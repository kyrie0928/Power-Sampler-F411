#ifndef DRV_SHUNT_RANGE_H
#define DRV_SHUNT_RANGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SHUNT_RANGE_LOW  = 1, /* 1 uA ~ 500 uA  (R16 = 50 ohm,    EN1=0, EN2=0) */
    SHUNT_RANGE_MID  = 2, /* 500 uA ~ 50 mA (R17 = 500 mohm,  EN1=0, EN2=1) */
    SHUNT_RANGE_HIGH = 3  /* 50 mA ~ 5 A    (R18 = 5 mohm,    EN1=1, EN2=0) */
} shunt_range_t;

typedef enum {
    SHUNT_MODE_AUTO = 0,
    SHUNT_MODE_LOCK_LOW  = SHUNT_RANGE_LOW,
    SHUNT_MODE_LOCK_MID  = SHUNT_RANGE_MID,
    SHUNT_MODE_LOCK_HIGH = SHUNT_RANGE_HIGH
} shunt_range_mode_t;

void drv_shunt_range_init(void);
bool drv_shunt_range_apply(shunt_range_t range);
shunt_range_t drv_shunt_range_get_active(void);

bool drv_shunt_range_set_mode(shunt_range_mode_t mode);
shunt_range_mode_t drv_shunt_range_get_mode(void);
const char *drv_shunt_range_name(shunt_range_t range);

/* Emergency ISR-safe switch to HIGH_CUR (5mR) when in AUTO mode */
void drv_shunt_range_emergency_high_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_SHUNT_RANGE_H */
