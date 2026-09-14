#include "net_transport_diag.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "net_transport_types.h"
#include "runtime_stats.h"

static net_transport_diag_t diagnostics;

void net_transport_diag_init(void)
{
  memset(&diagnostics, 0, sizeof(diagnostics));
}

void net_transport_diag_add(net_transport_diag_counter_t counter,
                            uint32_t value)
{
  taskENTER_CRITICAL();
  switch (counter)
  {
    case NET_DIAG_TCP_CONNECT_REQUEST: diagnostics.tcp_connect_requests += value; break;
    case NET_DIAG_TCP_CONNECT_SUCCESS: diagnostics.tcp_connect_success += value; break;
    case NET_DIAG_TCP_DISCONNECT: diagnostics.tcp_disconnects += value; break;
    case NET_DIAG_TCP_RX_BYTES: diagnostics.tcp_rx_bytes += value; break;
    case NET_DIAG_TCP_TX_BYTES: diagnostics.tcp_tx_bytes += value; break;
    case NET_DIAG_TCP_RX_BACKPRESSURE: diagnostics.tcp_rx_backpressure += value; break;
    case NET_DIAG_TCP_TX_WOULD_BLOCK: diagnostics.tcp_tx_would_block += value; break;
    case NET_DIAG_TCP_CALLBACK_DROP: diagnostics.tcp_callback_drop += value; break;
    case NET_DIAG_UDP_RX_DATAGRAM: diagnostics.udp_rx_datagrams += value; break;
    case NET_DIAG_UDP_TX_DATAGRAM: diagnostics.udp_tx_datagrams += value; break;
    case NET_DIAG_UDP_RX_DROP: diagnostics.udp_rx_drop += value; break;
    case NET_DIAG_UDP_TX_DROP: diagnostics.udp_tx_drop += value; break;
    case NET_DIAG_UDP_CALLBACK_DROP: diagnostics.udp_callback_drop += value; break;
    case NET_DIAG_DISCONNECT_WAKEUP: diagnostics.disconnect_wakeups += value; break;
    default: break;
  }
  taskEXIT_CRITICAL();
}

void net_transport_diag_ring_water(uint32_t rx_used, uint32_t tx_used)
{
  taskENTER_CRITICAL();
  if (rx_used > diagnostics.rx_ring_high_water)
    diagnostics.rx_ring_high_water = rx_used;
  if (tx_used > diagnostics.tx_ring_high_water)
    diagnostics.tx_ring_high_water = tx_used;
  taskEXIT_CRITICAL();
}

void net_transport_diag_udp_water(uint32_t rx_used, uint32_t tx_used)
{
  taskENTER_CRITICAL();
  if (rx_used > diagnostics.udp_rx_slots_high_water)
    diagnostics.udp_rx_slots_high_water = rx_used;
  if (tx_used > diagnostics.udp_tx_slots_high_water)
    diagnostics.udp_tx_slots_high_water = tx_used;
  taskEXIT_CRITICAL();
}

uint32_t net_transport_diag_time_begin(void)
{
  return runtime_stats_get_counter();
}

void net_transport_diag_time_end(net_raw_time_id_t id, uint32_t started)
{
  uint32_t elapsed_counts;
  uint32_t frequency;
  uint32_t elapsed_us;
  if ((uint32_t)id >= (uint32_t)NET_RAW_TIME_COUNT) return;
  elapsed_counts = runtime_stats_get_counter() - started;
  frequency = runtime_stats_get_frequency_hz();
  if (frequency == 0U) return;
  elapsed_us = (uint32_t)(((uint64_t)elapsed_counts * 1000000ULL) /
                          (uint64_t)frequency);
  taskENTER_CRITICAL();
  if (elapsed_us > diagnostics.callback_max_us[id])
    diagnostics.callback_max_us[id] = elapsed_us;
  if (elapsed_us > NET_RAW_CALLBACK_BUDGET_US)
    diagnostics.callback_budget_violations++;
  taskEXIT_CRITICAL();
}

bool net_transport_diag_get(net_transport_diag_t *snapshot)
{
  if (snapshot == NULL) return false;
  taskENTER_CRITICAL();
  *snapshot = diagnostics;
  taskEXIT_CRITICAL();
  return true;
}
