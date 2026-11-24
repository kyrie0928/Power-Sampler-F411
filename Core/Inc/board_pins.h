#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if !defined(HOST_UNIT_TEST)
#include "stm32f4xx_hal.h"

static inline uint32_t board_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void board_exit_critical(uint32_t primask)
{
    __set_PRIMASK(primask);
}
#endif

/* =========================================================================
 * System Clock & Bus Frequencies (25 MHz HSE -> 100 MHz SYSCLK)
 * ========================================================================= */
#define BOARD_HSE_FREQ_HZ               25000000UL
#define BOARD_SYSCLK_FREQ_HZ            100000000UL
#define BOARD_HCLK_FREQ_HZ              100000000UL
#define BOARD_APB1_PCLK_HZ              50000000UL
#define BOARD_APB1_TIM_CLK_HZ           100000000UL
#define BOARD_APB2_PCLK_HZ              100000000UL
#define BOARD_APB2_TIM_CLK_HZ           100000000UL

/* =========================================================================
 * Analog Frontend Physical Constants (from EasyEDA Pro '采样pcb' schematic)
 * ========================================================================= */
/* U6: REF3030AIDBZR -> 3.000V connected to U1.9 (VDDA) & R27/R28 1.500V divider */
#define BOARD_VDDA_REF_VOLT             3.000f
#define BOARD_ADC12_FULL_SCALE_LSB      4096.0f

/* U5 / U7 / U8: INA190A2QDCKRQ1 bidirectional current-sense amplifier (Gain = 50 V/V) */
#define BOARD_INA190_GAIN_V_PER_V       50.0f

/* Three-stage series high-side shunt chain: J1(SENS4) -> R16 -> R17 -> R18 -> J2(SENS1) */
#define BOARD_SHUNT_LOW_OHM             50.0f     /* R16: 50 ohm 0.1% (1 uA ~ 500 uA) */
#define BOARD_SHUNT_MID_OHM             0.500f    /* R17: 500 mohm 1% 1W (500 uA ~ 50 mA) */
#define BOARD_SHUNT_HIGH_OHM            0.005f    /* R18: 5 mohm 1% 1W (50 mA ~ 5 A) */

/* U2: ADS1220IPWR 24-bit ADC with internal 2.048 V reference & 47k / 2.2k 0.1% dividers */
#define BOARD_ADS1220_VREF_UV           2048000.0
#define BOARD_ADS1220_FULL_SCALE_CODE   8388608.0 /* 2^23 */
#define BOARD_VDIV_R_TOP_OHM            47000.0   /* R2 / R5 / R10 / R14: 47k 0.1% */
#define BOARD_VDIV_R_BOT_OHM            2200.0    /* R3 / R7 / R12 / R15: 2.2k 0.1% */
#define BOARD_VDIV_GAIN_NOMINAL         ((BOARD_VDIV_R_TOP_OHM + BOARD_VDIV_R_BOT_OHM) / BOARD_VDIV_R_BOT_OHM)

/* =========================================================================
 * Pin Mapping (STM32F411CEU6 UFQFPN-48)
 * ========================================================================= */
#if !defined(HOST_UNIT_TEST)

/* Status LED: LED1 (SZYY0402B blue) on PC13 -> R4(4.7k) -> GND (Active HIGH) */
#define BOARD_LED_PORT                  GPIOC
#define BOARD_LED_PIN                   GPIO_PIN_13

/* High-side PMOS Range Bypass Control:
 *   EN1 (PB4) -> Q3(2N7002) -> DRIVE1 -> Q2(AP30P30Q): bypasses R16+R17 (HIGH_CUR 5mR)
 *   EN2 (PB5) -> Q4(2N7002) -> DRIVE2 -> Q1(AP30P30Q): bypasses R16     (MID_CUR 500mR)
 *   EN1=0, EN2=0 -> Q1 & Q2 OFF                                         (LOW_CUR 50R)
 */
#define BOARD_RANGE_EN1_PORT            GPIOB
#define BOARD_RANGE_EN1_PIN             GPIO_PIN_4
#define BOARD_RANGE_EN2_PORT            GPIOB
#define BOARD_RANGE_EN2_PIN             GPIO_PIN_5

/* U2 ADS1220IPWR on SPI2 + EXTI0:
 *   PB12 = ADC_CS_N, PB13 = SPI2_SCK, PB14 = SPI2_MISO, PB15 = SPI2_MOSI, PA0 = ADC_DRDY_N
 */
#define BOARD_ADS_CS_PORT               GPIOB
#define BOARD_ADS_CS_PIN                GPIO_PIN_12
#define BOARD_ADS_SCK_PORT              GPIOB
#define BOARD_ADS_SCK_PIN               GPIO_PIN_13
#define BOARD_ADS_MISO_PORT             GPIOB
#define BOARD_ADS_MISO_PIN              GPIO_PIN_14
#define BOARD_ADS_MOSI_PORT             GPIOB
#define BOARD_ADS_MOSI_PIN              GPIO_PIN_15
#define BOARD_ADS_DRDY_PORT             GPIOA
#define BOARD_ADS_DRDY_PIN              GPIO_PIN_0

/* U4 BL24C02F EEPROM on I2C1: PB6 = SCL, PB7 = SDA */
#define BOARD_I2C_SCL_PORT              GPIOB
#define BOARD_I2C_SCL_PIN               GPIO_PIN_6
#define BOARD_I2C_SDA_PORT              GPIOB
#define BOARD_I2C_SDA_PIN               GPIO_PIN_7
#define BOARD_EEPROM_I2C_ADDR_7BIT      0x50U

/* Internal ADC1 Current Channels:
 *   PA6 (IN6) = IADC3 (U5 across R16 50R, LOW_CUR)
 *   PA7 (IN7) = IADC2 (U7 across R17 500mR, MID_CUR)
 *   PB0 (IN8) = IADC1 (U8 across R18 5mR, HIGH_CUR)
 *   PB1 (IN9) = 1V5_REF (U9 TLV333 buffered midpoint reference)
 */
#define BOARD_ADC_LOW_PORT              GPIOA
#define BOARD_ADC_LOW_PIN               GPIO_PIN_6
#define BOARD_ADC_MID_PORT              GPIOA
#define BOARD_ADC_MID_PIN               GPIO_PIN_7
#define BOARD_ADC_HIGH_PORT             GPIOB
#define BOARD_ADC_HIGH_PIN              GPIO_PIN_0
#define BOARD_ADC_REF_PORT              GPIOB
#define BOARD_ADC_REF_PIN               GPIO_PIN_1

/* H4 Host UART (ESP32 lvgl-demo): USART1 PA9 = TX (to HOST_RX), PA10 = RX (from HOST_TX) */
#define BOARD_HOST_UART_TX_PORT         GPIOA
#define BOARD_HOST_UART_TX_PIN          GPIO_PIN_9
#define BOARD_HOST_UART_RX_PORT         GPIOA
#define BOARD_HOST_UART_RX_PIN          GPIO_PIN_10
#define BOARD_HOST_UART_DEFAULT_BAUD    115200UL

/* H3 Diagnostic & Precision Test CLI UART: USART2 PA2 = TX, PA3 = RX */
#define BOARD_DIAG_UART_TX_PORT         GPIOA
#define BOARD_DIAG_UART_TX_PIN          GPIO_PIN_2
#define BOARD_DIAG_UART_RX_PORT         GPIOA
#define BOARD_DIAG_UART_RX_PIN          GPIO_PIN_3
#define BOARD_DIAG_UART_DEFAULT_BAUD    115200UL

/* H1 Reserved Expansion GPIOs: H1.6 = PA8, H1.5 = PA11, H1.4 = PA12 */
#define BOARD_EXT_IO_PA8_PORT           GPIOA
#define BOARD_EXT_IO_PA8_PIN            GPIO_PIN_8
#define BOARD_EXT_IO_PA11_PORT          GPIOA
#define BOARD_EXT_IO_PA11_PIN           GPIO_PIN_11
#define BOARD_EXT_IO_PA12_PORT          GPIOA
#define BOARD_EXT_IO_PA12_PIN           GPIO_PIN_12

/* NVIC Interrupt Priorities (4-bit preemption, 0 = highest .. 15 = lowest) */
#define BOARD_IRQ_PRIO_ADC_DMA          2U
#define BOARD_IRQ_PRIO_ADS_DRDY         4U
#define BOARD_IRQ_PRIO_HOST_UART        5U
#define BOARD_IRQ_PRIO_DIAG_UART        6U
#define BOARD_IRQ_PRIO_SYSTICK          15U

#endif /* !HOST_UNIT_TEST */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PINS_H */
