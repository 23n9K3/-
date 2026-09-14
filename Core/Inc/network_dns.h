#ifndef NETWORK_DNS_H
#define NETWORK_DNS_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/ip_addr.h"

typedef struct
{
  uint32_t generation;
  bool success;
  ip_addr_t address;
} network_dns_result_t;

void network_dns_init(void);
bool network_dns_request(const char *hostname, uint32_t generation);
void network_dns_cancel(uint32_t new_generation);
bool network_dns_take_result(network_dns_result_t *result);

#endif /* NETWORK_DNS_H */
