#ifndef NET_TRANSPORT_DIAG_H
#define NET_TRANSPORT_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  NET_RAW_TIME_TCP_RECV = 0,
  NET_RAW_TIME_TCP_SENT,
  NET_RAW_TIME_TCP_POLL,
  NET_RAW_TIME_UDP_RECV,
  NET_RAW_TIME_BRIDGE,
  NET_RAW_TIME_COUNT
} net_raw_time_id_t;

typedef struct
{
  uint32_t tcp_connect_requests;
  uint32_t tcp_connect_success;
  uint32_t tcp_disconnects;
  uint32_t tcp_rx_bytes;
  uint32_t tcp_tx_bytes;
  uint32_t tcp_rx_backpressure;
  uint32_t tcp_tx_would_block;
  uint32_t tcp_callback_drop;
  uint32_t udp_rx_datagrams;
  uint32_t udp_tx_datagrams;
  uint32_t udp_rx_drop;
  uint32_t udp_tx_drop;
  uint32_t udp_callback_drop;
  uint32_t disconnect_wakeups;
  uint32_t callback_budget_violations;
  uint32_t callback_max_us[NET_RAW_TIME_COUNT];
  uint32_t rx_ring_high_water;
  uint32_t tx_ring_high_water;
  uint32_t udp_rx_slots_high_water;
  uint32_t udp_tx_slots_high_water;
} net_transport_diag_t;

typedef enum
{
  NET_DIAG_TCP_CONNECT_REQUEST = 0,
  NET_DIAG_TCP_CONNECT_SUCCESS,
  NET_DIAG_TCP_DISCONNECT,
  NET_DIAG_TCP_RX_BYTES,
  NET_DIAG_TCP_TX_BYTES,
  NET_DIAG_TCP_RX_BACKPRESSURE,
  NET_DIAG_TCP_TX_WOULD_BLOCK,
  NET_DIAG_TCP_CALLBACK_DROP,
  NET_DIAG_UDP_RX_DATAGRAM,
  NET_DIAG_UDP_TX_DATAGRAM,
  NET_DIAG_UDP_RX_DROP,
  NET_DIAG_UDP_TX_DROP,
  NET_DIAG_UDP_CALLBACK_DROP,
  NET_DIAG_DISCONNECT_WAKEUP
} net_transport_diag_counter_t;

void net_transport_diag_init(void);
void net_transport_diag_add(net_transport_diag_counter_t counter,
                            uint32_t value);
void net_transport_diag_ring_water(uint32_t rx_used, uint32_t tx_used);
void net_transport_diag_udp_water(uint32_t rx_used, uint32_t tx_used);
uint32_t net_transport_diag_time_begin(void);
void net_transport_diag_time_end(net_raw_time_id_t id, uint32_t started);
bool net_transport_diag_get(net_transport_diag_t *snapshot);

#endif /* NET_TRANSPORT_DIAG_H */
