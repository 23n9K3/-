#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>

void app_main_init(void);
bool app_main_hardware_security_ready(void);
bool app_main_static_ipc_init(void);

#endif /* APP_MAIN_H */
