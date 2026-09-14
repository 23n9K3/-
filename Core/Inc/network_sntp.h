#ifndef NETWORK_SNTP_H
#define NETWORK_SNTP_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/ip_addr.h"

typedef struct
{
  uint64_t epoch;
  uint32_t microseconds;
  uint32_t generation;
} network_time_sample_t;

bool network_sntp_init(void);
bool network_sntp_request_start(const ip_addr_t *address,
                                uint32_t generation);
bool network_sntp_request_stop(void);
void network_sntp_stop_from_tcpip(void);
bool network_sntp_take_sample(network_time_sample_t *sample);
void network_sntp_lwip_time_received(uint32_t seconds,
                                     uint32_t microseconds);

#endif /* NETWORK_SNTP_H */
