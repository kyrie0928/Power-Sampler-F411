#include "bsp_uart.h"
#include "board_pins.h"
#include <string.h>

#define UART_RX_RING_SIZE 512U
#define UART_TX_RING_SIZE 2048U

typedef struct {
    USART_TypeDef    *regs;
    uint8_t           rx_buf[UART_RX_RING_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    uint8_t           tx_buf[UART_TX_RING_SIZE];
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
    volatile uint32_t rx_overrun_cnt;
    bool              initialized;
} uart_ctx_t;

static uart_ctx_t s_uarts[BSP_UART_COUNT];

bool bsp_uart_init(bsp_uart_port_t port, uint32_t baudrate)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t pclk_hz = 0UL;
    IRQn_Type irqn;
    uint32_t prio;

    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT || baudrate == 0UL) {
        return false;
    }

    uart_ctx_t *ctx = &s_uarts[port];
    memset(ctx, 0, sizeof(*ctx));

    __HAL_RCC_GPIOA_CLK_ENABLE();

    if (port == BSP_UART_HOST) {
        __HAL_RCC_USART1_CLK_ENABLE();
        ctx->regs = USART1;
        pclk_hz   = HAL_RCC_GetPCLK2Freq();
        irqn      = USART1_IRQn;
        prio      = BOARD_IRQ_PRIO_HOST_UART;

        gpio.Pin       = BOARD_HOST_UART_TX_PIN | BOARD_HOST_UART_RX_PIN;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(BOARD_HOST_UART_TX_PORT, &gpio);
    } else {
        __HAL_RCC_USART2_CLK_ENABLE();
        ctx->regs = USART2;
        pclk_hz   = HAL_RCC_GetPCLK1Freq();
        irqn      = USART2_IRQn;
        prio      = BOARD_IRQ_PRIO_DIAG_UART;

        gpio.Pin       = BOARD_DIAG_UART_TX_PIN | BOARD_DIAG_UART_RX_PIN;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(BOARD_DIAG_UART_TX_PORT, &gpio);
    }

    if (pclk_hz == 0UL) {
        return false;
    }

    ctx->regs->CR1 = 0UL;
    ctx->regs->CR2 = 0UL;
    ctx->regs->CR3 = 0UL;
    /* OVER8 = 0 (16x oversampling): BRR = round(PCLK / baud) */
    ctx->regs->BRR = (pclk_hz + (baudrate / 2UL)) / baudrate;
    ctx->regs->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    ctx->initialized = true;

    HAL_NVIC_SetPriority(irqn, prio, 0U);
    HAL_NVIC_EnableIRQ(irqn);
    return true;
}

size_t bsp_uart_write(bsp_uart_port_t port, const uint8_t *data, size_t length)
{
    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT || data == NULL || length == 0U) {
        return 0U;
    }

    uart_ctx_t *ctx = &s_uarts[port];
    if (!ctx->initialized) {
        return 0U;
    }

    size_t written = 0U;
    uint32_t primask = board_enter_critical();

    while (written < length) {
        uint16_t next = (uint16_t)((ctx->tx_head + 1U) % UART_TX_RING_SIZE);
        if (next == ctx->tx_tail) {
            break;
        }
        ctx->tx_buf[ctx->tx_head] = data[written++];
        ctx->tx_head = next;
    }

    if (ctx->tx_head != ctx->tx_tail) {
        ctx->regs->CR1 |= USART_CR1_TXEIE;
    }

    board_exit_critical(primask);
    return written;
}

size_t bsp_uart_write_str(bsp_uart_port_t port, const char *str)
{
    if (str == NULL) {
        return 0U;
    }
    return bsp_uart_write(port, (const uint8_t *)str, strlen(str));
}

size_t bsp_uart_read(bsp_uart_port_t port, uint8_t *out_buf, size_t max_length)
{
    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT || out_buf == NULL || max_length == 0U) {
        return 0U;
    }

    uart_ctx_t *ctx = &s_uarts[port];
    if (!ctx->initialized) {
        return 0U;
    }

    size_t count = 0U;
    uint16_t tail = ctx->rx_tail;

    while (count < max_length && tail != ctx->rx_head) {
        out_buf[count++] = ctx->rx_buf[tail];
        tail = (uint16_t)((tail + 1U) % UART_RX_RING_SIZE);
    }
    ctx->rx_tail = tail;
    return count;
}

uint32_t bsp_uart_get_rx_overrun_count(bsp_uart_port_t port)
{
    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT) {
        return 0UL;
    }
    return s_uarts[port].rx_overrun_cnt;
}

void bsp_uart_flush_tx_blocking(bsp_uart_port_t port, uint32_t timeout_ms)
{
    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT || !s_uarts[port].initialized) {
        return;
    }

    uart_ctx_t *ctx = &s_uarts[port];
    uint32_t start = HAL_GetTick();
    while (ctx->tx_head != ctx->tx_tail || (ctx->regs->SR & USART_SR_TC) == 0U) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            break;
        }
    }
}

void bsp_uart_irq_handler(bsp_uart_port_t port)
{
    if ((uint8_t)port >= (uint8_t)BSP_UART_COUNT) {
        return;
    }

    uart_ctx_t *ctx = &s_uarts[port];
    if (!ctx->initialized) {
        return;
    }

    uint32_t sr  = ctx->regs->SR;
    uint32_t cr1 = ctx->regs->CR1;

    /* Reading SR followed by DR clears RXNE as well as ORE/NF/FE/PE per RM0383 */
    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0U) {
        uint8_t byte = (uint8_t)(ctx->regs->DR & 0xFFU);
        if ((sr & USART_SR_RXNE) != 0U && (sr & (USART_SR_FE | USART_SR_PE)) == 0U) {
            uint16_t next = (uint16_t)((ctx->rx_head + 1U) % UART_RX_RING_SIZE);
            if (next != ctx->rx_tail) {
                ctx->rx_buf[ctx->rx_head] = byte;
                ctx->rx_head = next;
            } else {
                ctx->rx_overrun_cnt++;
            }
        }
        if ((sr & USART_SR_ORE) != 0U) {
            ctx->rx_overrun_cnt++;
        }
    }

    if ((cr1 & USART_CR1_TXEIE) != 0U && (sr & USART_SR_TXE) != 0U) {
        uint16_t tail = ctx->tx_tail;
        if (tail != ctx->tx_head) {
            ctx->regs->DR = ctx->tx_buf[tail];
            ctx->tx_tail = (uint16_t)((tail + 1U) % UART_TX_RING_SIZE);
        } else {
            ctx->regs->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}
