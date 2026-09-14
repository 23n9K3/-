#include "net_tcp_raw.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "net_rx_ring.h"
#include "net_transport.h"
#include "net_transport_diag.h"
#include "net_tx_ring.h"

#define NET_TCP_POLL_INTERVAL 2U
#define NET_TCP_DRAIN_LIMIT   4U

typedef struct
{
  struct tcp_pcb *pcb;
  uint32_t generation;
} net_tcp_context_t;

static net_tcp_context_t contexts[2];
static net_tcp_context_t *active_context;
static volatile net_transport_state_t tcp_state;
static ip_addr_t requested_address;
static uint16_t requested_port;
static volatile uint32_t pending_credit;
static volatile bool connect_requested;
static volatile bool close_requested;
static volatile bool tx_kick_requested;
static volatile bool callback_pending;
static uint32_t next_generation;

static void net_tcp_bridge_callback(void *argument);
static err_t net_tcp_connected(void *argument, struct tcp_pcb *pcb, err_t error);
static err_t net_tcp_receive(void *argument, struct tcp_pcb *pcb,
                             struct pbuf *packet, err_t error);
static err_t net_tcp_sent(void *argument, struct tcp_pcb *pcb, u16_t length);
static err_t net_tcp_poll(void *argument, struct tcp_pcb *pcb);
static void net_tcp_error(void *argument, err_t error);

static bool context_is_current(const net_tcp_context_t *context,
                               const struct tcp_pcb *pcb)
{
  return (context != NULL) && (context == active_context) &&
         (context->generation == next_generation) &&
         ((pcb == NULL) || (context->pcb == pcb));
}

static void set_state(net_transport_state_t state)
{
  tcp_state = state;
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
  if (submit && (tcpip_try_callback(net_tcp_bridge_callback, NULL) != ERR_OK))
  {
    taskENTER_CRITICAL();
    callback_pending = false;
    taskEXIT_CRITICAL();
    net_transport_diag_add(NET_DIAG_TCP_CALLBACK_DROP, 1U);
  }
}

static void detach_callbacks(struct tcp_pcb *pcb)
{
  tcp_arg(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_poll(pcb, NULL, 0U);
  tcp_err(pcb, NULL);
}

static void release_connection(bool abort_connection)
{
  struct tcp_pcb *pcb;
  if (active_context == NULL) return;
  pcb = active_context->pcb;
  active_context->pcb = NULL;
  active_context = NULL;
  next_generation++;
  if (pcb != NULL)
  {
    detach_callbacks(pcb);
    if (abort_connection)
      tcp_abort(pcb);
    else if (tcp_close(pcb) != ERR_OK)
      tcp_abort(pcb);
  }
  net_rx_ring_reset();
  net_tx_ring_reset();
}

static void report_disconnected(bool error)
{
  set_state(error ? NET_TRANSPORT_STATE_ERROR :
                    NET_TRANSPORT_STATE_DISCONNECTED);
  net_transport_diag_add(NET_DIAG_TCP_DISCONNECT, 1U);
  net_transport_diag_add(NET_DIAG_DISCONNECT_WAKEUP, 1U);
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_DISCONNECTED |
    (error ? NET_TRANSPORT_EVENT_ERROR : 0U));
}

static void drain_tx(struct tcp_pcb *pcb)
{
  uint32_t iteration;
  bool freed_space = false;
  for (iteration = 0U; iteration < NET_TCP_DRAIN_LIMIT; iteration++)
  {
    const uint8_t *data;
    uint32_t available;
    uint32_t send_length;
    err_t result;
    available = net_tx_ring_contiguous(&data);
    if ((available == 0U) || (data == NULL) || (tcp_sndbuf(pcb) == 0U)) break;
    send_length = available;
    if (send_length > (uint32_t)tcp_sndbuf(pcb))
      send_length = (uint32_t)tcp_sndbuf(pcb);
    if (send_length > 0xFFFFU) send_length = 0xFFFFU;
    result = tcp_write(pcb, data, (u16_t)send_length, TCP_WRITE_FLAG_COPY);
    if (result == ERR_MEM) break;
    if (result != ERR_OK)
    {
      release_connection(true);
      report_disconnected(true);
      return;
    }
    (void)net_tx_ring_discard(send_length);
    net_transport_diag_add(NET_DIAG_TCP_TX_BYTES, send_length);
    freed_space = true;
  }
  if (freed_space)
  {
    (void)tcp_output(pcb);
    net_transport_diag_ring_water(net_rx_ring_used(), net_tx_ring_used());
    net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_TX_SPACE);
  }
}

static void start_connect(const ip_addr_t *address, uint16_t port)
{
  struct tcp_pcb *pcb;
  net_tcp_context_t *context;
  err_t result;
  if (active_context != NULL) release_connection(true);
  next_generation++;
  context = &contexts[next_generation & 1U];
  memset(context, 0, sizeof(*context));
  context->generation = next_generation;
  pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == NULL)
  {
    set_state(NET_TRANSPORT_STATE_ERROR);
    net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_ERROR);
    return;
  }
  context->pcb = pcb;
  active_context = context;
  tcp_arg(pcb, context);
  tcp_recv(pcb, net_tcp_receive);
  tcp_sent(pcb, net_tcp_sent);
  tcp_poll(pcb, net_tcp_poll, NET_TCP_POLL_INTERVAL);
  tcp_err(pcb, net_tcp_error);
  result = tcp_connect(pcb, address, port, net_tcp_connected);
  if (result != ERR_OK)
  {
    release_connection(true);
    report_disconnected(true);
  }
}

static void net_tcp_bridge_callback(void *argument)
{
  bool do_connect;
  bool do_close;
  bool do_kick;
  uint32_t credit;
  ip_addr_t address;
  uint16_t port;
  uint32_t started;
  bool retry;
  (void)argument;
  started = net_transport_diag_time_begin();
  taskENTER_CRITICAL();
  callback_pending = false;
  do_connect = connect_requested;
  do_close = close_requested;
  do_kick = tx_kick_requested;
  credit = pending_credit;
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  pending_credit = 0U;
  address = requested_address;
  port = requested_port;
  taskEXIT_CRITICAL();

  if (do_close)
  {
    release_connection(false);
    report_disconnected(false);
  }
  if (do_connect) start_connect(&address, port);
  if ((active_context != NULL) &&
      (active_context->pcb != NULL) &&
      (tcp_state == NET_TRANSPORT_STATE_CONNECTED))
  {
    if (credit != 0U)
    {
      while (credit != 0U)
      {
        u16_t portion = (credit > 0xFFFFU) ? 0xFFFFU : (u16_t)credit;
        tcp_recved(active_context->pcb, portion);
        credit -= portion;
      }
    }
    if (do_kick || (net_tx_ring_used() != 0U))
      drain_tx(active_context->pcb);
  }
  net_transport_diag_time_end(NET_RAW_TIME_BRIDGE, started);
  taskENTER_CRITICAL();
  retry = connect_requested || close_requested || tx_kick_requested ||
          (pending_credit != 0U);
  taskEXIT_CRITICAL();
  if (retry) schedule_bridge();
}

static err_t net_tcp_connected(void *argument, struct tcp_pcb *pcb, err_t error)
{
  net_tcp_context_t *context = (net_tcp_context_t *)argument;
  if (!context_is_current(context, pcb)) return ERR_ABRT;
  if (error != ERR_OK)
  {
    release_connection(true);
    report_disconnected(true);
    return error;
  }
  set_state(NET_TRANSPORT_STATE_CONNECTED);
  net_transport_diag_add(NET_DIAG_TCP_CONNECT_SUCCESS, 1U);
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_CONNECTED |
                                  NET_TRANSPORT_EVENT_TX_SPACE);
  drain_tx(pcb);
  return ERR_OK;
}

static err_t net_tcp_receive(void *argument, struct tcp_pcb *pcb,
                             struct pbuf *packet, err_t error)
{
  net_tcp_context_t *context = (net_tcp_context_t *)argument;
  struct pbuf *part;
  uint32_t started = net_transport_diag_time_begin();
  if (!context_is_current(context, pcb))
  {
    if (packet != NULL) pbuf_free(packet);
    net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
    return ERR_ABRT;
  }
  if (packet == NULL)
  {
    release_connection(false);
    report_disconnected(false);
    net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
    return ERR_OK;
  }
  if (error != ERR_OK)
  {
    pbuf_free(packet);
    release_connection(true);
    report_disconnected(true);
    net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
    return ERR_ABRT;
  }
  if ((uint32_t)packet->tot_len > NET_TCP_RX_RING_SIZE)
  {
    pbuf_free(packet);
    release_connection(true);
    report_disconnected(true);
    net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
    return ERR_ABRT;
  }
  if (net_rx_ring_free() < (uint32_t)packet->tot_len)
  {
    net_transport_diag_add(NET_DIAG_TCP_RX_BACKPRESSURE, 1U);
    net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
    return ERR_MEM;
  }
  for (part = packet; part != NULL; part = part->next)
  {
    if (!net_rx_ring_write((const uint8_t *)part->payload,
                           (uint32_t)part->len))
    {
      pbuf_free(packet);
      release_connection(true);
      report_disconnected(true);
      net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
      return ERR_ABRT;
    }
  }
  net_transport_diag_add(NET_DIAG_TCP_RX_BYTES, (uint32_t)packet->tot_len);
  pbuf_free(packet);
  net_transport_diag_ring_water(net_rx_ring_used(), net_tx_ring_used());
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_RX_READY);
  net_transport_diag_time_end(NET_RAW_TIME_TCP_RECV, started);
  return ERR_OK;
}

static err_t net_tcp_sent(void *argument, struct tcp_pcb *pcb, u16_t length)
{
  net_tcp_context_t *context = (net_tcp_context_t *)argument;
  uint32_t started = net_transport_diag_time_begin();
  (void)length;
  if (!context_is_current(context, pcb))
  {
    net_transport_diag_time_end(NET_RAW_TIME_TCP_SENT, started);
    return ERR_ABRT;
  }
  drain_tx(pcb);
  net_transport_signal_from_tcpip(NET_TRANSPORT_EVENT_TX_SPACE);
  net_transport_diag_time_end(NET_RAW_TIME_TCP_SENT, started);
  return ERR_OK;
}

static err_t net_tcp_poll(void *argument, struct tcp_pcb *pcb)
{
  net_tcp_context_t *context = (net_tcp_context_t *)argument;
  uint32_t started = net_transport_diag_time_begin();
  if (!context_is_current(context, pcb))
  {
    net_transport_diag_time_end(NET_RAW_TIME_TCP_POLL, started);
    return ERR_ABRT;
  }
  drain_tx(pcb);
  net_transport_diag_time_end(NET_RAW_TIME_TCP_POLL, started);
  return ERR_OK;
}

static void net_tcp_error(void *argument, err_t error)
{
  net_tcp_context_t *context = (net_tcp_context_t *)argument;
  (void)error;
  if (!context_is_current(context, NULL)) return;
  context->pcb = NULL;
  active_context = NULL;
  next_generation++;
  net_rx_ring_reset();
  net_tx_ring_reset();
  report_disconnected(true);
}

void net_tcp_raw_init(void)
{
  memset(contexts, 0, sizeof(contexts));
  active_context = NULL;
  tcp_state = NET_TRANSPORT_STATE_CLOSED;
  pending_credit = 0U;
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  callback_pending = false;
  next_generation = 0U;
}

net_transport_result_t net_tcp_raw_request_connect(const ip_addr_t *address,
                                                   uint16_t port)
{
  if ((address == NULL) || (port == 0U)) return NET_TRANSPORT_INVALID;
  if ((tcp_state == NET_TRANSPORT_STATE_CONNECTING) ||
      (tcp_state == NET_TRANSPORT_STATE_CONNECTED) ||
      (tcp_state == NET_TRANSPORT_STATE_CLOSING))
    return NET_TRANSPORT_WOULD_BLOCK;
  taskENTER_CRITICAL();
  requested_address = *address;
  requested_port = port;
  connect_requested = true;
  tcp_state = NET_TRANSPORT_STATE_CONNECTING;
  taskEXIT_CRITICAL();
  net_transport_diag_add(NET_DIAG_TCP_CONNECT_REQUEST, 1U);
  schedule_bridge();
  return NET_TRANSPORT_OK;
}

net_transport_result_t net_tcp_raw_request_close(void)
{
  taskENTER_CRITICAL();
  close_requested = true;
  tcp_state = NET_TRANSPORT_STATE_CLOSING;
  taskEXIT_CRITICAL();
  schedule_bridge();
  return NET_TRANSPORT_OK;
}

void net_tcp_raw_add_rx_credit(uint32_t length)
{
  taskENTER_CRITICAL();
  if ((0xFFFFFFFFUL - pending_credit) < length)
    pending_credit = 0xFFFFFFFFUL;
  else
    pending_credit += length;
  taskEXIT_CRITICAL();
  schedule_bridge();
}

void net_tcp_raw_request_tx_kick(void)
{
  taskENTER_CRITICAL();
  tx_kick_requested = true;
  taskEXIT_CRITICAL();
  schedule_bridge();
}

void net_tcp_raw_process(void)
{
  bool needed;
  taskENTER_CRITICAL();
  needed = connect_requested || close_requested || tx_kick_requested ||
           (pending_credit != 0U);
  taskEXIT_CRITICAL();
  if (needed) schedule_bridge();
}

void net_tcp_raw_network_down_from_tcpip(void)
{
  taskENTER_CRITICAL();
  connect_requested = false;
  close_requested = false;
  tx_kick_requested = false;
  pending_credit = 0U;
  callback_pending = false;
  taskEXIT_CRITICAL();
  release_connection(true);
  set_state(NET_TRANSPORT_STATE_DISCONNECTED);
  net_transport_diag_add(NET_DIAG_TCP_DISCONNECT, 1U);
}

net_transport_state_t net_tcp_raw_state(void)
{
  return tcp_state;
}
