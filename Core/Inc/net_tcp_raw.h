#ifndef NET_TCP_RAW_H
#define NET_TCP_RAW_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/ip_addr.h"
#include "net_transport_types.h"

void net_tcp_raw_init(void);
net_transport_result_t net_tcp_raw_request_connect(const ip_addr_t *address,
                                                   uint16_t port);
net_transport_result_t net_tcp_raw_request_close(void);
void net_tcp_raw_add_rx_credit(uint32_t length);
void net_tcp_raw_request_tx_kick(void);
void net_tcp_raw_process(void);
void net_tcp_raw_network_down_from_tcpip(void);
net_transport_state_t net_tcp_raw_state(void);

#endif /* NET_TCP_RAW_H */
