#ifndef NET_UDP_RAW_H
#define NET_UDP_RAW_H

#include <stdint.h>

#include "lwip/ip_addr.h"
#include "net_transport_types.h"

void net_udp_raw_init(void);
net_transport_result_t net_udp_raw_request_connect(const ip_addr_t *address,
                                                   uint16_t port);
net_transport_result_t net_udp_raw_request_close(void);
int32_t net_udp_raw_receive_datagram(uint8_t *buffer,
                                     uint32_t capacity,
                                     ip_addr_t *source_addr,
                                     uint16_t *source_port);
int32_t net_udp_raw_queue_datagram(const uint8_t *buffer,
                                   uint32_t length,
                                   const ip_addr_t *destination,
                                   uint16_t destination_port);
int32_t net_udp_raw_queue_default(const uint8_t *buffer, uint32_t length);
void net_udp_raw_process(void);
void net_udp_raw_network_down_from_tcpip(void);
net_transport_state_t net_udp_raw_state(void);

#endif /* NET_UDP_RAW_H */
