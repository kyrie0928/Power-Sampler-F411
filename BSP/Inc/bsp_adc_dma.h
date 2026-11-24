#ifndef BSP_ADC_DMA_H
#define BSP_ADC_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BSP_ADC_SAMPLE_RATE_HZ      10000U
#define BSP_ADC_CHUNK_SAMPLES       10U    /* 10 samples = 1 ms per chunk @ 10 kHz */
#define BSP_ADC_CH_COUNT            4U     /* IN6(LOW), IN7(MID), IN8(HIGH), IN9(1V5_REF) */
#define BSP_ADC_RING_DEPTH          16U    /* 16 ms lossless SPSC queue */

typedef enum {
    BSP_ADC_IDX_LOW  = 0, /* PA6 / ADC1_IN6: IADC3 (50R shunt) */
    BSP_ADC_IDX_MID  = 1, /* PA7 / ADC1_IN7: IADC2 (500mR shunt) */
    BSP_ADC_IDX_HIGH = 2, /* PB0 / ADC1_IN8: IADC1 (5mR shunt) */
    BSP_ADC_IDX_REF  = 3  /* PB1 / ADC1_IN9: 1V5_REF */
} bsp_adc_ch_index_t;

typedef struct {
    uint64_t timestamp_us;
    uint16_t samples[BSP_ADC_CHUNK_SAMPLES][BSP_ADC_CH_COUNT];
} bsp_adc_chunk_t;

/* Optional fast ISR hook (must be ultra-short, integer-only, non-blocking) */
typedef void (*bsp_adc_fast_guard_fn_t)(const uint16_t samples[BSP_ADC_CHUNK_SAMPLES][BSP_ADC_CH_COUNT]);

bool bsp_adc_dma_init(bsp_adc_fast_guard_fn_t fast_guard_cb);
bool bsp_adc_dma_start(void);
bool bsp_adc_dma_pop_chunk(bsp_adc_chunk_t *out_chunk);
uint32_t bsp_adc_dma_get_dropped_chunks(void);
void bsp_adc_dma_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_DMA_H */
