#include "main.h"
#include "bsp_gpio.h"
#include "app_main.h"

static bool system_clock_config_100mhz(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    /* 25 MHz HSE / 25 * 200 / 2 = 100 MHz SYSCLK */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 25U;
    osc.PLL.PLLN       = 200U;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 4U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2; /* PCLK1 = 50 MHz, APB1 Timer = 100 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1; /* PCLK2 = 100 MHz, APB2 Timer = 100 MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        return false;
    }

    return true;
}

int main(void)
{
    SCB->VTOR = FLASH_BASE;

    HAL_Init();

    /* Immediately latch HIGH_CUR (5mR bypass) before any long initialization */
    bsp_gpio_init();

    if (!system_clock_config_100mhz()) {
        Error_Handler();
    }

    app_main_init();

    while (1) {
        app_main_poll();
    }
}

void Error_Handler(void)
{
    __disable_irq();
    /* Ensure HIGH_CUR bypass stays active so R16 (50R) is protected */
    GPIOB->BSRR = (uint32_t)BOARD_RANGE_EN1_PIN | ((uint32_t)BOARD_RANGE_EN2_PIN << 16U);
    while (1) {
    }
}
