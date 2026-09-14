#ifndef TCP_ECHO_RAW_H
#define TCP_ECHO_RAW_H

#include <stdbool.h>

#include "lwip/err.h"

err_t tcp_echo_raw_start(void);
void tcp_echo_raw_stop(bool force_abort);
bool tcp_echo_raw_is_running(void);

#endif /* TCP_ECHO_RAW_H */
