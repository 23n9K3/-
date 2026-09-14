#ifndef NETWORK_DIAG_H
#define NETWORK_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t arp_rx;
  uint32_t arp_tx;
  uint32_t icmp_echo_rx;
  uint32_t icmp_echo_tx;
  uint32_t udp_rx_packets;
  uint32_t udp_tx_packets;
  uint32_t udp_rx_bytes;
  uint32_t udp_tx_bytes;
  uint32_t udp_errors;
  uint32_t tcp_accepts;
  uint32_t tcp_active;
  uint32_t tcp_rx_bytes;
  uint32_t tcp_tx_bytes;
  uint32_t tcp_write_err_mem;
  uint32_t tcp_aborts;
  uint32_t dhcp_success;
  uint32_t dhcp_timeout;
  uint32_t static_fallback_count;
  uint32_t link_up_count;
  uint32_t link_down_count;
  uint32_t service_start_count;
  uint32_t service_stop_count;
  uint32_t callback_drop;
} network_diag_t;

typedef enum
{
  NETWORK_DIAG_ARP_RX = 0,
  NETWORK_DIAG_ARP_TX,
  NETWORK_DIAG_ICMP_ECHO_RX,
  NETWORK_DIAG_ICMP_ECHO_TX,
  NETWORK_DIAG_UDP_RX_PACKET,
  NETWORK_DIAG_UDP_TX_PACKET,
  NETWORK_DIAG_UDP_RX_BYTES,
  NETWORK_DIAG_UDP_TX_BYTES,
  NETWORK_DIAG_UDP_ERROR,
  NETWORK_DIAG_TCP_ACCEPT,
  NETWORK_DIAG_TCP_ACTIVE,
  NETWORK_DIAG_TCP_RX_BYTES,
  NETWORK_DIAG_TCP_TX_BYTES,
  NETWORK_DIAG_TCP_WRITE_ERR_MEM,
  NETWORK_DIAG_TCP_ABORT,
  NETWORK_DIAG_DHCP_SUCCESS,
  NETWORK_DIAG_DHCP_TIMEOUT,
  NETWORK_DIAG_STATIC_FALLBACK,
  NETWORK_DIAG_LINK_UP,
  NETWORK_DIAG_LINK_DOWN,
  NETWORK_DIAG_SERVICE_START,
  NETWORK_DIAG_SERVICE_STOP,
  NETWORK_DIAG_CALLBACK_DROP
} network_diag_counter_t;

void network_diag_init(void);
void network_diag_add(network_diag_counter_t counter, uint32_t value);
bool network_diag_get(network_diag_t *snapshot);
void network_diag_inspect_ethernet(const uint8_t *frame,
                                   uint16_t length,
                                   bool transmit);

#endif /* NETWORK_DIAG_H */
