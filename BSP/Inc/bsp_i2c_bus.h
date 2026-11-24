#ifndef BSP_I2C_BUS_H
#define BSP_I2C_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

bool bsp_i2c1_init(void);
bool bsp_i2c1_mem_read(uint8_t dev_addr_7bit,
                       uint8_t mem_addr,
                       uint8_t *data,
                       uint16_t length,
                       uint32_t timeout_ms);
bool bsp_i2c1_mem_write(uint8_t dev_addr_7bit,
                        uint8_t mem_addr,
                        const uint8_t *data,
                        uint16_t length,
                        uint32_t timeout_ms);
bool bsp_i2c1_wait_ready(uint8_t dev_addr_7bit, uint32_t trials, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_BUS_H */
