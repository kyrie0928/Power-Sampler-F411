#ifndef APP_DIAG_CLI_H
#define APP_DIAG_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void app_diag_cli_init(void);
void app_diag_cli_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_DIAG_CLI_H */
