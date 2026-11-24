#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_EXT_IO_PA8  = 0, /* H1 Pin 6 */
    BSP_EXT_IO_PA11 = 1, /* H1 Pin 5 */
    BSP_EXT_IO_PA12 = 2, /* H1 Pin 4 */
    BSP_EXT_IO_COUNT = 3
} bsp_ext_io_id_t;

typedef enum {
    BSP_EXT_IO_MODE_INPUT_NOPULL   = 0,
    BSP_EXT_IO_MODE_INPUT_PULLUP   = 1,
    BSP_EXT_IO_MODE_INPUT_PULLDOWN = 2,
    BSP_EXT_IO_MODE_OUTPUT_PP      = 3,
    BSP_EXT_IO_MODE_OUTPUT_OD      = 4
} bsp_ext_io_mode_t;

void bsp_gpio_init(void);

/* Atomic single-cycle BSRR update for PB4(EN1) & PB5(EN2) range gate drivers */
void bsp_gpio_set_range_pins(bool en1_high, bool en2_high);
void bsp_gpio_get_range_pins(bool *out_en1_high, bool *out_en2_high);

void bsp_led_set(bool on);
void bsp_led_toggle(void);

/* H1 reserved GPIO management (PA8 / PA11 / PA12) */
bool bsp_ext_io_configure(bsp_ext_io_id_t id, bsp_ext_io_mode_t mode, bool initial_high);
bool bsp_ext_io_write(bsp_ext_io_id_t id, bool high);
bool bsp_ext_io_read(bsp_ext_io_id_t id, bool *out_high);
uint8_t bsp_ext_io_read_mask(void);
bsp_ext_io_mode_t bsp_ext_io_get_mode(bsp_ext_io_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
