#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "lwip/ip_addr.h"
#include "net_transport_types.h"

#define NET_TRANSPORT_EVENT_CONNECTED    ((EventBits_t)(1UL << 0))
#define NET_TRANSPORT_EVENT_RX_READY     ((EventBits_t)(1UL << 1))
#define NET_TRANSPORT_EVENT_TX_SPACE     ((EventBits_t)(1UL << 2))
#define NET_TRANSPORT_EVENT_DISCONNECTED ((EventBits_t)(1UL << 3))
#define NET_TRANSPORT_EVENT_ERROR        ((EventBits_t)(1UL << 4))
#define NET_TRANSPORT_EVENT_ALL          ((EventBits_t)0x1FUL)

net_transport_result_t net_transport_init(void);
net_transport_result_t net_transport_connect(net_transport_type_t type,
                                             const ip_addr_t *remote_addr,
                                             uint16_t remote_port);
net_transport_result_t net_transport_close(net_transport_type_t type);
int32_t net_transport_read(net_transport_type_t type,
                           uint8_t *buffer,
                           uint32_t length,
                           uint32_t timeout_ms);
int32_t net_transport_write(net_transport_type_t type,
                            const uint8_t *buffer,
                            uint32_t length,
                            uint32_t timeout_ms);
int32_t net_transport_udp_recv_datagram(uint8_t *buffer,
                                        uint32_t capacity,
                                        ip_addr_t *source_addr,
                                        uint16_t *source_port,
                                        uint32_t timeout_ms);
int32_t net_transport_udp_send_datagram(const uint8_t *buffer,
                                        uint32_t length,
                                        const ip_addr_t *destination,
                                        uint16_t destination_port,
                                        uint32_t timeout_ms);
net_transport_state_t net_transport_get_state(net_transport_type_t type);
EventBits_t net_transport_wait_events(EventBits_t bits,
                                      uint32_t timeout_ms);
void net_transport_process(void);
void net_transport_on_network_down_from_tcpip(void);
void net_transport_signal_from_tcpip(EventBits_t bits);
void net_transport_log_stats(void);

#endif /* NET_TRANSPORT_H */
