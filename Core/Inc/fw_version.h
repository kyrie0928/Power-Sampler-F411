#ifndef FW_VERSION_H
#define FW_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Firmware Version (Single Source of Truth)
 * Update ONLY these 3 numbers when cutting a new firmware release.
 * ========================================================================= */
#define FW_VERSION_MAJOR        0
#define FW_VERSION_MINOR        0
#define FW_VERSION_PATCH        1

#define FW_VER_STR_HELPER(x)    #x
#define FW_VER_STR(x)           FW_VER_STR_HELPER(x)

#define FW_VERSION_STRING       FW_VER_STR(FW_VERSION_MAJOR) "." \
                                FW_VER_STR(FW_VERSION_MINOR) "." \
                                FW_VER_STR(FW_VERSION_PATCH)

#define FW_BUILD_DATE           __DATE__
#define FW_BUILD_TIME           __TIME__

#ifdef __cplusplus
}
#endif

#endif /* FW_VERSION_H */
