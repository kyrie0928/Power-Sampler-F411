#include "bsp_timebase.h"
#include "board_pins.h"

static TIM_HandleTypeDef s_htim5;
static volatile uint64_t s_total_us = 0ULL;
static volatile uint32_t s_last_cnt = 0UL;

bool bsp_timebase_init(void)
{
    __HAL_RCC_TIM5_CLK_ENABLE();

    s_htim5.Instance               = TIM5;
    s_htim5.Init.Prescaler         = (uint32_t)((BOARD_APB1_TIM_CLK_HZ / 1000000UL) - 1UL);
    s_htim5.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim5.Init.Period            = 0xFFFFFFFFUL;
    s_htim5.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&s_htim5) != HAL_OK) {
        return false;
    }

    __HAL_TIM_SET_COUNTER(&s_htim5, 0UL);
    s_last_cnt = 0UL;
    s_total_us = 0ULL;

    return (HAL_TIM_Base_Start(&s_htim5) == HAL_OK);
}

uint64_t bsp_timebase_get_us(void)
{
    uint32_t primask = board_enter_critical();
    uint32_t cur = __HAL_TIM_GET_COUNTER(&s_htim5);
    uint32_t delta = cur - s_last_cnt; /* Unsigned 32-bit wrap-safe subtraction */

    s_last_cnt = cur;
    s_total_us += (uint64_t)delta;
    uint64_t snapshot = s_total_us;
    board_exit_critical(primask);

    return snapshot;
}

uint32_t bsp_timebase_get_ms(void)
{
    return HAL_GetTick();
}

void bsp_timebase_delay_us(uint32_t delay_us)
{
    uint32_t start = __HAL_TIM_GET_COUNTER(&s_htim5);
    while ((uint32_t)(__HAL_TIM_GET_COUNTER(&s_htim5) - start) < delay_us) {
        __NOP();
    }
}
