#ifndef BSP_SPI_BUS_H
#define BSP_SPI_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

bool bsp_spi_ads_init(void);
void bsp_spi_ads_cs_set(bool active_low_selected);
bool bsp_spi_ads_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t length, uint32_t timeout_ms);

bool bsp_spi_ads_drdy_is_low(void);
bool bsp_spi_ads_consume_drdy_event(uint64_t *out_timestamp_us);
void bsp_spi_ads_clear_drdy_event(void);
void bsp_spi_ads_drdy_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_BUS_H */
