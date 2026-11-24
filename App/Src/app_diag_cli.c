#include "app_diag_cli.h"
#include "app_main.h"
#include "bsp_gpio.h"
#include "bsp_adc_dma.h"
#include "bsp_uart.h"
#include "drv_shunt_range.h"
#include "drv_ads1220.h"
#include "svc_calib_store.h"
#include "svc_sampler.h"
#include "svc_host_proto.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLI_LINE_BUF_SIZE       128U
#define CLI_TX_BUF_SIZE         384U
#define CLI_MAX_TOKENS          8U
#define CLI_RX_POLL_BYTES       32U
#define CLI_WATCH_MIN_MS        50UL
#define CLI_WATCH_MAX_MS        5000UL
#define CLI_PREC_DEFAULT_MS     1000UL

static char     s_line_buf[CLI_LINE_BUF_SIZE];
static uint16_t s_line_len             = 0U;
static char     s_tx_scratch[CLI_TX_BUF_SIZE];
static uint32_t s_watch_period_ms      = 0UL;
static uint32_t s_last_watch_ms        = 0UL;
static bool     s_prec_report_pending  = false;

static void cli_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void cli_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(s_tx_scratch, sizeof(s_tx_scratch), fmt, args);
    va_end(args);

    if (n <= 0) {
        return;
    }
    size_t len = ((size_t)n < sizeof(s_tx_scratch)) ? (size_t)n : (sizeof(s_tx_scratch) - 1U);
    (void)bsp_uart_write(BSP_UART_DIAG, (const uint8_t *)s_tx_scratch, len);
}

static const char *range_mode_str(shunt_range_mode_t mode)
{
    switch (mode) {
        case SHUNT_MODE_AUTO:      return "AUTO";
        case SHUNT_MODE_LOCK_LOW:  return "LOCK_LOW";
        case SHUNT_MODE_LOCK_MID:  return "LOCK_MID";
        case SHUNT_MODE_LOCK_HIGH: return "LOCK_HIGH";
        default:                   return "UNKNOWN";
    }
}

static const char *ads_mode_str(ads1220_work_mode_t mode)
{
    switch (mode) {
        case ADS1220_MODE_SCAN_4CH:       return "SCAN_4CH_330SPS";
        case ADS1220_MODE_FAST_MAIN_ONLY: return "FAST_MAIN_600SPS";
        case ADS1220_MODE_PREC_20SPS_4CH: return "PREC_4CH_20SPS";
        default:                          return "UNKNOWN";
    }
}

static const char *ext_io_mode_str(bsp_ext_io_mode_t mode)
{
    switch (mode) {
        case BSP_EXT_IO_MODE_INPUT_NOPULL:   return "IN_NOPULL";
        case BSP_EXT_IO_MODE_INPUT_PULLUP:   return "IN_PU";
        case BSP_EXT_IO_MODE_INPUT_PULLDOWN: return "IN_PD";
        case BSP_EXT_IO_MODE_OUTPUT_PP:      return "OUT_PP";
        case BSP_EXT_IO_MODE_OUTPUT_OD:      return "OUT_OD";
        default:                             return "UNKNOWN";
    }
}

static bool parse_range_token(const char *tok, shunt_range_t *out_range)
{
    if (strcmp(tok, "low") == 0) {
        *out_range = SHUNT_RANGE_LOW;
        return true;
    }
    if (strcmp(tok, "mid") == 0) {
        *out_range = SHUNT_RANGE_MID;
        return true;
    }
    if (strcmp(tok, "high") == 0) {
        *out_range = SHUNT_RANGE_HIGH;
        return true;
    }
    return false;
}

static bool parse_u32_strict(const char *str, uint32_t *out_val)
{
    if (str == NULL || str[0] == '\0') {
        return false;
    }
    char *endp = NULL;
    unsigned long val = strtoul(str, &endp, 10);
    if (endp == str || *endp != '\0') {
        return false;
    }
    *out_val = (uint32_t)val;
    return true;
}

static bool parse_double_strict(const char *str, double *out_val)
{
    if (str == NULL || str[0] == '\0') {
        return false;
    }
    char *endp = NULL;
    double val = strtod(str, &endp);
    if (endp == str || *endp != '\0') {
        return false;
    }
    *out_val = val;
    return true;
}

static uint8_t tokenize_line(char *line, char *argv[CLI_MAX_TOKENS])
{
    uint8_t argc = 0U;
    char *p = line;

    while (*p != '\0' && argc < CLI_MAX_TOKENS) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

static void cmd_help(void)
{
    cli_printf("\r\n=== Power-Sampler-F411 Diag & Precision CLI (H3/USART2) ===\r\n");
    cli_printf("  help                          : Show command list\r\n");
    cli_printf("  status | snap                 : Full measurement & raw ADC snapshot\r\n");
    cli_printf("  watch <50..5000|off>          : Live periodic telemetry on H3\r\n");
    cli_printf("  range <auto|low|mid|high>     : Set auto-range or lock 50R/500mR/5mR\r\n");
    cli_printf("  vmode <scan|fast|prec20>      : Set ADS1220 24-bit ADC sampling mode\r\n");
    cli_printf("  prec [50..60000]              : Run Welford precision stats (default 1000 ms)\r\n");
    cli_printf("  cal show                      : Show all current & voltage cal coefficients\r\n");
    cli_printf("  cal zero                      : Tare zero offset of locked current range\r\n");
    cli_printf("  cal cur <low|mid|high> <uA>   : Span-calibrate locked range against DMM (uA)\r\n");
    cli_printf("  cal vol <0..3> <mV>           : Span-calibrate voltage ch 0..3 against DMM (mV)\r\n");
    cli_printf("  cal save | cal reset          : Save cal to BL24C02 EEPROM or factory-reset\r\n");
    cli_printf("  energy clear                  : Reset accumulated energy counter\r\n");
    cli_printf("  io read                       : Read H1 reserved GPIOs (0=PA8, 1=PA11, 2=PA12)\r\n");
    cli_printf("  io cfg <0..2> <in|pu|pd|pp|od> [0|1] : Configure H1 GPIO\r\n");
    cli_printf("  io set <0..2> <0|1>           : Drive H1 GPIO output\r\n");
}

static void cmd_status(void)
{
    svc_sampler_snapshot_t snap;
    svc_sampler_get_snapshot(&snap);

    cli_printf("\r\n--- Snapshot @ %lu ms ---\r\n", (unsigned long)(snap.timestamp_us / 1000ULL));
    cli_printf("  Range : Mode=%s  Active=%s  Switches=%lu\r\n",
               range_mode_str(snap.range_mode),
               drv_shunt_range_name(snap.active_range),
               (unsigned long)snap.range_switch_count);
    cli_printf("  Flags : 0x%02X [ADS=%u EEP=%u CAL=%u OVL=%u SETTLING=%u WIN_RDY=%u] VMode=%s\r\n",
               snap.status_flags,
               (snap.status_flags & SVC_STATUS_ADS1220_ONLINE) ? 1U : 0U,
               (snap.status_flags & SVC_STATUS_EEPROM_OK) ? 1U : 0U,
               (snap.status_flags & SVC_STATUS_CALIB_LOADED) ? 1U : 0U,
               (snap.status_flags & SVC_STATUS_OVERLOAD_WARN) ? 1U : 0U,
               (snap.status_flags & SVC_STATUS_RANGE_SETTLING) ? 1U : 0U,
               (snap.status_flags & SVC_STATUS_WINDOW_READY) ? 1U : 0U,
               ads_mode_str(drv_ads1220_get_mode()));
    cli_printf("  Meas  : I = %.2f uA (%.5f mA) | P = %.4f mW | E = %ld uJ\r\n",
               (double)snap.current_ua,
               (double)snap.current_ua / 1000.0,
               (double)snap.power_mw,
               (long)snap.energy_uj);
    cli_printf("  Volt  : SENS4=%.3f mV | VIN1=%.3f mV | VIN2=%.3f mV | VIN3=%.3f mV\r\n",
               (double)snap.voltage_mv[0],
               (double)snap.voltage_mv[1],
               (double)snap.voltage_mv[2],
               (double)snap.voltage_mv[3]);
    cli_printf("  ADC12 : LOW(PA6)=%u  MID(PA7)=%u  HIGH(PB0)=%u  1V5_REF(PB1)=%u\r\n",
               snap.raw_adc_low, snap.raw_adc_mid, snap.raw_adc_high, snap.raw_adc_ref);
    cli_printf("  ADS24 : CH0=%ld  CH1=%ld  CH2=%ld  CH3=%ld  (SPI_Err=%lu)\r\n",
               (long)snap.raw_ads24[0], (long)snap.raw_ads24[1],
               (long)snap.raw_ads24[2], (long)snap.raw_ads24[3],
               (unsigned long)drv_ads1220_get_error_count());
    cli_printf("  Diag  : DMA_Drop=%lu  Host_OVR=%lu  Host_CRC=%lu  Diag_OVR=%lu  H1_IO=0x%02X\r\n",
               (unsigned long)bsp_adc_dma_get_dropped_chunks(),
               (unsigned long)bsp_uart_get_rx_overrun_count(BSP_UART_HOST),
               (unsigned long)svc_host_proto_get_crc_error_count(),
               (unsigned long)bsp_uart_get_rx_overrun_count(BSP_UART_DIAG),
               bsp_ext_io_read_mask());
}

static void cmd_watch(uint8_t argc, char *argv[])
{
    if (argc < 2U) {
        cli_printf("Usage: watch <50..5000|off>\r\n");
        return;
    }
    if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
        s_watch_period_ms = 0UL;
        cli_printf("OK: watch disabled\r\n");
        return;
    }

    uint32_t ms = 0UL;
    if (!parse_u32_strict(argv[1], &ms) || ms < CLI_WATCH_MIN_MS || ms > CLI_WATCH_MAX_MS) {
        cli_printf("ERR: watch period must be %lu..%lu ms or 'off'\r\n",
                   (unsigned long)CLI_WATCH_MIN_MS, (unsigned long)CLI_WATCH_MAX_MS);
        return;
    }
    s_watch_period_ms = ms;
    cli_printf("OK: watch every %lu ms\r\n", (unsigned long)ms);
}

static void cmd_range(uint8_t argc, char *argv[])
{
    if (argc < 2U) {
        cli_printf("Usage: range <auto|low|mid|high>\r\n");
        return;
    }

    shunt_range_mode_t mode = SHUNT_MODE_AUTO;
    if (strcmp(argv[1], "auto") == 0) {
        mode = SHUNT_MODE_AUTO;
    } else if (strcmp(argv[1], "low") == 0) {
        mode = SHUNT_MODE_LOCK_LOW;
    } else if (strcmp(argv[1], "mid") == 0) {
        mode = SHUNT_MODE_LOCK_MID;
    } else if (strcmp(argv[1], "high") == 0) {
        mode = SHUNT_MODE_LOCK_HIGH;
    } else {
        cli_printf("ERR: unknown range '%s' (expected auto|low|mid|high)\r\n", argv[1]);
        return;
    }

    if (!app_main_set_range_mode(mode)) {
        cli_printf("ERR: failed to apply range mode\r\n");
        return;
    }
    cli_printf("OK: mode=%s, active=%s\r\n",
               range_mode_str(drv_shunt_range_get_mode()),
               drv_shunt_range_name(drv_shunt_range_get_active()));
}

static void cmd_vmode(uint8_t argc, char *argv[])
{
    if (argc < 2U) {
        cli_printf("Usage: vmode <scan|fast|prec20>\r\n");
        return;
    }

    ads1220_work_mode_t mode = ADS1220_MODE_SCAN_4CH;
    if (strcmp(argv[1], "scan") == 0) {
        mode = ADS1220_MODE_SCAN_4CH;
    } else if (strcmp(argv[1], "fast") == 0) {
        mode = ADS1220_MODE_FAST_MAIN_ONLY;
    } else if (strcmp(argv[1], "prec20") == 0) {
        mode = ADS1220_MODE_PREC_20SPS_4CH;
    } else {
        cli_printf("ERR: unknown vmode '%s' (expected scan|fast|prec20)\r\n", argv[1]);
        return;
    }

    if (!app_main_set_ads_mode(mode)) {
        cli_printf("ERR: failed to set ADS1220 mode\r\n");
        return;
    }
    cli_printf("OK: ADS1220 mode -> %s\r\n", ads_mode_str(mode));
}

static void cmd_prec(uint8_t argc, char *argv[])
{
    uint32_t duration_ms = CLI_PREC_DEFAULT_MS;
    if (argc >= 2U && !parse_u32_strict(argv[1], &duration_ms)) {
        cli_printf("ERR: invalid duration '%s' (50..60000 ms)\r\n", argv[1]);
        return;
    }

    svc_sampler_snapshot_t snap;
    svc_sampler_get_snapshot(&snap);
    if ((snap.status_flags & SVC_STATUS_RANGE_SETTLING) != 0U) {
        cli_printf("ERR: range gate is still settling, retry in 50 ms\r\n");
        return;
    }

    shunt_range_t active = drv_shunt_range_get_active();
    if (!svc_sampler_start_precision_eval(duration_ms, active)) {
        cli_printf("ERR: duration must be 50..60000 ms\r\n");
        return;
    }

    s_prec_report_pending = true;
    cli_printf("Running precision capture for %lu ms on %s (mode=%s)...\r\n",
               (unsigned long)duration_ms,
               drv_shunt_range_name(active),
               range_mode_str(drv_shunt_range_get_mode()));
}

static void print_precision_report(const svc_precision_report_t *rep)
{
    static const char *const s_vch_names[ADS1220_CH_COUNT] = {
        "CH0_SENS4", "CH1_VIN1 ", "CH2_VIN2 ", "CH3_VIN3 "
    };

    cli_printf("\r\n=== Precision Report (%lu ms, %s) ===\r\n",
               (unsigned long)rep->elapsed_ms,
               drv_shunt_range_name(rep->evaluated_range));
    if (rep->range_changed_during_run) {
        cli_printf("  [WARN] Shunt range switched during capture! Lock range ('range low|mid|high') for single-tier bench test.\r\n");
    }

    double cur_pkpk = rep->cur_max_ua - rep->cur_min_ua;
    cli_printf("  Current : N=%lu | Mean=%.3f uA | StdDev=%.3f uA | Pk-Pk=%.3f uA | Min=%.3f | Max=%.3f\r\n",
               (unsigned long)rep->cur_sample_count,
               rep->cur_mean_ua,
               rep->cur_stddev_ua,
               cur_pkpk,
               rep->cur_min_ua,
               rep->cur_max_ua);
    cli_printf("  ADC Raw : CurMean=%.2f LSB | 1V5_RefMean=%.2f LSB | Delta=%.2f LSB\r\n",
               rep->raw_cur_mean_lsb,
               rep->raw_ref_mean_lsb,
               rep->raw_cur_mean_lsb - rep->raw_ref_mean_lsb);

    for (uint8_t i = 0U; i < ADS1220_CH_COUNT; i++) {
        if (rep->vol_sample_count[i] == 0UL) {
            continue;
        }
        double v_pkpk = rep->vol_max_mv[i] - rep->vol_min_mv[i];
        cli_printf("  %s: N=%lu | Mean=%.3f mV | StdDev=%.3f mV | Pk-Pk=%.3f mV | Raw24=%.1f\r\n",
                   s_vch_names[i],
                   (unsigned long)rep->vol_sample_count[i],
                   rep->vol_mean_mv[i],
                   rep->vol_stddev_mv[i],
                   v_pkpk,
                   rep->raw_ads_mean_code[i]);
    }
}

static void cmd_cal_show(void)
{
    svc_calib_data_t cal;
    svc_calib_get(&cal);

    cli_printf("\r\n--- Calibration Coefficients (EEPROM=%s, NV_Loaded=%s) ---\r\n",
               app_main_is_eeprom_ok() ? "OK" : "MISSING",
               svc_calib_is_loaded_from_nv() ? "YES" : "NO(DEFAULT)");
    cli_printf("  CUR LOW  (50R)   : gain=%.6f  offset=%.3f uA\r\n",
               (double)cal.cur_gain[0], (double)cal.cur_offset_ua[0]);
    cli_printf("  CUR MID  (500mR) : gain=%.6f  offset=%.3f uA\r\n",
               (double)cal.cur_gain[1], (double)cal.cur_offset_ua[1]);
    cli_printf("  CUR HIGH (5mR)   : gain=%.6f  offset=%.3f uA\r\n",
               (double)cal.cur_gain[2], (double)cal.cur_offset_ua[2]);
    for (uint8_t i = 0U; i < SVC_CALIB_VOL_CH_COUNT; i++) {
        cli_printf("  VOL CH%u          : gain=%.6f  offset=%.3f mV\r\n",
                   i, (double)cal.vol_gain[i], (double)cal.vol_offset_mv[i]);
    }
}

static void cmd_cal(uint8_t argc, char *argv[])
{
    if (argc < 2U) {
        cli_printf("Usage: cal <show|zero|cur|vol|save|reset>\r\n");
        return;
    }

    if (strcmp(argv[1], "show") == 0) {
        cmd_cal_show();
        return;
    }

    if (strcmp(argv[1], "zero") == 0) {
        if (drv_shunt_range_get_mode() == SHUNT_MODE_AUTO) {
            cli_printf("ERR: lock target range first ('range low|mid|high') with load open before 'cal zero'\r\n");
            return;
        }
        svc_sampler_snapshot_t snap;
        svc_sampler_get_snapshot(&snap);
        if ((snap.status_flags & SVC_STATUS_WINDOW_READY) == 0U ||
            (snap.status_flags & SVC_STATUS_RANGE_SETTLING) != 0U) {
            cli_printf("ERR: wait 250 ms for post-switch window to settle\r\n");
            return;
        }
        if (!svc_sampler_tare_current_zero(snap.active_range, (double)snap.current_ua)) {
            cli_printf("ERR: zero offset out of allowed safety bounds\r\n");
            return;
        }
        svc_sampler_notify_calib_changed();
        cli_printf("OK: tared %s (prev=%.3f uA). Run 'cal save' to persist.\r\n",
                   drv_shunt_range_name(snap.active_range),
                   (double)snap.current_ua);
        return;
    }

    if (strcmp(argv[1], "cur") == 0) {
        shunt_range_t target = SHUNT_RANGE_HIGH;
        double true_ua = 0.0;
        if (argc < 4U || !parse_range_token(argv[2], &target) || !parse_double_strict(argv[3], &true_ua)) {
            cli_printf("Usage: cal cur <low|mid|high> <true_uA>\r\n");
            return;
        }
        if (drv_shunt_range_get_mode() == SHUNT_MODE_AUTO || drv_shunt_range_get_active() != target) {
            cli_printf("ERR: lock range '%s' first and wait 250 ms before calibrating span\r\n", argv[2]);
            return;
        }
        svc_sampler_snapshot_t snap;
        svc_sampler_get_snapshot(&snap);
        if ((snap.status_flags & SVC_STATUS_WINDOW_READY) == 0U) {
            cli_printf("ERR: current window not ready yet, retry in 250 ms\r\n");
            return;
        }
        if (!svc_sampler_calibrate_current_span(target, (double)snap.current_ua, true_ua)) {
            cli_printf("ERR: span ratio out of [0.70, 1.30] bounds or signal too small\r\n");
            return;
        }
        svc_sampler_notify_calib_changed();
        cli_printf("OK: calibrated %s span (meas=%.3f uA -> ref=%.3f uA). Run 'cal save' to persist.\r\n",
                   drv_shunt_range_name(target), (double)snap.current_ua, true_ua);
        return;
    }

    if (strcmp(argv[1], "vol") == 0) {
        uint32_t ch = 0UL;
        double true_mv = 0.0;
        if (argc < 4U || !parse_u32_strict(argv[2], &ch) || ch >= ADS1220_CH_COUNT ||
            !parse_double_strict(argv[3], &true_mv)) {
            cli_printf("Usage: cal vol <0..3> <true_mV>\r\n");
            return;
        }
        svc_sampler_snapshot_t snap;
        svc_sampler_get_snapshot(&snap);
        if ((snap.status_flags & SVC_STATUS_ADS1220_ONLINE) == 0U) {
            cli_printf("ERR: ADS1220 is offline\r\n");
            return;
        }
        if (!svc_sampler_calibrate_voltage_span((ads1220_channel_t)ch,
                                                (double)snap.voltage_mv[ch],
                                                true_mv)) {
            cli_printf("ERR: voltage span ratio out of [0.70, 1.30] bounds or input < 1 mV\r\n");
            return;
        }
        svc_sampler_notify_calib_changed();
        cli_printf("OK: calibrated VOL CH%lu (meas=%.3f mV -> ref=%.3f mV). Run 'cal save' to persist.\r\n",
                   (unsigned long)ch, (double)snap.voltage_mv[ch], true_mv);
        return;
    }

    if (strcmp(argv[1], "save") == 0) {
        svc_calib_data_t cal;
        svc_calib_get(&cal);
        cal.default_range_mode = (uint8_t)drv_shunt_range_get_mode();
        cal.default_ads_mode   = (uint8_t)drv_ads1220_get_mode();
        (void)svc_calib_set(&cal);

        if (!svc_calib_store_save()) {
            cli_printf("ERR: failed to write/verify BL24C02 EEPROM\r\n");
            return;
        }
        cli_printf("OK: calibration saved to dual-copy BL24C02 EEPROM\r\n");
        return;
    }

    if (strcmp(argv[1], "reset") == 0) {
        bool nv_ok = svc_calib_store_reset_defaults(app_main_is_eeprom_ok());
        svc_sampler_notify_calib_changed();
        cli_printf("OK: defaults restored (EEPROM write=%s)\r\n", nv_ok ? "OK" : "SKIPPED/ERR");
        return;
    }

    cli_printf("ERR: unknown cal subcommand '%s'\r\n", argv[1]);
}

static void cmd_io(uint8_t argc, char *argv[])
{
    static const char *const s_pin_names[BSP_EXT_IO_COUNT] = {
        "0:PA8(H1.6)", "1:PA11(H1.5)", "2:PA12(H1.4)"
    };

    if (argc < 2U || strcmp(argv[1], "read") == 0) {
        cli_printf("H1 Reserved GPIOs (mask=0x%02X):\r\n", bsp_ext_io_read_mask());
        for (uint8_t i = 0U; i < (uint8_t)BSP_EXT_IO_COUNT; i++) {
            bool high = false;
            (void)bsp_ext_io_read((bsp_ext_io_id_t)i, &high);
            cli_printf("  %s : mode=%s  level=%u\r\n",
                       s_pin_names[i],
                       ext_io_mode_str(bsp_ext_io_get_mode((bsp_ext_io_id_t)i)),
                       high ? 1U : 0U);
        }
        return;
    }

    if (strcmp(argv[1], "cfg") == 0) {
        uint32_t id = 0UL;
        if (argc < 4U || !parse_u32_strict(argv[2], &id) || id >= (uint32_t)BSP_EXT_IO_COUNT) {
            cli_printf("Usage: io cfg <0..2> <in|pu|pd|pp|od> [0|1]\r\n");
            return;
        }
        bsp_ext_io_mode_t mode = BSP_EXT_IO_MODE_INPUT_NOPULL;
        if (strcmp(argv[3], "in") == 0) {
            mode = BSP_EXT_IO_MODE_INPUT_NOPULL;
        } else if (strcmp(argv[3], "pu") == 0) {
            mode = BSP_EXT_IO_MODE_INPUT_PULLUP;
        } else if (strcmp(argv[3], "pd") == 0) {
            mode = BSP_EXT_IO_MODE_INPUT_PULLDOWN;
        } else if (strcmp(argv[3], "pp") == 0) {
            mode = BSP_EXT_IO_MODE_OUTPUT_PP;
        } else if (strcmp(argv[3], "od") == 0) {
            mode = BSP_EXT_IO_MODE_OUTPUT_OD;
        } else {
            cli_printf("ERR: invalid mode '%s' (in|pu|pd|pp|od)\r\n", argv[3]);
            return;
        }
        uint32_t init_lvl = 0UL;
        if (argc >= 5U && (!parse_u32_strict(argv[4], &init_lvl) || init_lvl > 1UL)) {
            cli_printf("ERR: initial level must be 0 or 1\r\n");
            return;
        }
        if (!bsp_ext_io_configure((bsp_ext_io_id_t)id, mode, init_lvl != 0UL)) {
            cli_printf("ERR: io cfg failed\r\n");
            return;
        }
        cli_printf("OK: %s -> %s\r\n", s_pin_names[id], ext_io_mode_str(mode));
        return;
    }

    if (strcmp(argv[1], "set") == 0) {
        uint32_t id  = 0UL;
        uint32_t lvl = 0UL;
        if (argc < 4U || !parse_u32_strict(argv[2], &id) || id >= (uint32_t)BSP_EXT_IO_COUNT ||
            !parse_u32_strict(argv[3], &lvl) || lvl > 1UL) {
            cli_printf("Usage: io set <0..2> <0|1>\r\n");
            return;
        }
        if (!bsp_ext_io_write((bsp_ext_io_id_t)id, lvl != 0UL)) {
            cli_printf("ERR: configure pin %lu as 'pp' or 'od' first\r\n", (unsigned long)id);
            return;
        }
        cli_printf("OK: %s <= %lu\r\n", s_pin_names[id], (unsigned long)lvl);
        return;
    }

    cli_printf("ERR: unknown io subcommand '%s'\r\n", argv[1]);
}

static void dispatch_line(char *line)
{
    char *argv[CLI_MAX_TOKENS];
    uint8_t argc = tokenize_line(line, argv);

    if (argc == 0U) {
        return;
    }

    if (strcmp(argv[0], "help") == 0 || strcmp(argv[0], "?") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "status") == 0 || strcmp(argv[0], "snap") == 0) {
        cmd_status();
    } else if (strcmp(argv[0], "watch") == 0) {
        cmd_watch(argc, argv);
    } else if (strcmp(argv[0], "range") == 0) {
        cmd_range(argc, argv);
    } else if (strcmp(argv[0], "vmode") == 0) {
        cmd_vmode(argc, argv);
    } else if (strcmp(argv[0], "prec") == 0) {
        cmd_prec(argc, argv);
    } else if (strcmp(argv[0], "cal") == 0) {
        cmd_cal(argc, argv);
    } else if (strcmp(argv[0], "energy") == 0 && argc >= 2U && strcmp(argv[1], "clear") == 0) {
        svc_sampler_clear_energy();
        cli_printf("OK: energy cleared\r\n");
    } else if (strcmp(argv[0], "io") == 0) {
        cmd_io(argc, argv);
    } else {
        cli_printf("ERR: unknown cmd '%s' (type 'help')\r\n", argv[0]);
    }
}

void app_diag_cli_init(void)
{
    s_line_len            = 0U;
    s_watch_period_ms     = 0UL;
    s_last_watch_ms       = 0UL;
    s_prec_report_pending = false;

    cli_printf("\r\n[Power-Sampler-F411] Ready on H3(USART2 115200). Type 'help' for 3-range precision & cal commands.\r\n> ");
}

void app_diag_cli_poll(uint32_t now_ms)
{
    uint8_t rx[CLI_RX_POLL_BYTES];
    size_t n = bsp_uart_read(BSP_UART_DIAG, rx, sizeof(rx));

    for (size_t i = 0U; i < n; i++) {
        char ch = (char)rx[i];
        if (ch == '\r' || ch == '\n') {
            if (s_line_len > 0U) {
                s_line_buf[s_line_len] = '\0';
                dispatch_line(s_line_buf);
                s_line_len = 0U;
                cli_printf("> ");
            }
        } else if (ch == '\b' || (uint8_t)ch == 0x7FU) {
            if (s_line_len > 0U) {
                s_line_len--;
            }
        } else if ((uint8_t)ch >= 0x20U && (uint8_t)ch <= 0x7EU) {
            if ((uint16_t)(s_line_len + 1U) < CLI_LINE_BUF_SIZE) {
                s_line_buf[s_line_len++] = ch;
            }
        }
    }

    if (s_prec_report_pending) {
        svc_precision_report_t rep;
        svc_sampler_get_precision_report(&rep);
        if (rep.completed) {
            s_prec_report_pending = false;
            print_precision_report(&rep);
            cli_printf("> ");
        }
    }

    if (s_watch_period_ms > 0UL && !s_prec_report_pending &&
        (now_ms - s_last_watch_ms) >= s_watch_period_ms) {
        s_last_watch_ms = now_ms;
        svc_sampler_snapshot_t snap;
        svc_sampler_get_snapshot(&snap);
        cli_printf("[%7lu ms] %-19s | I=%10.2f uA | V0=%8.2f mV V1=%7.2f V2=%7.2f V3=%7.2f | P=%8.3f mW | ADC=%4u/%4u/%4u/%4u\r\n",
                   (unsigned long)now_ms,
                   drv_shunt_range_name(snap.active_range),
                   (double)snap.current_ua,
                   (double)snap.voltage_mv[0],
                   (double)snap.voltage_mv[1],
                   (double)snap.voltage_mv[2],
                   (double)snap.voltage_mv[3],
                   (double)snap.power_mw,
                   snap.raw_adc_low,
                   snap.raw_adc_mid,
                   snap.raw_adc_high,
                   snap.raw_adc_ref);
    }
}
