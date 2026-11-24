#include "svc_calib_store.h"
#include "board_pins.h"
#include <math.h>
#include <string.h>

#define CALIB_MAGIC             0x50534631UL /* 'PSF1' */
#define CALIB_VERSION           1U

#define CALIB_PRIMARY_ADDR      0x00U
#define CALIB_BACKUP_ADDR       0x80U

#define GAIN_MIN_LIMIT          0.70f
#define GAIN_MAX_LIMIT          1.30f
#define VOL_OFFSET_MAX_MV       500.0f

static const float s_cur_offset_max_ua[SVC_CALIB_CUR_RANGE_COUNT] = {
    100.0f,    /* LOW_50R:   max +/-100 uA zero trim */
    10000.0f,  /* MID_500mR: max +/-10 mA zero trim */
    1000000.0f /* HIGH_5mR:  max +/-1 A zero trim */
};

#pragma pack(push, 1)
typedef struct {
    uint32_t         magic;
    uint16_t         version;
    svc_calib_data_t payload;
    uint16_t         reserved;
    uint16_t         crc16;
} calib_nv_blob_t;
#pragma pack(pop)

/* Static compile-time check: dual copies (0x00 and 0x80) must fit inside 256-byte 24C02 */
typedef char calib_blob_size_check[(sizeof(calib_nv_blob_t) <= 120U) ? 1 : -1];

static svc_calib_nv_ops_t s_nv_ops = { NULL, NULL };
static svc_calib_data_t   s_active_calib;
static bool               s_loaded_from_nv = false;

static uint32_t calib_lock(void)
{
#if !defined(HOST_UNIT_TEST)
    return board_enter_critical();
#else
    return 0U;
#endif
}

static void calib_unlock(uint32_t state)
{
#if !defined(HOST_UNIT_TEST)
    board_exit_critical(state);
#else
    (void)state;
#endif
}

uint16_t svc_crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    if (data == NULL) {
        return crc;
    }

    for (uint16_t i = 0U; i < length; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t b = 0U; b < 8U; b++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

void svc_calib_set_defaults(svc_calib_data_t *out_cal)
{
    if (out_cal == NULL) {
        return;
    }

    memset(out_cal, 0, sizeof(*out_cal));
    for (uint8_t i = 0U; i < SVC_CALIB_CUR_RANGE_COUNT; i++) {
        out_cal->cur_gain[i]      = 1.0f;
        out_cal->cur_offset_ua[i] = 0.0f;
    }
    for (uint8_t i = 0U; i < SVC_CALIB_VOL_CH_COUNT; i++) {
        out_cal->vol_gain[i]      = 1.0f;
        out_cal->vol_offset_mv[i] = 0.0f;
    }
    out_cal->default_range_mode = 0U; /* SHUNT_MODE_AUTO */
    out_cal->default_ads_mode   = 0U; /* ADS1220_MODE_SCAN_4CH */
}

bool svc_calib_is_valid(const svc_calib_data_t *cal)
{
    if (cal == NULL) {
        return false;
    }
    if (cal->default_range_mode > 3U || cal->default_ads_mode > 2U) {
        return false;
    }

    for (uint8_t i = 0U; i < SVC_CALIB_CUR_RANGE_COUNT; i++) {
        float g   = cal->cur_gain[i];
        float off = cal->cur_offset_ua[i];
        if (!isfinite(g) || !isfinite(off) ||
            g < GAIN_MIN_LIMIT || g > GAIN_MAX_LIMIT ||
            fabsf(off) > s_cur_offset_max_ua[i]) {
            return false;
        }
    }

    for (uint8_t i = 0U; i < SVC_CALIB_VOL_CH_COUNT; i++) {
        float g   = cal->vol_gain[i];
        float off = cal->vol_offset_mv[i];
        if (!isfinite(g) || !isfinite(off) ||
            g < GAIN_MIN_LIMIT || g > GAIN_MAX_LIMIT ||
            fabsf(off) > VOL_OFFSET_MAX_MV) {
            return false;
        }
    }

    return true;
}

static bool verify_blob(const calib_nv_blob_t *blob)
{
    if (blob->magic != CALIB_MAGIC || blob->version != CALIB_VERSION) {
        return false;
    }
    uint16_t calc_crc = svc_crc16_ccitt((const uint8_t *)blob,
                                        (uint16_t)(sizeof(*blob) - sizeof(uint16_t)));
    if (calc_crc != blob->crc16) {
        return false;
    }
    return svc_calib_is_valid(&blob->payload);
}

static void pack_blob(const svc_calib_data_t *cal, calib_nv_blob_t *out_blob)
{
    memset(out_blob, 0, sizeof(*out_blob));
    out_blob->magic    = CALIB_MAGIC;
    out_blob->version  = CALIB_VERSION;
    out_blob->payload  = *cal;
    out_blob->reserved = 0U;
    out_blob->crc16    = svc_crc16_ccitt((const uint8_t *)out_blob,
                                         (uint16_t)(sizeof(*out_blob) - sizeof(uint16_t)));
}

void svc_calib_store_init(const svc_calib_nv_ops_t *nv_ops)
{
    if (nv_ops != NULL) {
        s_nv_ops = *nv_ops;
    } else {
        s_nv_ops.read  = NULL;
        s_nv_ops.write = NULL;
    }
    svc_calib_set_defaults(&s_active_calib);
    s_loaded_from_nv = false;
}

bool svc_calib_store_load(void)
{
    calib_nv_blob_t primary = {0};
    calib_nv_blob_t backup  = {0};

    if (s_nv_ops.read == NULL) {
        svc_calib_set_defaults(&s_active_calib);
        s_loaded_from_nv = false;
        return false;
    }

    bool pri_ok = s_nv_ops.read(CALIB_PRIMARY_ADDR, (uint8_t *)&primary, sizeof(primary))
               && verify_blob(&primary);
    bool bak_ok = s_nv_ops.read(CALIB_BACKUP_ADDR, (uint8_t *)&backup, sizeof(backup))
               && verify_blob(&backup);

    if (pri_ok) {
        uint32_t lock = calib_lock();
        s_active_calib = primary.payload;
        calib_unlock(lock);
        s_loaded_from_nv = true;
        if (!bak_ok && s_nv_ops.write != NULL) {
            (void)s_nv_ops.write(CALIB_BACKUP_ADDR, (const uint8_t *)&primary, sizeof(primary));
        }
        return true;
    }

    if (bak_ok) {
        uint32_t lock = calib_lock();
        s_active_calib = backup.payload;
        calib_unlock(lock);
        s_loaded_from_nv = true;
        if (s_nv_ops.write != NULL) {
            (void)s_nv_ops.write(CALIB_PRIMARY_ADDR, (const uint8_t *)&backup, sizeof(backup));
        }
        return true;
    }

    uint32_t lock = calib_lock();
    svc_calib_set_defaults(&s_active_calib);
    calib_unlock(lock);
    s_loaded_from_nv = false;
    return false;
}

bool svc_calib_store_save(void)
{
    if (s_nv_ops.write == NULL || s_nv_ops.read == NULL) {
        return false;
    }

    svc_calib_data_t snapshot;
    svc_calib_get(&snapshot);
    if (!svc_calib_is_valid(&snapshot)) {
        return false;
    }

    calib_nv_blob_t blob;
    calib_nv_blob_t verify;
    pack_blob(&snapshot, &blob);

    /* Write primary then verify readback, then update backup */
    if (!s_nv_ops.write(CALIB_PRIMARY_ADDR, (const uint8_t *)&blob, sizeof(blob))) {
        return false;
    }
    if (!s_nv_ops.read(CALIB_PRIMARY_ADDR, (uint8_t *)&verify, sizeof(verify)) ||
        memcmp(&blob, &verify, sizeof(blob)) != 0) {
        return false;
    }

    if (!s_nv_ops.write(CALIB_BACKUP_ADDR, (const uint8_t *)&blob, sizeof(blob))) {
        return false;
    }
    s_loaded_from_nv = true;
    return true;
}

bool svc_calib_store_reset_defaults(bool save_to_nv)
{
    svc_calib_data_t defaults;
    svc_calib_set_defaults(&defaults);

    uint32_t lock = calib_lock();
    s_active_calib = defaults;
    calib_unlock(lock);

    if (save_to_nv) {
        return svc_calib_store_save();
    }
    return true;
}

void svc_calib_get(svc_calib_data_t *out_cal)
{
    if (out_cal == NULL) {
        return;
    }
    uint32_t lock = calib_lock();
    *out_cal = s_active_calib;
    calib_unlock(lock);
}

bool svc_calib_set(const svc_calib_data_t *new_cal)
{
    if (!svc_calib_is_valid(new_cal)) {
        return false;
    }
    uint32_t lock = calib_lock();
    s_active_calib = *new_cal;
    calib_unlock(lock);
    return true;
}

bool svc_calib_set_current_range(uint8_t range_idx, float gain, float offset_ua)
{
    if (range_idx >= SVC_CALIB_CUR_RANGE_COUNT) {
        return false;
    }

    svc_calib_data_t copy;
    svc_calib_get(&copy);
    copy.cur_gain[range_idx]      = gain;
    copy.cur_offset_ua[range_idx] = offset_ua;
    return svc_calib_set(&copy);
}

bool svc_calib_set_voltage_ch(uint8_t ch_idx, float gain, float offset_mv)
{
    if (ch_idx >= SVC_CALIB_VOL_CH_COUNT) {
        return false;
    }

    svc_calib_data_t copy;
    svc_calib_get(&copy);
    copy.vol_gain[ch_idx]      = gain;
    copy.vol_offset_mv[ch_idx] = offset_mv;
    return svc_calib_set(&copy);
}

bool svc_calib_is_loaded_from_nv(void)
{
    return s_loaded_from_nv;
}
