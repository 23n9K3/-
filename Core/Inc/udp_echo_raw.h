#ifndef UDP_ECHO_RAW_H
#define UDP_ECHO_RAW_H

#include <stdbool.h>

#include "lwip/err.h"

err_t udp_echo_raw_start(void);
void udp_echo_raw_stop(void);
bool udp_echo_raw_is_running(void);

#endif /* UDP_ECHO_RAW_H */
