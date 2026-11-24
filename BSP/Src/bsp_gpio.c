#include "bsp_gpio.h"
#include "board_pins.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} ext_io_hw_t;

static const ext_io_hw_t s_ext_io_map[BSP_EXT_IO_COUNT] = {
    [BSP_EXT_IO_PA8]  = { BOARD_EXT_IO_PA8_PORT,  BOARD_EXT_IO_PA8_PIN  },
    [BSP_EXT_IO_PA11] = { BOARD_EXT_IO_PA11_PORT, BOARD_EXT_IO_PA11_PIN },
    [BSP_EXT_IO_PA12] = { BOARD_EXT_IO_PA12_PORT, BOARD_EXT_IO_PA12_PIN },
};

static bsp_ext_io_mode_t s_ext_io_modes[BSP_EXT_IO_COUNT] = {
    BSP_EXT_IO_MODE_INPUT_NOPULL,
    BSP_EXT_IO_MODE_INPUT_NOPULL,
    BSP_EXT_IO_MODE_INPUT_NOPULL,
};

void bsp_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Preload EN1=HIGH (Q2 5mR bypass ON) and EN2=LOW before switching PB4/PB5 to output */
    BOARD_RANGE_EN1_PORT->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN
                               | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);

    gpio.Pin   = BOARD_RANGE_EN1_PIN | BOARD_RANGE_EN2_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOARD_RANGE_EN1_PORT, &gpio);

    /* Status LED on PC13: default OFF */
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, GPIO_PIN_RESET);
    gpio.Pin   = BOARD_LED_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LED_PORT, &gpio);

    /* H1 reserved GPIOs (PA8, PA11, PA12): safe high-Z input by default */
    for (uint8_t i = 0U; i < (uint8_t)BSP_EXT_IO_COUNT; i++) {
        (void)bsp_ext_io_configure((bsp_ext_io_id_t)i, BSP_EXT_IO_MODE_INPUT_NOPULL, false);
    }
}

void bsp_gpio_set_range_pins(bool en1_high, bool en2_high)
{
    uint32_t bsrr = 0U;

    bsrr |= en1_high ? (uint32_t)BOARD_RANGE_EN1_PIN : ((uint32_t)BOARD_RANGE_EN1_PIN << 16U);
    bsrr |= en2_high ? (uint32_t)BOARD_RANGE_EN2_PIN : ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    BOARD_RANGE_EN1_PORT->BSRR = bsrr;
}

void bsp_gpio_get_range_pins(bool *out_en1_high, bool *out_en2_high)
{
    uint32_t odr = BOARD_RANGE_EN1_PORT->ODR;

    if (out_en1_high != NULL) {
        *out_en1_high = ((odr & BOARD_RANGE_EN1_PIN) != 0U);
    }
    if (out_en2_high != NULL) {
        *out_en2_high = ((odr & BOARD_RANGE_EN2_PIN) != 0U);
    }
}

void bsp_led_set(bool on)
{
    HAL_GPIO_WritePin(BOARD_LED_PORT, BOARD_LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void bsp_led_toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
}

bool bsp_ext_io_configure(bsp_ext_io_id_t id, bsp_ext_io_mode_t mode, bool initial_high)
{
    GPIO_InitTypeDef gpio = {0};

    if ((uint8_t)id >= (uint8_t)BSP_EXT_IO_COUNT) {
        return false;
    }

    gpio.Pin   = s_ext_io_map[id].pin;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;

    switch (mode) {
        case BSP_EXT_IO_MODE_INPUT_NOPULL:
            gpio.Mode = GPIO_MODE_INPUT;
            gpio.Pull = GPIO_NOPULL;
            break;
        case BSP_EXT_IO_MODE_INPUT_PULLUP:
            gpio.Mode = GPIO_MODE_INPUT;
            gpio.Pull = GPIO_PULLUP;
            break;
        case BSP_EXT_IO_MODE_INPUT_PULLDOWN:
            gpio.Mode = GPIO_MODE_INPUT;
            gpio.Pull = GPIO_PULLDOWN;
            break;
        case BSP_EXT_IO_MODE_OUTPUT_PP:
            HAL_GPIO_WritePin(s_ext_io_map[id].port,
                              s_ext_io_map[id].pin,
                              initial_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
            gpio.Mode = GPIO_MODE_OUTPUT_PP;
            gpio.Pull = GPIO_NOPULL;
            break;
        case BSP_EXT_IO_MODE_OUTPUT_OD:
            HAL_GPIO_WritePin(s_ext_io_map[id].port,
                              s_ext_io_map[id].pin,
                              initial_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
            gpio.Mode = GPIO_MODE_OUTPUT_OD;
            gpio.Pull = GPIO_NOPULL;
            break;
        default:
            return false;
    }

    HAL_GPIO_Init(s_ext_io_map[id].port, &gpio);
    s_ext_io_modes[id] = mode;
    return true;
}

bool bsp_ext_io_write(bsp_ext_io_id_t id, bool high)
{
    if ((uint8_t)id >= (uint8_t)BSP_EXT_IO_COUNT) {
        return false;
    }
    if (s_ext_io_modes[id] != BSP_EXT_IO_MODE_OUTPUT_PP &&
        s_ext_io_modes[id] != BSP_EXT_IO_MODE_OUTPUT_OD) {
        return false;
    }

    HAL_GPIO_WritePin(s_ext_io_map[id].port,
                      s_ext_io_map[id].pin,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return true;
}

bool bsp_ext_io_read(bsp_ext_io_id_t id, bool *out_high)
{
    if ((uint8_t)id >= (uint8_t)BSP_EXT_IO_COUNT || out_high == NULL) {
        return false;
    }

    *out_high = (HAL_GPIO_ReadPin(s_ext_io_map[id].port, s_ext_io_map[id].pin) == GPIO_PIN_SET);
    return true;
}

uint8_t bsp_ext_io_read_mask(void)
{
    uint8_t mask = 0U;

    for (uint8_t i = 0U; i < (uint8_t)BSP_EXT_IO_COUNT; i++) {
        bool state = false;
        if (bsp_ext_io_read((bsp_ext_io_id_t)i, &state) && state) {
            mask |= (uint8_t)(1U << i);
        }
    }
    return mask;
}

bsp_ext_io_mode_t bsp_ext_io_get_mode(bsp_ext_io_id_t id)
{
    if ((uint8_t)id >= (uint8_t)BSP_EXT_IO_COUNT) {
        return BSP_EXT_IO_MODE_INPUT_NOPULL;
    }
    return s_ext_io_modes[id];
}
