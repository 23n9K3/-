#include "net_udp_raw.h"

#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/tcpip.h"
#include "net_transport.h"
#include "net_transport_diag.h"

typedef struct
{
  uint8_t data[NET_UDP_MAX_DATAGRAM];
  ip_addr_t address;
  uint16_t length;
  uint16_t port;
  volatile uint8_t state;
} net_udp_slot_t;

#define UDP_SLOT_FREE      0U
#define UDP_SLOT_RESERVED  1U
#define UDP_SLOT_READY     2U
#define UDP_SLOT_CONSUMING 3U

static struct udp_pcb *udp_pcb;
static net_udp_slot_t rx_slots[NET_UDP_SLOT_COUNT];
static net_udp_slot_t tx_slots[NET_UDP_SLOT_COUNT];
static volatile net_transport_state_t udp_state;
static ip_addr_t default_address;
static uint16_t default_port;
static volatile bool connect_requested;
static volatile bool close_requested;
static volatile bool tx_kick_requested;
static volatile bool callback_pending;

static void net_udp_bridge_callback(void *argument);

static uint32_t slot_count(const net_udp_slot_t *slots)
{
  uint32_t index;
  uint32_t count = 0U;
  for (index = 0U; index < NET_UDP_SLOT_COUNT; index++)
  {
    if (slots[index].state != UDP_SLOT_FREE) count++;
  }
  return count;
}

static void reset_slots(net_udp_slot_t *slots)
{
  uint32_t index;
  bool scheduler_running =
    xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
  if (scheduler_running) taskENTER_CRITICAL();
  for (index = 0U; index < NET_UDP_SLOT_COUNT; index++)
  {
    slots[index].length = 0U;
    slots[index].port = 0U;
    slots[index].state = UDP_SLOT_FREE;
  }
  if (scheduler_running) taskEXIT_CRITICAL();
}

static int32_t reserve_free_slot(net_udp_slot_t *slots)
{
  uint32_t index;
  int32_t found = -1;
  taskENTER_CRITICAL();
  for (index = 0U; index < NET_UDP_SLOT_COUNT; index++)
  {
    if (slots[index].state == UDP_SLOT_FREE)
    {
      slots[index].state = UDP_SLOT_RESERVED;
      found = (int32_t)index;
      break;
    }
  }
  taskEXIT_CRITICAL();
  return found;
}

static int32_t claim_ready_slot(net_udp_slot_t *slots)
{
  uint32_t index;
  int32_t found = -1;
  taskENTER_CRITICAL();
  for (index = 0U; index < NET_UDP_SLOT_COUNT; index++)
  {
    if (slots[index].state == UDP_SLOT_READY)
    {
      slots[index].state = UDP_SLOT_CONSUMING;
      found = (int32_t)index;
      break;
    }
  }
  taskEXIT_CRITICAL();
  return found;
}

static void publish_slot(net_udp_slot_t *slot)
{
  __DMB();
  slot->state = UDP_SLOT_READY;
}

static void release_slot(net_udp_slot_t *slot)
{
  slot->length = 0U;
  __DMB();
  slot->state = UDP_SLOT_FREE;
}

static void schedule_bridge(void)
{
  bool submit = false;
  taskENTER_CRITICAL();
  if (!callback_pending)
  {
    callback_pending = true;
    submit = true;
  }
  taskEXIT_CRITICAL();
  if (submit && (tcpip_try_callback(net_udp_bridge_callback, NULL) != ERR_OK))
  {
    taskENTER_CRITICAL();
    callback_pending = false;
    taskEXIT_CRITICAL();
    net_transport_diag_add(NET_DIAG_UDP_CALLBACK_DROP, 1U);
  }
}

static void udp_receive_callback(void *argument, struct udp_pcb *pcb,
                                 struct pbuf *packet,
                                 const ip_addr_t *address, u16_t port)
{
  int32_t slot_index;
  net_udp_slot_t *slot;
  uint32_t started = net_transport_diag_time_begin();
  (void)argument;
  if ((pcb != udp_pcb) || (packet == NULL) || (address == NULL) ||
      (packet->tot_len == 0U) ||
      ((uint32_t)packet->tot_len > NET_UDP_MAX_DATAGRAM))
  {
    if (packet != NULL) pbuf_free(packet);
    net_transport_diag_add(NET_DIAG_UDP_RX_DROP, 1U);
    net_transport_diag_time_end(NET_RAW_TIME_UDP_RECV, started);
    return;
  }
  slot_index = reserve_free_slot(rx_slots);
  if (slot_index < 0)
  {
    pbuf_free(packet);
    net_transport_diag_add(NET_DIAG_UDP_RX_DROP, 1U);
    net_transport_diag_time_end(NET_RAW_TIME_UDP_RECV, started);
    return;
  }
  slot = &rx_slots[(uint32_t)slot_index];
  slot->length = packet->tot_len;
  slot->address = *address;
  slot->port = port;
  if (pbuf_copy_partial(packet, slot->data, packet->tot_len, 0U) !=
      packet->tot_len)
  {
    release_slot(slot);
    pbuf_free(packet);
    net_transport_diag_add(NET_DIAG_UDP_RX_DROP, 1U);
    net_transport_diag_time_end(NET_RAW_TIME_UDP_RECV, started);
    return;
  }
  publish_slot(slot);
  pbuf_free(packet);
  net_transport_diag_add(NET_DIAG_UDP_RX_DATAGRAM, 1U);
  net_transport_diag_udp_water(slot_count(rx_slots), slot_count(tx_slots));
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_RX_READY);
  net_transport_diag_time_end(NET_RAW_TIME_UDP_RECV, started);
}

static bool drain_one_tx(void)
{
  int32_t slot_index;
  net_udp_slot_t *slot;
  struct pbuf *packet;
  err_t result;
  slot_index = claim_ready_slot(tx_slots);
  if (slot_index < 0) return false;
  slot = &tx_slots[(uint32_t)slot_index];
  packet = pbuf_alloc(PBUF_TRANSPORT, slot->length, PBUF_RAM);
  if (packet == NULL)
  {
    publish_slot(slot);
    taskENTER_CRITICAL();
    tx_kick_requested = true;
    taskEXIT_CRITICAL();
    return false;
  }
  if (pbuf_take(packet, slot->data, slot->length) != ERR_OK)
  {
    pbuf_free(packet);
    release_slot(slot);
    net_transport_diag_add(NET_DIAG_UDP_TX_DROP, 1U);
    net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_TX_SPACE |
                                    NET_TRANSPORT_EVENT_ERROR);
    return true;
  }
  result = udp_sendto(udp_pcb, packet, &slot->address, slot->port);
  pbuf_free(packet);
  release_slot(slot);
  if (result == ERR_OK)
    net_transport_diag_add(NET_DIAG_UDP_TX_DATAGRAM, 1U);
  else
    net_transport_diag_add(NET_DIAG_UDP_TX_DROP, 1U);
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_TX_SPACE |
    ((result == ERR_OK) ? 0U : NET_TRANSPORT_EVENT_ERROR));
  return true;
}

static void net_udp_bridge_callback(void *argument)
{
  bool do_connect;
  bool do_close;
  bool do_kick;
  ip_addr_t address;
  uint16_t port;
  uint32_t index;
  uint32_t started;
  bool retry;
  (void)argument;
  started = net_transport_diag_time_begin();
  taskENTER_CRITICAL();
  callback_pending = false;
  do_connect = connect_requested;
  do_close = close_requested;
  do_kick = tx_kick_requested;
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  address = default_address;
  port = default_port;
  taskEXIT_CRITICAL();
  if (do_close)
  {
    if (udp_pcb != NULL)
    {
      udp_recv(udp_pcb, NULL, NULL);
      udp_remove(udp_pcb);
      udp_pcb = NULL;
    }
    reset_slots(rx_slots);
    reset_slots(tx_slots);
    udp_state = NET_TRANSPORT_STATE_DISCONNECTED;
    net_transport_diag_add(NET_DIAG_DISCONNECT_WAKEUP, 1U);
    net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_DISCONNECTED);
  }
  if (do_connect)
  {
    if (udp_pcb != NULL)
    {
      udp_recv(udp_pcb, NULL, NULL);
      udp_remove(udp_pcb);
    }
    udp_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if ((udp_pcb == NULL) ||
        (udp_bind(udp_pcb, IP_ANY_TYPE, 0U) != ERR_OK))
    {
      if (udp_pcb != NULL) udp_remove(udp_pcb);
      udp_pcb = NULL;
      udp_state = NET_TRANSPORT_STATE_ERROR;
      net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_ERROR);
    }
    else
    {
      default_address = address;
      default_port = port;
      udp_recv(udp_pcb, udp_receive_callback, NULL);
      udp_state = NET_TRANSPORT_STATE_CONNECTED;
      net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_CONNECTED |
                                      NET_TRANSPORT_EVENT_TX_SPACE);
    }
  }
  if (do_kick && (udp_pcb != NULL) &&
      (udp_state == NET_TRANSPORT_STATE_CONNECTED))
  {
    for (index = 0U; index < NET_UDP_SLOT_COUNT; index++)
    {
      if (!drain_one_tx()) break;
    }
    net_transport_diag_udp_water(slot_count(rx_slots), slot_count(tx_slots));
  }
  net_transport_diag_time_end(NET_RAW_TIME_BRIDGE, started);
  taskENTER_CRITICAL();
  retry = connect_requested || close_requested || tx_kick_requested;
  taskEXIT_CRITICAL();
  if (retry) schedule_bridge();
}

void net_udp_raw_init(void)
{
  udp_pcb = NULL;
  udp_state = NET_TRANSPORT_STATE_CLOSED;
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  callback_pending = false;
  default_port = 0U;
  ip_addr_set_zero(&default_address);
  reset_slots(rx_slots);
  reset_slots(tx_slots);
}

net_transport_result_t net_udp_raw_request_connect(const ip_addr_t *address,
                                                   uint16_t port)
{
  if ((address == NULL) || (port == 0U)) return NET_TRANSPORT_INVALID;
  if ((udp_state == NET_TRANSPORT_STATE_CONNECTING) ||
      (udp_state == NET_TRANSPORT_STATE_CONNECTED) ||
      (udp_state == NET_TRANSPORT_STATE_CLOSING))
    return NET_TRANSPORT_WOULD_BLOCK;
  taskENTER_CRITICAL();
  default_address = *address;
  default_port = port;
  connect_requested = true;
  udp_state = NET_TRANSPORT_STATE_CONNECTING;
  taskEXIT_CRITICAL();
  schedule_bridge();
  return NET_TRANSPORT_OK;
}

net_transport_result_t net_udp_raw_request_close(void)
{
  taskENTER_CRITICAL();
  close_requested = true;
  udp_state = NET_TRANSPORT_STATE_CLOSING;
  taskEXIT_CRITICAL();
  schedule_bridge();
  return NET_TRANSPORT_OK;
}

int32_t net_udp_raw_receive_datagram(uint8_t *buffer,
                                     uint32_t capacity,
                                     ip_addr_t *source_addr,
                                     uint16_t *source_port)
{
  int32_t slot_index;
  net_udp_slot_t *slot;
  uint16_t length;
  if ((buffer == NULL) || (capacity == 0U)) return NET_TRANSPORT_INVALID;
  slot_index = claim_ready_slot(rx_slots);
  if (slot_index < 0) return NET_TRANSPORT_WOULD_BLOCK;
  slot = &rx_slots[(uint32_t)slot_index];
  length = slot->length;
  if (capacity < (uint32_t)length)
  {
    publish_slot(slot);
    return NET_TRANSPORT_NO_SPACE;
  }
  memcpy(buffer, slot->data, length);
  if (source_addr != NULL) *source_addr = slot->address;
  if (source_port != NULL) *source_port = slot->port;
  release_slot(slot);
  net_transport_diag_udp_water(slot_count(rx_slots), slot_count(tx_slots));
  return (int32_t)length;
}

int32_t net_udp_raw_queue_datagram(const uint8_t *buffer,
                                   uint32_t length,
                                   const ip_addr_t *destination,
                                   uint16_t destination_port)
{
  int32_t slot_index;
  net_udp_slot_t *slot;
  if ((buffer == NULL) || (destination == NULL) || (length == 0U) ||
      (length > NET_UDP_MAX_DATAGRAM) || (destination_port == 0U))
    return NET_TRANSPORT_INVALID;
  if (udp_state != NET_TRANSPORT_STATE_CONNECTED)
    return NET_TRANSPORT_DISCONNECTED;
  slot_index = reserve_free_slot(tx_slots);
  if (slot_index < 0) return NET_TRANSPORT_WOULD_BLOCK;
  slot = &tx_slots[(uint32_t)slot_index];
  memcpy(slot->data, buffer, length);
  slot->length = (uint16_t)length;
  slot->address = *destination;
  slot->port = destination_port;
  publish_slot(slot);
  net_transport_diag_udp_water(slot_count(rx_slots), slot_count(tx_slots));
  taskENTER_CRITICAL();
  tx_kick_requested = true;
  taskEXIT_CRITICAL();
  schedule_bridge();
  return (int32_t)length;
}

int32_t net_udp_raw_queue_default(const uint8_t *buffer, uint32_t length)
{
  ip_addr_t address;
  uint16_t port;
  taskENTER_CRITICAL();
  address = default_address;
  port = default_port;
  taskEXIT_CRITICAL();
  return net_udp_raw_queue_datagram(buffer, length, &address, port);
}

void net_udp_raw_process(void)
{
  bool needed;
  taskENTER_CRITICAL();
  needed = connect_requested || close_requested || tx_kick_requested;
  taskEXIT_CRITICAL();
  if (needed) schedule_bridge();
}

void net_udp_raw_network_down_from_tcpip(void)
{
  taskENTER_CRITICAL();
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  callback_pending = false;
  taskEXIT_CRITICAL();
  if (udp_pcb != NULL)
  {
    udp_recv(udp_pcb, NULL, NULL);
    udp_remove(udp_pcb);
    udp_pcb = NULL;
  }
  reset_slots(rx_slots);
  reset_slots(tx_slots);
  udp_state = NET_TRANSPORT_STATE_DISCONNECTED;
}

net_transport_state_t net_udp_raw_state(void)
{
  return udp_state;
}
