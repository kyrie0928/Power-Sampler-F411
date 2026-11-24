#ifndef DRV_EEPROM_24C02_H
#define DRV_EEPROM_24C02_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define DRV_EEPROM_TOTAL_BYTES 256U
#define DRV_EEPROM_PAGE_BYTES  8U   /* 8-byte page write safe across all 24C02 variants */

bool drv_eeprom_is_present(void);
bool drv_eeprom_read(uint8_t mem_addr, uint8_t *out_data, uint16_t length);
bool drv_eeprom_write(uint8_t mem_addr, const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* DRV_EEPROM_24C02_H */
