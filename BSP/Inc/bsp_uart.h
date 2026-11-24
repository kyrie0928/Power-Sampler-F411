#ifndef BSP_UART_H
#define BSP_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BSP_UART_HOST = 0, /* USART1 (PA9 TX / PA10 RX) -> H4 ESP32 lvgl-demo */
    BSP_UART_DIAG = 1, /* USART2 (PA2 TX / PA3 RX)  -> H3 Diagnostic / Precision CLI */
    BSP_UART_COUNT = 2
} bsp_uart_port_t;

bool bsp_uart_init(bsp_uart_port_t port, uint32_t baudrate);
size_t bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, size_t length);
size_t bsp_uart_write_str(bsp_uart_port_t port, const char *str);
size_t bsp_uart_read(bsp_uart_port_t port, uint8_t *out_buf, size_t max_length);
uint32_t bsp_uart_get_rx_overrun_count(bsp_uart_port_t port);
void bsp_uart_flush_tx_blocking(bsp_uart_port_t port, uint32_t timeout_ms);
void bsp_uart_irq_handler(bsp_uart_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
