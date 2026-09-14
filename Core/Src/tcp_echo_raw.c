#include "tcp_echo_raw.h"

#include <string.h>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "app_network_config.h"
#include "network_diag.h"
#include "platform_log.h"

#if TCP_WND > APP_TCP_ECHO_BUFFER_SIZE
#error "TCP receive window exceeds the bounded echo buffer"
#endif

typedef struct
{
  struct tcp_pcb *pcb;
  uint8_t active;
  uint8_t closing;
  uint32_t last_activity_ms;
  uint16_t read_index;
  uint16_t write_index;
  uint16_t used;
  uint8_t echo_buffer[APP_TCP_ECHO_BUFFER_SIZE];
} tcp_echo_client_t;

static struct tcp_pcb *tcp_listen_pcb;
static tcp_echo_client_t tcp_clients[APP_TCP_MAX_CLIENTS];

static uint32_t active_client_count(void)
{
  uint32_t index;
  uint32_t active = 0U;
  for (index = 0U; index < APP_TCP_MAX_CLIENTS; index++)
  {
    if (tcp_clients[index].active != 0U) active++;
  }
  return active;
}

static void update_active_count(void)
{
  network_diag_add(NETWORK_DIAG_TCP_ACTIVE, active_client_count());
}

static void client_release(tcp_echo_client_t *client)
{
  if (client == NULL) return;
  memset(client, 0, sizeof(*client));
  update_active_count();
}

static err_t client_flush(tcp_echo_client_t *client)
{
  struct tcp_pcb *pcb;
  err_t result = ERR_OK;

  if ((client == NULL) || (client->pcb == NULL)) return ERR_ARG;
  pcb = client->pcb;
  while ((client->used != 0U) && (tcp_sndbuf(pcb) != 0U))
  {
    uint16_t contiguous = (uint16_t)(APP_TCP_ECHO_BUFFER_SIZE -
                                     client->read_index);
    uint16_t length = client->used;
    u16_t available = tcp_sndbuf(pcb);
    if (length > contiguous) length = contiguous;
    if (length > available) length = available;
    result = tcp_write(pcb, &client->echo_buffer[client->read_index],
                       length, TCP_WRITE_FLAG_COPY);
    if (result == ERR_MEM)
    {
      network_diag_add(NETWORK_DIAG_TCP_WRITE_ERR_MEM, 1U);
      return result;
    }
    if (result != ERR_OK) return result;
    client->read_index = (uint16_t)((client->read_index + length) %
                                    APP_TCP_ECHO_BUFFER_SIZE);
    client->used = (uint16_t)(client->used - length);
    network_diag_add(NETWORK_DIAG_TCP_TX_BYTES, length);
  }
  if (result == ERR_OK) result = tcp_output(pcb);
  return result;
}

static err_t client_try_close(tcp_echo_client_t *client)
{
  err_t result;
  if ((client == NULL) || (client->pcb == NULL)) return ERR_OK;
  client->closing = 1U;
  if (client->used != 0U)
  {
    (void)client_flush(client);
    if (client->used != 0U) return ERR_MEM;
  }
  result = tcp_close(client->pcb);
  if (result == ERR_OK)
  {
    client->pcb = NULL;
    LOG_INFO("TCP", "TCP client closed");
    client_release(client);
  }
  else if (result == ERR_MEM)
  {
    network_diag_add(NETWORK_DIAG_TCP_WRITE_ERR_MEM, 1U);
  }
  return result;
}

static err_t tcp_echo_sent(void *argument, struct tcp_pcb *pcb, u16_t length)
{
  tcp_echo_client_t *client = (tcp_echo_client_t *)argument;
  (void)length;
  if ((client == NULL) || (client->pcb != pcb))
  {
    tcp_abort(pcb);
    network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    return ERR_ABRT;
  }
  client->last_activity_ms = sys_now();
  (void)client_flush(client);
  if (client->closing != 0U) (void)client_try_close(client);
  return ERR_OK;
}

static err_t tcp_echo_poll(void *argument, struct tcp_pcb *pcb)
{
  tcp_echo_client_t *client = (tcp_echo_client_t *)argument;
  if ((client == NULL) || (client->pcb != pcb))
  {
    tcp_abort(pcb);
    network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    return ERR_ABRT;
  }
  if ((client->closing != 0U) ||
      ((uint32_t)(sys_now() - client->last_activity_ms) >= APP_TCP_IDLE_MS))
  {
    (void)client_try_close(client);
    return ERR_OK;
  }
  (void)client_flush(client);
  return ERR_OK;
}

static void tcp_echo_error(void *argument, err_t error)
{
  tcp_echo_client_t *client = (tcp_echo_client_t *)argument;
  (void)error;
  /* lwIP has already freed the PCB before this callback. */
  if (client != NULL)
  {
    client->pcb = NULL;
    client_release(client);
  }
}

static err_t tcp_echo_receive(void *argument,
                              struct tcp_pcb *pcb,
                              struct pbuf *p,
                              err_t error)
{
  tcp_echo_client_t *client = (tcp_echo_client_t *)argument;
  uint16_t free_space;
  uint16_t copied = 0U;

  if ((client == NULL) || (client->pcb != pcb))
  {
    if (p != NULL) pbuf_free(p);
    if (pcb != NULL) tcp_abort(pcb);
    network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    return ERR_ABRT;
  }
  if (p == NULL)
  {
    client->closing = 1U;
    (void)client_try_close(client);
    return ERR_OK;
  }
  if (error != ERR_OK)
  {
    pbuf_free(p);
    client->pcb = NULL;
    client_release(client);
    tcp_abort(pcb);
    network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    return ERR_ABRT;
  }

  free_space = (uint16_t)(APP_TCP_ECHO_BUFFER_SIZE - client->used);
  if (p->tot_len > free_space)
  {
    /* Returning ERR_MEM without freeing p transfers no ownership. lwIP keeps
       it as refused_data and retries this callback when the window reopens. */
    network_diag_add(NETWORK_DIAG_TCP_WRITE_ERR_MEM, 1U);
    return ERR_MEM;
  }
  while (copied < p->tot_len)
  {
    uint16_t contiguous = (uint16_t)(APP_TCP_ECHO_BUFFER_SIZE -
                                     client->write_index);
    uint16_t remaining = (uint16_t)(p->tot_len - copied);
    uint16_t length = (remaining < contiguous) ? remaining : contiguous;
    u16_t actual = pbuf_copy_partial(p,
      &client->echo_buffer[client->write_index], length, copied);
    if (actual != length)
    {
      pbuf_free(p);
      client->pcb = NULL;
      client_release(client);
      tcp_abort(pcb);
      network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
      return ERR_ABRT;
    }
    client->write_index = (uint16_t)((client->write_index + length) %
                                     APP_TCP_ECHO_BUFFER_SIZE);
    client->used = (uint16_t)(client->used + length);
    copied = (uint16_t)(copied + length);
  }
  tcp_recved(pcb, p->tot_len);
  network_diag_add(NETWORK_DIAG_TCP_RX_BYTES, p->tot_len);
  pbuf_free(p);
  client->last_activity_ms = sys_now();
  (void)client_flush(client);
  return ERR_OK;
}

static err_t tcp_echo_accept(void *argument,
                             struct tcp_pcb *new_pcb,
                             err_t error)
{
  uint32_t index;
  tcp_echo_client_t *client = NULL;
  (void)argument;

  if ((error != ERR_OK) || (new_pcb == NULL)) return ERR_VAL;
  for (index = 0U; index < APP_TCP_MAX_CLIENTS; index++)
  {
    if (tcp_clients[index].active == 0U)
    {
      client = &tcp_clients[index];
      break;
    }
  }
  if (client == NULL)
  {
    tcp_abort(new_pcb);
    network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    return ERR_ABRT;
  }

  memset(client, 0, sizeof(*client));
  client->pcb = new_pcb;
  client->active = 1U;
  client->last_activity_ms = sys_now();
  tcp_arg(new_pcb, client);
  tcp_recv(new_pcb, tcp_echo_receive);
  tcp_sent(new_pcb, tcp_echo_sent);
  tcp_poll(new_pcb, tcp_echo_poll, 2U);
  tcp_err(new_pcb, tcp_echo_error);
  network_diag_add(NETWORK_DIAG_TCP_ACCEPT, 1U);
  update_active_count();
  LOG_INFO("TCP", "TCP client connected");
  return ERR_OK;
}

err_t tcp_echo_raw_start(void)
{
  struct tcp_pcb *pcb;
  struct tcp_pcb *listener;
  err_t result;

  if (tcp_listen_pcb != NULL) return ERR_ALREADY;
  pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == NULL) return ERR_MEM;
  result = tcp_bind(pcb, IP_ANY_TYPE, APP_TCP_ECHO_PORT);
  if (result != ERR_OK)
  {
    tcp_abort(pcb);
    return result;
  }
  listener = tcp_listen_with_backlog_and_err(
    pcb, (u8_t)APP_TCP_MAX_CLIENTS, &result);
  if (listener == NULL)
  {
    tcp_abort(pcb);
    return result;
  }
  tcp_accept(listener, tcp_echo_accept);
  tcp_listen_pcb = listener;
  return ERR_OK;
}

void tcp_echo_raw_stop(bool force_abort)
{
  uint32_t index;
  struct tcp_pcb *pcb;

  if (tcp_listen_pcb != NULL)
  {
    pcb = tcp_listen_pcb;
    tcp_listen_pcb = NULL;
    tcp_accept(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
  }
  for (index = 0U; index < APP_TCP_MAX_CLIENTS; index++)
  {
    tcp_echo_client_t *client = &tcp_clients[index];
    if ((client->active == 0U) || (client->pcb == NULL)) continue;
    if (force_abort)
    {
      pcb = client->pcb;
      tcp_arg(pcb, NULL);
      tcp_recv(pcb, NULL);
      tcp_sent(pcb, NULL);
      tcp_poll(pcb, NULL, 0U);
      tcp_err(pcb, NULL);
      client->pcb = NULL;
      client_release(client);
      tcp_abort(pcb);
      network_diag_add(NETWORK_DIAG_TCP_ABORT, 1U);
    }
    else
    {
      (void)client_try_close(client);
    }
  }
  update_active_count();
}

bool tcp_echo_raw_is_running(void)
{
  return tcp_listen_pcb != NULL;
}
