#include "stm32f4xx_hal.h"
#include "board_pins.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();

    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(SysTick_IRQn, BOARD_IRQ_PRIO_SYSTICK, 0U);
}
