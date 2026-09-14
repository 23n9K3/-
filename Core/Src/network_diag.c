#include "network_diag.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static network_diag_t diagnostics;

void network_diag_init(void)
{
  taskENTER_CRITICAL();
  memset(&diagnostics, 0, sizeof(diagnostics));
  taskEXIT_CRITICAL();
}

void network_diag_add(network_diag_counter_t counter, uint32_t value)
{
  taskENTER_CRITICAL();
  switch (counter)
  {
    case NETWORK_DIAG_ARP_RX: diagnostics.arp_rx += value; break;
    case NETWORK_DIAG_ARP_TX: diagnostics.arp_tx += value; break;
    case NETWORK_DIAG_ICMP_ECHO_RX: diagnostics.icmp_echo_rx += value; break;
    case NETWORK_DIAG_ICMP_ECHO_TX: diagnostics.icmp_echo_tx += value; break;
    case NETWORK_DIAG_UDP_RX_PACKET: diagnostics.udp_rx_packets += value; break;
    case NETWORK_DIAG_UDP_TX_PACKET: diagnostics.udp_tx_packets += value; break;
    case NETWORK_DIAG_UDP_RX_BYTES: diagnostics.udp_rx_bytes += value; break;
    case NETWORK_DIAG_UDP_TX_BYTES: diagnostics.udp_tx_bytes += value; break;
    case NETWORK_DIAG_UDP_ERROR: diagnostics.udp_errors += value; break;
    case NETWORK_DIAG_TCP_ACCEPT: diagnostics.tcp_accepts += value; break;
    case NETWORK_DIAG_TCP_ACTIVE: diagnostics.tcp_active = value; break;
    case NETWORK_DIAG_TCP_RX_BYTES: diagnostics.tcp_rx_bytes += value; break;
    case NETWORK_DIAG_TCP_TX_BYTES: diagnostics.tcp_tx_bytes += value; break;
    case NETWORK_DIAG_TCP_WRITE_ERR_MEM: diagnostics.tcp_write_err_mem += value; break;
    case NETWORK_DIAG_TCP_ABORT: diagnostics.tcp_aborts += value; break;
    case NETWORK_DIAG_DHCP_SUCCESS: diagnostics.dhcp_success += value; break;
    case NETWORK_DIAG_DHCP_TIMEOUT: diagnostics.dhcp_timeout += value; break;
    case NETWORK_DIAG_STATIC_FALLBACK: diagnostics.static_fallback_count += value; break;
    case NETWORK_DIAG_LINK_UP: diagnostics.link_up_count += value; break;
    case NETWORK_DIAG_LINK_DOWN: diagnostics.link_down_count += value; break;
    case NETWORK_DIAG_SERVICE_START: diagnostics.service_start_count += value; break;
    case NETWORK_DIAG_SERVICE_STOP: diagnostics.service_stop_count += value; break;
    case NETWORK_DIAG_CALLBACK_DROP: diagnostics.callback_drop += value; break;
    default: break;
  }
  taskEXIT_CRITICAL();
}

bool network_diag_get(network_diag_t *snapshot)
{
  if (snapshot == NULL) return false;
  taskENTER_CRITICAL();
  *snapshot = diagnostics;
  taskEXIT_CRITICAL();
  return true;
}

void network_diag_inspect_ethernet(const uint8_t *frame,
                                   uint16_t length,
                                   bool transmit)
{
  uint16_t ether_type;
  uint16_t ip_offset = 14U;
  uint8_t ihl;
  uint16_t icmp_offset;

  if ((frame == NULL) || (length < 14U)) return;
  ether_type = (uint16_t)(((uint16_t)frame[12] << 8U) | frame[13]);
  if (ether_type == 0x0806U)
  {
    network_diag_add(transmit ? NETWORK_DIAG_ARP_TX : NETWORK_DIAG_ARP_RX, 1U);
    return;
  }
  if ((ether_type != 0x0800U) || (length < 34U)) return;
  ihl = (uint8_t)((frame[ip_offset] & 0x0FU) * 4U);
  if ((ihl < 20U) || (((uint32_t)ip_offset + ihl + 1U) >= length) ||
      (frame[ip_offset + 9U] != 1U)) return;
  icmp_offset = (uint16_t)(ip_offset + ihl);
  if ((!transmit) && (frame[icmp_offset] == 8U))
  {
    network_diag_add(NETWORK_DIAG_ICMP_ECHO_RX, 1U);
  }
  else if (transmit && (frame[icmp_offset] == 0U))
  {
    network_diag_add(NETWORK_DIAG_ICMP_ECHO_TX, 1U);
  }
}
