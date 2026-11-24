#include "bsp_i2c_bus.h"
#include "bsp_timebase.h"
#include "board_pins.h"

static I2C_HandleTypeDef s_hi2c1;

/* 9-clock bus recovery in case slave held SDA low across MCU reset */
static void i2c1_bus_recover_pins(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(BOARD_I2C_SCL_PORT, BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN, GPIO_PIN_SET);
    gpio.Pin   = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_I2C_SCL_PORT, &gpio);

    if (HAL_GPIO_ReadPin(BOARD_I2C_SDA_PORT, BOARD_I2C_SDA_PIN) == GPIO_PIN_RESET) {
        for (uint8_t i = 0U; i < 9U; i++) {
            HAL_GPIO_WritePin(BOARD_I2C_SCL_PORT, BOARD_I2C_SCL_PIN, GPIO_PIN_RESET);
            bsp_timebase_delay_us(6U);
            HAL_GPIO_WritePin(BOARD_I2C_SCL_PORT, BOARD_I2C_SCL_PIN, GPIO_PIN_SET);
            bsp_timebase_delay_us(6U);
        }
        /* Generate STOP condition */
        HAL_GPIO_WritePin(BOARD_I2C_SDA_PORT, BOARD_I2C_SDA_PIN, GPIO_PIN_RESET);
        bsp_timebase_delay_us(6U);
        HAL_GPIO_WritePin(BOARD_I2C_SCL_PORT, BOARD_I2C_SCL_PIN, GPIO_PIN_SET);
        bsp_timebase_delay_us(6U);
        HAL_GPIO_WritePin(BOARD_I2C_SDA_PORT, BOARD_I2C_SDA_PIN, GPIO_PIN_SET);
        bsp_timebase_delay_us(6U);
    }
}

bool bsp_i2c1_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    i2c1_bus_recover_pins();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin       = BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(BOARD_I2C_SCL_PORT, &gpio);

    __HAL_RCC_I2C1_FORCE_RESET();
    bsp_timebase_delay_us(5U);
    __HAL_RCC_I2C1_RELEASE_RESET();

    s_hi2c1.Instance             = I2C1;
    s_hi2c1.Init.ClockSpeed      = 100000UL;
    s_hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    s_hi2c1.Init.OwnAddress1     = 0U;
    s_hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_hi2c1.Init.OwnAddress2     = 0U;
    s_hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    return (HAL_I2C_Init(&s_hi2c1) == HAL_OK);
}

bool bsp_i2c1_mem_read(uint8_t dev_addr_7bit,
                       uint8_t mem_addr,
                       uint8_t *data,
                       uint16_t length,
                       uint32_t timeout_ms)
{
    if (data == NULL || length == 0U) {
        return false;
    }

    return (HAL_I2C_Mem_Read(&s_hi2c1,
                             (uint16_t)((uint16_t)dev_addr_7bit << 1U),
                             mem_addr,
                             I2C_MEMADD_SIZE_8BIT,
                             data,
                             length,
                             timeout_ms) == HAL_OK);
}

bool bsp_i2c1_mem_write(uint8_t dev_addr_7bit,
                        uint8_t mem_addr,
                        const uint8_t *data,
                        uint16_t length,
                        uint32_t timeout_ms)
{
    if (data == NULL || length == 0U) {
        return false;
    }

    return (HAL_I2C_Mem_Write(&s_hi2c1,
                              (uint16_t)((uint16_t)dev_addr_7bit << 1U),
                              mem_addr,
                              I2C_MEMADD_SIZE_8BIT,
                              (uint8_t *)data,
                              length,
                              timeout_ms) == HAL_OK);
}

bool bsp_i2c1_wait_ready(uint8_t dev_addr_7bit, uint32_t trials, uint32_t timeout_ms)
{
    return (HAL_I2C_IsDeviceReady(&s_hi2c1,
                                  (uint16_t)((uint16_t)dev_addr_7bit << 1U),
                                  trials,
                                  timeout_ms) == HAL_OK);
}
