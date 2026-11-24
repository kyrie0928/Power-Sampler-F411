#include "main.h"
#include "stm32f4xx_it.h"
#include "bsp_adc_dma.h"
#include "bsp_spi_bus.h"
#include "bsp_uart.h"

void NMI_Handler(void)
{
    while (1) {
    }
}

void HardFault_Handler(void)
{
    /* Force safe HIGH_CUR (5mR bypass) on fault to protect 50R shunt */
    GPIOB->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    while (1) {
    }
}

void MemManage_Handler(void)
{
    GPIOB->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    while (1) {
    }
}

void BusFault_Handler(void)
{
    GPIOB->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    while (1) {
    }
}

void UsageFault_Handler(void)
{
    GPIOB->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    while (1) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void EXTI0_IRQHandler(void)
{
    bsp_spi_ads_drdy_irq_handler();
}

void DMA2_Stream0_IRQHandler(void)
{
    bsp_adc_dma_irq_handler();
}

void USART1_IRQHandler(void)
{
    bsp_uart_irq_handler(BSP_UART_HOST);
}

void USART2_IRQHandler(void)
{
    bsp_uart_irq_handler(BSP_UART_DIAG);
}
