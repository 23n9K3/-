#ifndef NETWORK_SERVICES_H
#define NETWORK_SERVICES_H

#include <stdbool.h>

#include "lwip/err.h"

err_t network_services_start(void);
void network_services_stop(bool force_abort);
bool network_services_are_running(void);

#endif /* NETWORK_SERVICES_H */
