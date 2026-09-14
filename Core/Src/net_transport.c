#include "net_transport.h"

#include "task.h"
#include "net_rx_ring.h"
#include "net_tcp_raw.h"
#include "net_transport_diag.h"
#include "net_tx_ring.h"
#include "net_udp_raw.h"
#include "platform_log.h"

static StaticEventGroup_t transport_event_storage;
static EventGroupHandle_t transport_events;
static bool initialized;

static TickType_t timeout_ticks(uint32_t timeout_ms)
{
  TickType_t ticks;
  if (timeout_ms == 0U) return 0U;
  ticks = pdMS_TO_TICKS(timeout_ms);
  return (ticks == 0U) ? 1U : ticks;
}

void net_transport_signal_from_tcpip(EventBits_t bits)
{
  if (transport_events != NULL)
    (void)xEventGroupSetBits(transport_events, bits & NET_TRANSPORT_EVENT_ALL);
}

net_transport_result_t net_transport_init(void)
{
  bool rx_pass;
  bool tx_pass;
  if (initialized) return NET_TRANSPORT_OK;
  transport_events = xEventGroupCreateStatic(&transport_event_storage);
  if (transport_events == NULL) return NET_TRANSPORT_INTERNAL_ERROR;
  net_transport_diag_init();
  net_rx_ring_init();
  net_tx_ring_init();
  rx_pass = net_rx_ring_self_test();
  tx_pass = net_tx_ring_self_test();
  net_rx_ring_reset();
  net_tx_ring_reset();
  if ((!rx_pass) || (!tx_pass)) return NET_TRANSPORT_INTERNAL_ERROR;
  net_tcp_raw_init();
  net_udp_raw_init();
  initialized = true;
  LOG_INFO("XPORT", "RX ring self-test: PASS");
  LOG_INFO("XPORT", "TX ring self-test: PASS");
  LOG_INFO("XPORT", "RAW transport bridge: READY");
  return NET_TRANSPORT_OK;
}

net_transport_result_t net_transport_connect(net_transport_type_t type,
                                             const ip_addr_t *remote_addr,
                                             uint16_t remote_port)
{
  if ((!initialized) || (remote_addr == NULL) || (remote_port == 0U))
    return NET_TRANSPORT_INVALID;
  (void)xEventGroupClearBits(transport_events, NET_TRANSPORT_EVENT_ALL);
  if (type == NET_TRANSPORT_TCP)
    return net_tcp_raw_request_connect(remote_addr, remote_port);
  if (type == NET_TRANSPORT_UDP)
    return net_udp_raw_request_connect(remote_addr, remote_port);
  return NET_TRANSPORT_INVALID;
}

net_transport_result_t net_transport_close(net_transport_type_t type)
{
  if (!initialized) return NET_TRANSPORT_INVALID;
  if (type == NET_TRANSPORT_TCP) return net_tcp_raw_request_close();
  if (type == NET_TRANSPORT_UDP) return net_udp_raw_request_close();
  return NET_TRANSPORT_INVALID;
}

EventBits_t net_transport_wait_events(EventBits_t bits, uint32_t timeout_ms)
{
  if ((!initialized) || ((bits & NET_TRANSPORT_EVENT_ALL) == 0U)) return 0U;
  return xEventGroupWaitBits(transport_events,
    bits & NET_TRANSPORT_EVENT_ALL, pdTRUE, pdFALSE, timeout_ticks(timeout_ms));
}

int32_t net_transport_read(net_transport_type_t type,
                           uint8_t *buffer,
                           uint32_t length,
                           uint32_t timeout_ms)
{
  TickType_t started;
  TickType_t wait_ticks;
  if ((!initialized) || (buffer == NULL) || (length == 0U))
    return NET_TRANSPORT_INVALID;
  if (type == NET_TRANSPORT_UDP)
    return net_transport_udp_recv_datagram(buffer, length, NULL, NULL, timeout_ms);
  if (type != NET_TRANSPORT_TCP) return NET_TRANSPORT_INVALID;
  started = xTaskGetTickCount();
  wait_ticks = timeout_ticks(timeout_ms);
  for (;;)
  {
    uint32_t copied = net_rx_ring_read(buffer, length);
    if (copied != 0U)
    {
      net_tcp_raw_add_rx_credit(copied);
      net_transport_diag_ring_water(net_rx_ring_used(), net_tx_ring_used());
      return (int32_t)copied;
    }
    if ((net_tcp_raw_state() == NET_TRANSPORT_STATE_DISCONNECTED) ||
        (net_tcp_raw_state() == NET_TRANSPORT_STATE_CLOSED) ||
        (net_tcp_raw_state() == NET_TRANSPORT_STATE_ERROR))
      return NET_TRANSPORT_DISCONNECTED;
    if (wait_ticks == 0U) return NET_TRANSPORT_WOULD_BLOCK;
    (void)xEventGroupWaitBits(transport_events,
      NET_TRANSPORT_EVENT_RX_READY | NET_TRANSPORT_EVENT_DISCONNECTED |
      NET_TRANSPORT_EVENT_ERROR, pdTRUE, pdFALSE, wait_ticks);
    {
      TickType_t elapsed = xTaskGetTickCount() - started;
      TickType_t total = timeout_ticks(timeout_ms);
      if (elapsed >= total) return NET_TRANSPORT_TIMEOUT;
      wait_ticks = total - elapsed;
    }
  }
}

int32_t net_transport_write(net_transport_type_t type,
                            const uint8_t *buffer,
                            uint32_t length,
                            uint32_t timeout_ms)
{
  TickType_t started;
  TickType_t wait_ticks;
  if ((!initialized) || (buffer == NULL) || (length == 0U))
    return NET_TRANSPORT_INVALID;
  if (type == NET_TRANSPORT_UDP)
  {
    started = xTaskGetTickCount();
    wait_ticks = timeout_ticks(timeout_ms);
    for (;;)
    {
      int32_t udp_result = net_udp_raw_queue_default(buffer, length);
      if (udp_result != NET_TRANSPORT_WOULD_BLOCK) return udp_result;
      if (wait_ticks == 0U) return NET_TRANSPORT_WOULD_BLOCK;
      (void)xEventGroupWaitBits(transport_events,
        NET_TRANSPORT_EVENT_TX_SPACE | NET_TRANSPORT_EVENT_DISCONNECTED |
        NET_TRANSPORT_EVENT_ERROR, pdTRUE, pdFALSE, wait_ticks);
      {
        TickType_t elapsed = xTaskGetTickCount() - started;
        TickType_t total = timeout_ticks(timeout_ms);
        if (elapsed >= total) return NET_TRANSPORT_TIMEOUT;
        wait_ticks = total - elapsed;
      }
    }
  }
  if (type != NET_TRANSPORT_TCP) return NET_TRANSPORT_INVALID;
  started = xTaskGetTickCount();
  wait_ticks = timeout_ticks(timeout_ms);
  for (;;)
  {
    uint32_t accepted;
    uint32_t free_space;
    if (net_tcp_raw_state() != NET_TRANSPORT_STATE_CONNECTED)
      return NET_TRANSPORT_DISCONNECTED;
    free_space = net_tx_ring_free();
    accepted = (length < free_space) ? length : free_space;
    if ((accepted != 0U) && net_tx_ring_write(buffer, accepted))
    {
      net_transport_diag_ring_water(net_rx_ring_used(), net_tx_ring_used());
      net_tcp_raw_request_tx_kick();
      return (int32_t)accepted;
    }
    net_transport_diag_add(NET_DIAG_TCP_TX_WOULD_BLOCK, 1U);
    if (wait_ticks == 0U) return NET_TRANSPORT_WOULD_BLOCK;
    (void)xEventGroupWaitBits(transport_events,
      NET_TRANSPORT_EVENT_TX_SPACE | NET_TRANSPORT_EVENT_DISCONNECTED |
      NET_TRANSPORT_EVENT_ERROR, pdTRUE, pdFALSE, wait_ticks);
    {
      TickType_t elapsed = xTaskGetTickCount() - started;
      TickType_t total = timeout_ticks(timeout_ms);
      if (elapsed >= total) return NET_TRANSPORT_TIMEOUT;
      wait_ticks = total - elapsed;
    }
  }
}

int32_t net_transport_udp_recv_datagram(uint8_t *buffer,
                                        uint32_t capacity,
                                        ip_addr_t *source_addr,
                                        uint16_t *source_port,
                                        uint32_t timeout_ms)
{
  int32_t result;
  TickType_t started;
  TickType_t wait_ticks;
  if ((!initialized) || (buffer == NULL) || (capacity == 0U))
    return NET_TRANSPORT_INVALID;
  started = xTaskGetTickCount();
  wait_ticks = timeout_ticks(timeout_ms);
  for (;;)
  {
    result = net_udp_raw_receive_datagram(buffer, capacity,
                                          source_addr, source_port);
    if (result != NET_TRANSPORT_WOULD_BLOCK) return result;
    if (net_udp_raw_state() != NET_TRANSPORT_STATE_CONNECTED)
      return NET_TRANSPORT_DISCONNECTED;
    if (wait_ticks == 0U) return NET_TRANSPORT_WOULD_BLOCK;
    (void)xEventGroupWaitBits(transport_events,
      NET_TRANSPORT_EVENT_RX_READY | NET_TRANSPORT_EVENT_DISCONNECTED |
      NET_TRANSPORT_EVENT_ERROR, pdTRUE, pdFALSE, wait_ticks);
    {
      TickType_t elapsed = xTaskGetTickCount() - started;
      TickType_t total = timeout_ticks(timeout_ms);
      if (elapsed >= total) return NET_TRANSPORT_TIMEOUT;
      wait_ticks = total - elapsed;
    }
  }
}

int32_t net_transport_udp_send_datagram(const uint8_t *buffer,
                                        uint32_t length,
                                        const ip_addr_t *destination,
                                        uint16_t destination_port,
                                        uint32_t timeout_ms)
{
  int32_t result;
  TickType_t started;
  TickType_t wait_ticks;
  if ((!initialized) || (buffer == NULL) || (destination == NULL) ||
      (length == 0U) || (length > NET_UDP_MAX_DATAGRAM) ||
      (destination_port == 0U)) return NET_TRANSPORT_INVALID;
  started = xTaskGetTickCount();
  wait_ticks = timeout_ticks(timeout_ms);
  for (;;)
  {
    result = net_udp_raw_queue_datagram(buffer, length, destination,
                                        destination_port);
    if (result != NET_TRANSPORT_WOULD_BLOCK) return result;
    if (net_udp_raw_state() != NET_TRANSPORT_STATE_CONNECTED)
      return NET_TRANSPORT_DISCONNECTED;
    if (wait_ticks == 0U) return NET_TRANSPORT_WOULD_BLOCK;
    (void)xEventGroupWaitBits(transport_events,
      NET_TRANSPORT_EVENT_TX_SPACE | NET_TRANSPORT_EVENT_DISCONNECTED |
      NET_TRANSPORT_EVENT_ERROR, pdTRUE, pdFALSE, wait_ticks);
    {
      TickType_t elapsed = xTaskGetTickCount() - started;
      TickType_t total = timeout_ticks(timeout_ms);
      if (elapsed >= total) return NET_TRANSPORT_TIMEOUT;
      wait_ticks = total - elapsed;
    }
  }
}

net_transport_state_t net_transport_get_state(net_transport_type_t type)
{
  if (type == NET_TRANSPORT_TCP) return net_tcp_raw_state();
  if (type == NET_TRANSPORT_UDP) return net_udp_raw_state();
  return NET_TRANSPORT_STATE_ERROR;
}

void net_transport_process(void)
{
  if (!initialized) return;
  net_tcp_raw_process();
  net_udp_raw_process();
}

void net_transport_on_network_down_from_tcpip(void)
{
  if (!initialized) return;
  net_tcp_raw_network_down_from_tcpip();
  net_udp_raw_network_down_from_tcpip();
  net_rx_ring_reset();
  net_tx_ring_reset();
  net_transport_diag_add(NET_DIAG_DISCONNECT_WAKEUP, 1U);
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_DISCONNECTED);
}

void net_transport_log_stats(void)
{
  net_transport_diag_t d;
  if (!net_transport_diag_get(&d)) return;
  LOG_INFO("XPORT", "TCP conn=%lu/%lu disc=%lu rx=%lu tx=%lu bp=%lu would=%lu cb_drop=%lu",
    (unsigned long)d.tcp_connect_success,
    (unsigned long)d.tcp_connect_requests,
    (unsigned long)d.tcp_disconnects,
    (unsigned long)d.tcp_rx_bytes,
    (unsigned long)d.tcp_tx_bytes,
    (unsigned long)d.tcp_rx_backpressure,
    (unsigned long)d.tcp_tx_would_block,
    (unsigned long)d.tcp_callback_drop);
  LOG_INFO("XPORT", "UDP rx=%lu drop=%lu tx=%lu drop=%lu ring=%lu/%u,%lu/%u slots=%lu/%u,%lu/%u",
    (unsigned long)d.udp_rx_datagrams, (unsigned long)d.udp_rx_drop,
    (unsigned long)d.udp_tx_datagrams, (unsigned long)d.udp_tx_drop,
    (unsigned long)d.rx_ring_high_water, (unsigned int)NET_TCP_RX_RING_SIZE,
    (unsigned long)d.tx_ring_high_water, (unsigned int)NET_TCP_TX_RING_SIZE,
    (unsigned long)d.udp_rx_slots_high_water, (unsigned int)NET_UDP_SLOT_COUNT,
    (unsigned long)d.udp_tx_slots_high_water, (unsigned int)NET_UDP_SLOT_COUNT);
  LOG_INFO("XPORT", "RAW max_us recv=%lu sent=%lu poll=%lu udp=%lu bridge=%lu budget=%lu",
    (unsigned long)d.callback_max_us[NET_RAW_TIME_TCP_RECV],
    (unsigned long)d.callback_max_us[NET_RAW_TIME_TCP_SENT],
    (unsigned long)d.callback_max_us[NET_RAW_TIME_TCP_POLL],
    (unsigned long)d.callback_max_us[NET_RAW_TIME_UDP_RECV],
    (unsigned long)d.callback_max_us[NET_RAW_TIME_BRIDGE],
    (unsigned long)d.callback_budget_violations);
}
