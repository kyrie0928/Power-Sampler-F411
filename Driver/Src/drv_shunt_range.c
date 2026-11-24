#include "drv_shunt_range.h"
#include "bsp_gpio.h"
#include "board_pins.h"

static volatile shunt_range_t      s_active_range = SHUNT_RANGE_HIGH;
static volatile shunt_range_mode_t s_range_mode   = SHUNT_MODE_AUTO;

static void write_hw_range(shunt_range_t range)
{
    switch (range) {
        case SHUNT_RANGE_LOW:
            /* Q1 OFF, Q2 OFF -> R16(50R) + R17 + R18 */
            bsp_gpio_set_range_pins(false, false);
            break;
        case SHUNT_RANGE_MID:
            /* Q1 ON (EN2=1), Q2 OFF (EN1=0) -> R17(500mR) + R18 */
            bsp_gpio_set_range_pins(false, true);
            break;
        case SHUNT_RANGE_HIGH:
        default:
            /* Q2 ON (EN1=1), Q1 OFF (EN2=0) -> R18(5mR) */
            bsp_gpio_set_range_pins(true, false);
            break;
    }
}

void drv_shunt_range_init(void)
{
    uint32_t primask = board_enter_critical();
    s_active_range = SHUNT_RANGE_HIGH;
    s_range_mode   = SHUNT_MODE_AUTO;
    write_hw_range(SHUNT_RANGE_HIGH);
    board_exit_critical(primask);
}

bool drv_shunt_range_apply(shunt_range_t range)
{
    if (range != SHUNT_RANGE_LOW && range != SHUNT_RANGE_MID && range != SHUNT_RANGE_HIGH) {
        return false;
    }

    uint32_t primask = board_enter_critical();
    write_hw_range(range);
    s_active_range = range;
    board_exit_critical(primask);
    return true;
}

shunt_range_t drv_shunt_range_get_active(void)
{
    return s_active_range;
}

bool drv_shunt_range_set_mode(shunt_range_mode_t mode)
{
    if (mode != SHUNT_MODE_AUTO &&
        mode != SHUNT_MODE_LOCK_LOW &&
        mode != SHUNT_MODE_LOCK_MID &&
        mode != SHUNT_MODE_LOCK_HIGH) {
        return false;
    }

    uint32_t primask = board_enter_critical();
    s_range_mode = mode;
    if (mode != SHUNT_MODE_AUTO) {
        shunt_range_t target = (shunt_range_t)mode;
        write_hw_range(target);
        s_active_range = target;
    }
    board_exit_critical(primask);
    return true;
}

shunt_range_mode_t drv_shunt_range_get_mode(void)
{
    return s_range_mode;
}

const char *drv_shunt_range_name(shunt_range_t range)
{
    switch (range) {
        case SHUNT_RANGE_LOW:  return "LOW_50R(1-500uA)";
        case SHUNT_RANGE_MID:  return "MID_500mR(0.5-50mA)";
        case SHUNT_RANGE_HIGH: return "HIGH_5mR(50mA-5A)";
        default:               return "UNKNOWN";
    }
}

void drv_shunt_range_emergency_high_isr(void)
{
    if (s_range_mode == SHUNT_MODE_AUTO && s_active_range != SHUNT_RANGE_HIGH) {
        write_hw_range(SHUNT_RANGE_HIGH);
        s_active_range = SHUNT_RANGE_HIGH;
    }
}
