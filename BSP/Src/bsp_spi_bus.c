#include "bsp_spi_bus.h"
#include "bsp_timebase.h"
#include "board_pins.h"

static SPI_HandleTypeDef s_hspi2;
static volatile bool     s_drdy_event_flag = false;
static volatile uint64_t s_drdy_event_us   = 0ULL;

bool bsp_spi_ads_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* PB12 = ADC_CS_N: idle HIGH */
    HAL_GPIO_WritePin(BOARD_ADS_CS_PORT, BOARD_ADS_CS_PIN, GPIO_PIN_SET);
    gpio.Pin   = BOARD_ADS_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(BOARD_ADS_CS_PORT, &gpio);

    /* PB13 = SCK, PB14 = MISO, PB15 = MOSI (AF5 SPI2) */
    gpio.Pin       = BOARD_ADS_SCK_PIN | BOARD_ADS_MOSI_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(BOARD_ADS_SCK_PORT, &gpio);

    gpio.Pin       = BOARD_ADS_MISO_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(BOARD_ADS_MISO_PORT, &gpio);

    /* PA0 = ADC_DRDY_N falling-edge interrupt */
    gpio.Pin   = BOARD_ADS_DRDY_PIN;
    gpio.Mode  = GPIO_MODE_IT_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_ADS_DRDY_PORT, &gpio);

    /* ADS1220 requires SPI Mode 1: CPOL = LOW, CPHA = 2EDGE, SCLK <= 6.67 MHz.
     * PCLK1 = 50 MHz / 16 = 3.125 MHz.
     */
    s_hspi2.Instance               = SPI2;
    s_hspi2.Init.Mode              = SPI_MODE_MASTER;
    s_hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
    s_hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    s_hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
    s_hspi2.Init.CLKPhase          = SPI_PHASE_2EDGE;
    s_hspi2.Init.NSS               = SPI_NSS_SOFT;
    s_hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    s_hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    s_hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
    s_hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    s_hspi2.Init.CRCPolynomial     = 7U;

    if (HAL_SPI_Init(&s_hspi2) != HAL_OK) {
        return false;
    }

    s_drdy_event_flag = false;
    __HAL_GPIO_EXTI_CLEAR_IT(BOARD_ADS_DRDY_PIN);
    HAL_NVIC_SetPriority(EXTI0_IRQn, BOARD_IRQ_PRIO_ADS_DRDY, 0U);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    return true;
}

void bsp_spi_ads_cs_set(bool active_low_selected)
{
    HAL_GPIO_WritePin(BOARD_ADS_CS_PORT,
                      BOARD_ADS_CS_PIN,
                      active_low_selected ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool bsp_spi_ads_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t length, uint32_t timeout_ms)
{
    if (length == 0U || (tx_buf == NULL && rx_buf == NULL)) {
        return false;
    }

    if (tx_buf != NULL && rx_buf != NULL) {
        return (HAL_SPI_TransmitReceive(&s_hspi2, (uint8_t *)tx_buf, rx_buf, length, timeout_ms) == HAL_OK);
    }
    if (tx_buf != NULL) {
        return (HAL_SPI_Transmit(&s_hspi2, (uint8_t *)tx_buf, length, timeout_ms) == HAL_OK);
    }

    /* Receive-only: clock out 0x00 (NOP) bytes as required by ADS1220 RDATA */
    for (uint16_t i = 0U; i < length; i++) {
        uint8_t dummy = 0x00U;
        if (HAL_SPI_TransmitReceive(&s_hspi2, &dummy, &rx_buf[i], 1U, timeout_ms) != HAL_OK) {
            return false;
        }
    }
    return true;
}

bool bsp_spi_ads_drdy_is_low(void)
{
    return (HAL_GPIO_ReadPin(BOARD_ADS_DRDY_PORT, BOARD_ADS_DRDY_PIN) == GPIO_PIN_RESET);
}

bool bsp_spi_ads_consume_drdy_event(uint64_t *out_timestamp_us)
{
    bool triggered = false;
    uint32_t primask = board_enter_critical();

    if (s_drdy_event_flag) {
        s_drdy_event_flag = false;
        triggered = true;
        if (out_timestamp_us != NULL) {
            *out_timestamp_us = s_drdy_event_us;
        }
    }
    board_exit_critical(primask);
    return triggered;
}

void bsp_spi_ads_clear_drdy_event(void)
{
    uint32_t primask = board_enter_critical();
    s_drdy_event_flag = false;
    board_exit_critical(primask);
}

void bsp_spi_ads_drdy_irq_handler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(BOARD_ADS_DRDY_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(BOARD_ADS_DRDY_PIN);
        s_drdy_event_us   = bsp_timebase_get_us();
        s_drdy_event_flag = true;
    }
}
