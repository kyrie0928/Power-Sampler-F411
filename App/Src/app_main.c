#include "app_main.h"
#include "app_diag_cli.h"
#include "main.h"
#include "bsp_gpio.h"
#include "bsp_timebase.h"
#include "bsp_adc_dma.h"
#include "bsp_spi_bus.h"
#include "bsp_i2c_bus.h"
#include "bsp_uart.h"
#include "drv_eeprom_24c02.h"
#include "svc_calib_store.h"
#include "svc_sampler.h"
#include "svc_host_proto.h"

/* Fast ISR rail-saturation protection constants (12-bit ADC codes) */
#define FAST_GUARD_LOW_RAIL_LSB         12U
#define FAST_GUARD_HIGH_RAIL_LSB        4083U
#define FAST_GUARD_CONFIRM_SAMPLES      2U

/* LED heartbeat periods */
#define LED_PERIOD_NORMAL_MS            500UL
#define LED_PERIOD_FAULT_MS             100UL
#define STARTUP_ADS_GRACE_MS            250UL

/* Max bytes drained per poll iteration from Host UART */
#define HOST_UART_POLL_CHUNK_BYTES      64U

static bool     s_eeprom_ok   = false;
static uint32_t s_last_led_ms = 0UL;

/* Ultra-short integer-only ISR hook: protects R16 (50R) & R17 (500mR) against sudden load steps */
static void fast_adc_guard_isr(const uint16_t samples[BSP_ADC_CHUNK_SAMPLES][BSP_ADC_CH_COUNT])
{
    if (drv_shunt_range_get_mode() != SHUNT_MODE_AUTO) {
        return;
    }

    shunt_range_t active = drv_shunt_range_get_active();
    if (active == SHUNT_RANGE_HIGH) {
        return;
    }

    uint8_t ch_idx = (active == SHUNT_RANGE_LOW) ? (uint8_t)BSP_ADC_IDX_LOW : (uint8_t)BSP_ADC_IDX_MID;
    uint8_t rail_hits = 0U;

    for (uint8_t i = 0U; i < BSP_ADC_CHUNK_SAMPLES; i++) {
        uint16_t raw = samples[i][ch_idx];
        if (raw <= FAST_GUARD_LOW_RAIL_LSB || raw >= FAST_GUARD_HIGH_RAIL_LSB) {
            rail_hits++;
            if (rail_hits >= FAST_GUARD_CONFIRM_SAMPLES) {
                drv_shunt_range_emergency_high_isr();
                return;
            }
        }
    }
}

static bool proto_cb_set_range_mode(uint8_t mode)
{
    return app_main_set_range_mode((shunt_range_mode_t)mode);
}

static bool proto_cb_set_ads_mode(uint8_t ads_mode)
{
    return app_main_set_ads_mode((ads1220_work_mode_t)ads_mode);
}

static void proto_cb_clear_energy(void)
{
    svc_sampler_clear_energy();
}

static bool proto_cb_ext_io_cfg(uint8_t id, uint8_t mode, bool init_high)
{
    return bsp_ext_io_configure((bsp_ext_io_id_t)id, (bsp_ext_io_mode_t)mode, init_high);
}

static bool proto_cb_ext_io_write(uint8_t id, bool high)
{
    return bsp_ext_io_write((bsp_ext_io_id_t)id, high);
}

static uint8_t proto_cb_ext_io_get_mode(uint8_t id)
{
    return (uint8_t)bsp_ext_io_get_mode((bsp_ext_io_id_t)id);
}

static size_t proto_cb_uart_send(const uint8_t *data, size_t len)
{
    return bsp_uart_write(BSP_UART_HOST, data, len);
}

static const svc_calib_nv_ops_t s_calib_nv_ops = {
    .read  = drv_eeprom_read,
    .write = drv_eeprom_write,
};

static const svc_host_proto_ops_t s_host_proto_ops = {
    .get_uptime_ms    = bsp_timebase_get_ms,
    .get_snapshot     = svc_sampler_get_snapshot,
    .set_range_mode   = proto_cb_set_range_mode,
    .set_ads_mode     = proto_cb_set_ads_mode,
    .clear_energy     = proto_cb_clear_energy,
    .ext_io_cfg       = proto_cb_ext_io_cfg,
    .ext_io_write     = proto_cb_ext_io_write,
    .ext_io_read_mask = bsp_ext_io_read_mask,
    .ext_io_get_mode  = proto_cb_ext_io_get_mode,
    .uart_send        = proto_cb_uart_send,
};

bool app_main_set_range_mode(shunt_range_mode_t mode)
{
    if (!drv_shunt_range_set_mode(mode)) {
        return false;
    }
    svc_sampler_notify_manual_range_change(drv_shunt_range_get_active());
    return true;
}

bool app_main_set_ads_mode(ads1220_work_mode_t mode)
{
    return drv_ads1220_set_mode(mode);
}

bool app_main_is_eeprom_ok(void)
{
    return s_eeprom_ok;
}

void app_main_init(void)
{
    if (!bsp_timebase_init()) {
        Error_Handler();
    }

    (void)bsp_uart_init(BSP_UART_HOST, BOARD_HOST_UART_DEFAULT_BAUD);
    (void)bsp_uart_init(BSP_UART_DIAG, BOARD_DIAG_UART_DEFAULT_BAUD);

    if (bsp_i2c1_init()) {
        s_eeprom_ok = drv_eeprom_is_present();
    } else {
        s_eeprom_ok = false;
    }

    if (!bsp_spi_ads_init()) {
        Error_Handler();
    }

    drv_shunt_range_init();

    svc_calib_store_init(&s_calib_nv_ops);
    bool calib_loaded = false;
    if (s_eeprom_ok) {
        calib_loaded = svc_calib_store_load();
    }

    svc_calib_data_t cal;
    svc_calib_get(&cal);

    svc_sampler_init(drv_shunt_range_apply);
    (void)app_main_set_range_mode((shunt_range_mode_t)cal.default_range_mode);

    drv_ads1220_init((ads1220_work_mode_t)cal.default_ads_mode);
    svc_sampler_set_hw_flags(drv_ads1220_is_online(), s_eeprom_ok, calib_loaded);

    svc_host_proto_init(&s_host_proto_ops);
    app_diag_cli_init();

    if (!bsp_adc_dma_init(fast_adc_guard_isr) || !bsp_adc_dma_start()) {
        Error_Handler();
    }

    s_last_led_ms = bsp_timebase_get_ms();
}

static void poll_adc_chunks(void)
{
    bsp_adc_chunk_t chunk;

    while (bsp_adc_dma_pop_chunk(&chunk)) {
        svc_sampler_feed_adc_chunk((const uint16_t (*)[SVC_ADC_CH_NUM])chunk.samples,
                                   chunk.timestamp_us,
                                   drv_shunt_range_get_mode(),
                                   drv_shunt_range_get_active());
    }
}

static void poll_ads1220(void)
{
    ads1220_sample_t sample;

    if (drv_ads1220_poll(&sample)) {
        svc_sampler_feed_ads1220_sample(&sample);
    }
}

static void poll_host_uart(uint32_t now_ms)
{
    uint8_t rx_buf[HOST_UART_POLL_CHUNK_BYTES];
    size_t n = bsp_uart_read(BSP_UART_HOST, rx_buf, sizeof(rx_buf));

    if (n > 0U) {
        svc_host_proto_feed_rx(rx_buf, n);
    }
    svc_host_proto_poll_stream(now_ms);
}

static void poll_status_led(uint32_t now_ms)
{
    svc_sampler_snapshot_t snap;
    svc_sampler_get_snapshot(&snap);

    bool overload   = ((snap.status_flags & SVC_STATUS_OVERLOAD_WARN) != 0U);
    bool ads_fault  = (now_ms > STARTUP_ADS_GRACE_MS) && !drv_ads1220_is_online();
    uint32_t period = (overload || ads_fault) ? LED_PERIOD_FAULT_MS : LED_PERIOD_NORMAL_MS;

    if ((now_ms - s_last_led_ms) >= period) {
        s_last_led_ms = now_ms;
        bsp_led_toggle();
    }
}

void app_main_poll(void)
{
    poll_adc_chunks();
    poll_ads1220();

    svc_sampler_set_hw_flags(drv_ads1220_is_online(),
                             s_eeprom_ok,
                             svc_calib_is_loaded_from_nv());

    uint32_t now_ms = bsp_timebase_get_ms();
    poll_host_uart(now_ms);
    app_diag_cli_poll(now_ms);
    poll_status_led(now_ms);
}
