#include "drv_ads1220.h"
#include "bsp_spi_bus.h"
#include "bsp_timebase.h"
#include <stddef.h>

/* Official TI ADS1220 SPI Commands (SBAS501 Table 14) */
#define ADS_CMD_POWERDOWN       0x02U
#define ADS_CMD_RESET           0x06U
#define ADS_CMD_START_SYNC      0x08U
#define ADS_CMD_RDATA           0x10U
#define ADS_CMD_RREG_ALL4       0x23U /* 0x20 | (reg=0 << 2) | (count-1=3) */
#define ADS_CMD_WREG_ALL4       0x43U /* 0x40 | (reg=0 << 2) | (count-1=3) */
#define ADS_CMD_WREG_REG0       0x40U /* Write 1 byte to Reg 0 */

/* Reg 0: MUX[7:4] | GAIN[3:1]=000 (Gain=1) | PGA_BYPASS[0]=1 (mandatory for AINN=AVSS) */
#define ADS_REG0_SINGLE_ENDED(ch) ((uint8_t)(((0x08U + ((uint8_t)(ch) & 0x03U)) << 4U) | 0x01U))

/* Reg 1: DR[7:5] | MODE[4:3]=00 (Normal) | CM[2] | TS[1]=0 | BCS[0]=0 */
#define ADS_REG1_DR_20SPS       (0x00U << 5U)
#define ADS_REG1_DR_330SPS      (0x04U << 5U)
#define ADS_REG1_DR_600SPS      (0x05U << 5U)
#define ADS_REG1_CM_CONTINUOUS  0x04U
#define ADS_REG1_CM_SINGLESHOT  0x00U

/* Reg 2: VREF=00 (Internal 2.048V) | 50/60=01 (Simultaneous 50Hz & 60Hz rejection @ 20SPS) */
#define ADS_REG2_INT_2V048_50_60 0x10U
#define ADS_REG3_DEFAULT         0x00U

#define ADS_SPI_TIMEOUT_MS       5U
#define ADS_RESET_WAIT_MS        3U
#define ADS_RETRY_INTERVAL_MS    500U
#define ADS_CONV_TIMEOUT_MS      200U

typedef enum {
    ADS_FSM_RESET_SEND = 0,
    ADS_FSM_RESET_WAIT,
    ADS_FSM_CONFIG_VERIFY,
    ADS_FSM_WAIT_DRDY,
    ADS_FSM_ERROR_BACKOFF
} ads_fsm_state_t;

static ads_fsm_state_t     s_state        = ADS_FSM_RESET_SEND;
static ads1220_work_mode_t s_mode         = ADS1220_MODE_SCAN_4CH;
static ads1220_channel_t   s_active_ch    = ADS1220_CH_MAIN_SENS4;
static uint32_t            s_state_ts_ms  = 0UL;
static uint32_t            s_error_cnt    = 0UL;
static bool                s_online       = false;

static bool ads_send_cmd(uint8_t cmd)
{
    bsp_spi_ads_cs_set(true);
    bsp_timebase_delay_us(1U);
    bool ok = bsp_spi_ads_transfer(&cmd, NULL, 1U, ADS_SPI_TIMEOUT_MS);
    bsp_timebase_delay_us(1U);
    bsp_spi_ads_cs_set(false);
    return ok;
}

static bool ads_write_regs4(const uint8_t regs[4])
{
    uint8_t tx[5] = { ADS_CMD_WREG_ALL4, regs[0], regs[1], regs[2], regs[3] };

    bsp_spi_ads_cs_set(true);
    bsp_timebase_delay_us(1U);
    bool ok = bsp_spi_ads_transfer(tx, NULL, sizeof(tx), ADS_SPI_TIMEOUT_MS);
    bsp_timebase_delay_us(1U);
    bsp_spi_ads_cs_set(false);
    return ok;
}

static bool ads_read_regs4(uint8_t out_regs[4])
{
    uint8_t tx[5] = { ADS_CMD_RREG_ALL4, 0U, 0U, 0U, 0U };
    uint8_t rx[5] = { 0U };

    bsp_spi_ads_cs_set(true);
    bsp_timebase_delay_us(1U);
    bool ok = bsp_spi_ads_transfer(tx, rx, sizeof(tx), ADS_SPI_TIMEOUT_MS);
    bsp_timebase_delay_us(1U);
    bsp_spi_ads_cs_set(false);

    if (ok) {
        out_regs[0] = rx[1];
        out_regs[1] = rx[2];
        out_regs[2] = rx[3];
        out_regs[3] = rx[4];
    }
    return ok;
}

static bool ads_switch_mux_and_start(ads1220_channel_t ch)
{
    uint8_t tx[3] = {
        ADS_CMD_WREG_REG0,
        ADS_REG0_SINGLE_ENDED(ch),
        ADS_CMD_START_SYNC
    };

    bsp_spi_ads_clear_drdy_event();
    bsp_spi_ads_cs_set(true);
    bsp_timebase_delay_us(1U);
    bool ok = bsp_spi_ads_transfer(tx, NULL, sizeof(tx), ADS_SPI_TIMEOUT_MS);
    bsp_timebase_delay_us(1U);
    bsp_spi_ads_cs_set(false);
    return ok;
}

static bool ads_read_conversion24(int32_t *out_code24)
{
    uint8_t tx[4] = { ADS_CMD_RDATA, 0U, 0U, 0U };
    uint8_t rx[4] = { 0U };

    bsp_spi_ads_cs_set(true);
    bsp_timebase_delay_us(1U);
    bool ok = bsp_spi_ads_transfer(tx, rx, sizeof(tx), ADS_SPI_TIMEOUT_MS);
    bsp_timebase_delay_us(1U);
    bsp_spi_ads_cs_set(false);
    if (!ok) {
        return false;
    }

    int32_t raw = ((int32_t)rx[1] << 16) | ((int32_t)rx[2] << 8) | (int32_t)rx[3];
    if ((raw & 0x00800000L) != 0L) {
        raw |= (int32_t)0xFF000000UL;
    }
    *out_code24 = raw;
    return true;
}

static void build_mode_regs(ads1220_work_mode_t mode, ads1220_channel_t ch, uint8_t regs[4])
{
    regs[0] = ADS_REG0_SINGLE_ENDED(ch);
    switch (mode) {
        case ADS1220_MODE_FAST_MAIN_ONLY:
            regs[1] = (uint8_t)(ADS_REG1_DR_600SPS | ADS_REG1_CM_CONTINUOUS);
            break;
        case ADS1220_MODE_PREC_20SPS_4CH:
            regs[1] = (uint8_t)(ADS_REG1_DR_20SPS | ADS_REG1_CM_SINGLESHOT);
            break;
        case ADS1220_MODE_SCAN_4CH:
        default:
            regs[1] = (uint8_t)(ADS_REG1_DR_330SPS | ADS_REG1_CM_SINGLESHOT);
            break;
    }
    regs[2] = ADS_REG2_INT_2V048_50_60;
    regs[3] = ADS_REG3_DEFAULT;
}

void drv_ads1220_init(ads1220_work_mode_t initial_mode)
{
    s_mode        = initial_mode;
    s_active_ch   = ADS1220_CH_MAIN_SENS4;
    s_state       = ADS_FSM_RESET_SEND;
    s_state_ts_ms = bsp_timebase_get_ms();
    s_online      = false;
    s_error_cnt   = 0UL;
}

bool drv_ads1220_set_mode(ads1220_work_mode_t mode)
{
    if (mode != ADS1220_MODE_SCAN_4CH &&
        mode != ADS1220_MODE_FAST_MAIN_ONLY &&
        mode != ADS1220_MODE_PREC_20SPS_4CH) {
        return false;
    }

    s_mode        = mode;
    s_active_ch   = ADS1220_CH_MAIN_SENS4;
    s_state       = ADS_FSM_CONFIG_VERIFY;
    s_state_ts_ms = bsp_timebase_get_ms();
    return true;
}

ads1220_work_mode_t drv_ads1220_get_mode(void)
{
    return s_mode;
}

bool drv_ads1220_poll(ads1220_sample_t *out_sample)
{
    uint32_t now_ms = bsp_timebase_get_ms();

    switch (s_state) {
        case ADS_FSM_RESET_SEND:
            s_online = false;
            if (!ads_send_cmd(ADS_CMD_RESET)) {
                s_error_cnt++;
                s_state       = ADS_FSM_ERROR_BACKOFF;
                s_state_ts_ms = now_ms;
                return false;
            }
            s_state       = ADS_FSM_RESET_WAIT;
            s_state_ts_ms = now_ms;
            return false;

        case ADS_FSM_RESET_WAIT:
            if ((now_ms - s_state_ts_ms) >= ADS_RESET_WAIT_MS) {
                s_state = ADS_FSM_CONFIG_VERIFY;
            }
            return false;

        case ADS_FSM_CONFIG_VERIFY: {
            uint8_t cfg[4];
            uint8_t rbk[4] = {0};

            s_active_ch = ADS1220_CH_MAIN_SENS4;
            build_mode_regs(s_mode, s_active_ch, cfg);

            if (!ads_write_regs4(cfg) || !ads_read_regs4(rbk) ||
                rbk[0] != cfg[0] || rbk[1] != cfg[1] || rbk[2] != cfg[2] || rbk[3] != cfg[3]) {
                s_error_cnt++;
                s_online      = false;
                s_state       = ADS_FSM_ERROR_BACKOFF;
                s_state_ts_ms = now_ms;
                return false;
            }

            bsp_spi_ads_clear_drdy_event();
            if (!ads_send_cmd(ADS_CMD_START_SYNC)) {
                s_error_cnt++;
                s_online      = false;
                s_state       = ADS_FSM_ERROR_BACKOFF;
                s_state_ts_ms = now_ms;
                return false;
            }

            s_online      = true;
            s_state       = ADS_FSM_WAIT_DRDY;
            s_state_ts_ms = now_ms;
            return false;
        }

        case ADS_FSM_WAIT_DRDY: {
            uint64_t sample_us = 0ULL;
            bool drdy_ready = bsp_spi_ads_consume_drdy_event(&sample_us);

            if (!drdy_ready && bsp_spi_ads_drdy_is_low()) {
                sample_us  = bsp_timebase_get_us();
                drdy_ready = true;
            }

            if (!drdy_ready) {
                if ((now_ms - s_state_ts_ms) >= ADS_CONV_TIMEOUT_MS) {
                    s_error_cnt++;
                    s_online      = false;
                    s_state       = ADS_FSM_RESET_SEND;
                    s_state_ts_ms = now_ms;
                }
                return false;
            }

            int32_t code24 = 0;
            if (!ads_read_conversion24(&code24)) {
                s_error_cnt++;
                s_online      = false;
                s_state       = ADS_FSM_ERROR_BACKOFF;
                s_state_ts_ms = now_ms;
                return false;
            }

            ads1220_channel_t completed_ch = s_active_ch;

            if (s_mode != ADS1220_MODE_FAST_MAIN_ONLY) {
                s_active_ch = (ads1220_channel_t)(((uint8_t)s_active_ch + 1U) % ADS1220_CH_COUNT);
                if (!ads_switch_mux_and_start(s_active_ch)) {
                    s_error_cnt++;
                    s_online      = false;
                    s_state       = ADS_FSM_ERROR_BACKOFF;
                    s_state_ts_ms = now_ms;
                    return false;
                }
            }

            s_state_ts_ms = now_ms;
            if (out_sample != NULL) {
                out_sample->channel      = completed_ch;
                out_sample->raw_code24   = code24;
                out_sample->timestamp_us = sample_us;
            }
            return true;
        }

        case ADS_FSM_ERROR_BACKOFF:
        default:
            if ((now_ms - s_state_ts_ms) >= ADS_RETRY_INTERVAL_MS) {
                s_state       = ADS_FSM_RESET_SEND;
                s_state_ts_ms = now_ms;
            }
            return false;
    }
}

bool drv_ads1220_is_online(void)
{
    return s_online;
}

uint32_t drv_ads1220_get_error_count(void)
{
    return s_error_cnt;
}
