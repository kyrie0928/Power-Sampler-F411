#include "bsp_adc_dma.h"
#include "bsp_timebase.h"
#include "board_pins.h"
#include <string.h>

static ADC_HandleTypeDef s_hadc1;
static DMA_HandleTypeDef s_hdma_adc1;
static TIM_HandleTypeDef s_htim2;

static uint16_t s_dma_pingpong[2U][BSP_ADC_CHUNK_SAMPLES][BSP_ADC_CH_COUNT];

static bsp_adc_chunk_t  s_ring[BSP_ADC_RING_DEPTH];
static volatile uint8_t s_ring_head = 0U;
static volatile uint8_t s_ring_tail = 0U;
static volatile uint32_t s_dropped_chunks = 0UL;
static bsp_adc_fast_guard_fn_t s_fast_guard_cb = NULL;

static bool init_tim2_trigger_10khz(void)
{
    TIM_MasterConfigTypeDef master = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    /* APB1 timer clock = 100 MHz -> /100 = 1 MHz -> /100 = 10 kHz exact update TRGO */
    s_htim2.Instance               = TIM2;
    s_htim2.Init.Prescaler         = (uint32_t)((BOARD_APB1_TIM_CLK_HZ / 1000000UL) - 1UL);
    s_htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim2.Init.Period            = (1000000UL / BSP_ADC_SAMPLE_RATE_HZ) - 1UL;
    s_htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&s_htim2) != HAL_OK) {
        return false;
    }

    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    return (HAL_TIMEx_MasterConfigSynchronization(&s_htim2, &master) == HAL_OK);
}

bool bsp_adc_dma_init(bsp_adc_fast_guard_fn_t fast_guard_cb)
{
    GPIO_InitTypeDef gpio = {0};
    ADC_ChannelConfTypeDef ch_cfg = {0};

    s_fast_guard_cb  = fast_guard_cb;
    s_ring_head      = 0U;
    s_ring_tail      = 0U;
    s_dropped_chunks = 0UL;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* Only configure the 4 connected analog inputs: PA6, PA7, PB0, PB1 (PA5 is floating!) */
    gpio.Pin  = BOARD_ADC_LOW_PIN | BOARD_ADC_MID_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BOARD_ADC_LOW_PORT, &gpio);

    gpio.Pin  = BOARD_ADC_HIGH_PIN | BOARD_ADC_REF_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BOARD_ADC_HIGH_PORT, &gpio);

    s_hdma_adc1.Instance                 = DMA2_Stream0;
    s_hdma_adc1.Init.Channel             = DMA_CHANNEL_0;
    s_hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    s_hdma_adc1.Init.Mode                = DMA_CIRCULAR;
    s_hdma_adc1.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    s_hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&s_hdma_adc1) != HAL_OK) {
        return false;
    }

    __HAL_LINKDMA(&s_hadc1, DMA_Handle, s_hdma_adc1);

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, BOARD_IRQ_PRIO_ADC_DMA, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    /* ADCCLK = PCLK2(100 MHz) / 4 = 25 MHz (within 30 MHz typ / 36 MHz max @ VDDA=3.0V) */
    s_hadc1.Instance                   = ADC1;
    s_hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    s_hadc1.Init.ScanConvMode          = ENABLE;
    s_hadc1.Init.ContinuousConvMode    = DISABLE;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    s_hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T2_TRGO;
    s_hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    s_hadc1.Init.NbrOfConversion       = BSP_ADC_CH_COUNT;
    s_hadc1.Init.DMAContinuousRequests = ENABLE;
    s_hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&s_hadc1) != HAL_OK) {
        return false;
    }

    /* 84 cycles S&H time: 4 * (84 + 12) / 25 MHz = 15.36 us << 100 us period */
    const uint32_t channels[BSP_ADC_CH_COUNT] = {
        ADC_CHANNEL_6, /* Rank 1: PA6 = IADC3 (LOW_CUR 50R) */
        ADC_CHANNEL_7, /* Rank 2: PA7 = IADC2 (MID_CUR 500mR) */
        ADC_CHANNEL_8, /* Rank 3: PB0 = IADC1 (HIGH_CUR 5mR) */
        ADC_CHANNEL_9  /* Rank 4: PB1 = 1V5_REF */
    };

    ch_cfg.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    ch_cfg.Offset       = 0U;
    for (uint8_t i = 0U; i < BSP_ADC_CH_COUNT; i++) {
        ch_cfg.Channel = channels[i];
        ch_cfg.Rank    = (uint32_t)i + 1UL;
        if (HAL_ADC_ConfigChannel(&s_hadc1, &ch_cfg) != HAL_OK) {
            return false;
        }
    }

    return init_tim2_trigger_10khz();
}

bool bsp_adc_dma_start(void)
{
    uint32_t total_halfwords = BSP_ADC_CHUNK_SAMPLES * 2U * BSP_ADC_CH_COUNT;

    if (HAL_ADC_Start_DMA(&s_hadc1, (uint32_t *)s_dma_pingpong, total_halfwords) != HAL_OK) {
        return false;
    }
    return (HAL_TIM_Base_Start(&s_htim2) == HAL_OK);
}

static void push_dma_half_from_isr(const uint16_t src[BSP_ADC_CHUNK_SAMPLES][BSP_ADC_CH_COUNT])
{
    if (s_fast_guard_cb != NULL) {
        s_fast_guard_cb(src);
    }

    uint8_t head = s_ring_head;
    uint8_t next = (uint8_t)((head + 1U) % BSP_ADC_RING_DEPTH);
    if (next == s_ring_tail) {
        s_dropped_chunks++;
        return;
    }

    s_ring[head].timestamp_us = bsp_timebase_get_us();
    memcpy(s_ring[head].samples, src, sizeof(s_ring[head].samples));
    s_ring_head = next;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &s_hadc1) {
        push_dma_half_from_isr((const uint16_t (*)[BSP_ADC_CH_COUNT])s_dma_pingpong[0]);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &s_hadc1) {
        push_dma_half_from_isr((const uint16_t (*)[BSP_ADC_CH_COUNT])s_dma_pingpong[1]);
    }
}

bool bsp_adc_dma_pop_chunk(bsp_adc_chunk_t *out_chunk)
{
    if (out_chunk == NULL) {
        return false;
    }

    uint8_t tail = s_ring_tail;
    if (tail == s_ring_head) {
        return false;
    }

    *out_chunk = s_ring[tail];
    s_ring_tail = (uint8_t)((tail + 1U) % BSP_ADC_RING_DEPTH);
    return true;
}

uint32_t bsp_adc_dma_get_dropped_chunks(void)
{
    return s_dropped_chunks;
}

void bsp_adc_dma_irq_handler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_adc1);
}
