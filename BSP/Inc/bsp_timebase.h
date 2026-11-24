#ifndef BSP_TIMEBASE_H
#define BSP_TIMEBASE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

bool bsp_timebase_init(void);
uint64_t bsp_timebase_get_us(void);
uint32_t bsp_timebase_get_ms(void);
void bsp_timebase_delay_us(uint32_t delay_us);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIMEBASE_H */
