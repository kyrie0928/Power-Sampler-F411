#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "drv_shunt_range.h"
#include "drv_ads1220.h"

void app_main_init(void);
void app_main_poll(void);

/* Shared application control entrypoints used by both Host Protocol and Diag CLI */
bool app_main_set_range_mode(shunt_range_mode_t mode);
bool app_main_set_ads_mode(ads1220_work_mode_t mode);
bool app_main_is_eeprom_ok(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
