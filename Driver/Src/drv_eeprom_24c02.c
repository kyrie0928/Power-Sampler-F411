#include "drv_eeprom_24c02.h"
#include "bsp_i2c_bus.h"
#include "board_pins.h"

#define EEPROM_RW_TIMEOUT_MS  30U
#define EEPROM_ACK_TRIALS     25U
#define EEPROM_ACK_TIMEOUT_MS 2U

bool drv_eeprom_is_present(void)
{
    return bsp_i2c1_wait_ready(BOARD_EEPROM_I2C_ADDR_7BIT, 3U, EEPROM_ACK_TIMEOUT_MS);
}

bool drv_eeprom_read(uint8_t mem_addr, uint8_t *out_data, uint16_t length)
{
    if (out_data == NULL || length == 0U ||
        ((uint16_t)mem_addr + length) > DRV_EEPROM_TOTAL_BYTES) {
        return false;
    }

    return bsp_i2c1_mem_read(BOARD_EEPROM_I2C_ADDR_7BIT,
                             mem_addr,
                             out_data,
                             length,
                             EEPROM_RW_TIMEOUT_MS);
}

bool drv_eeprom_write(uint8_t mem_addr, const uint8_t *data, uint16_t length)
{
    if (data == NULL || length == 0U ||
        ((uint16_t)mem_addr + length) > DRV_EEPROM_TOTAL_BYTES) {
        return false;
    }

    uint16_t cur_addr  = mem_addr;
    uint16_t remaining = length;
    const uint8_t *ptr = data;

    while (remaining > 0U) {
        uint8_t page_offset = (uint8_t)(cur_addr & (DRV_EEPROM_PAGE_BYTES - 1U));
        uint8_t page_space  = (uint8_t)(DRV_EEPROM_PAGE_BYTES - page_offset);
        uint8_t chunk       = (remaining < page_space) ? (uint8_t)remaining : page_space;

        if (!bsp_i2c1_mem_write(BOARD_EEPROM_I2C_ADDR_7BIT,
                                (uint8_t)cur_addr,
                                ptr,
                                chunk,
                                EEPROM_RW_TIMEOUT_MS)) {
            return false;
        }
        /* Poll internal write cycle completion (~3-5 ms typical) */
        if (!bsp_i2c1_wait_ready(BOARD_EEPROM_I2C_ADDR_7BIT,
                                 EEPROM_ACK_TRIALS,
                                 EEPROM_ACK_TIMEOUT_MS)) {
            return false;
        }

        cur_addr  = (uint16_t)(cur_addr + chunk);
        ptr      += chunk;
        remaining = (uint16_t)(remaining - chunk);
    }

    return true;
}
