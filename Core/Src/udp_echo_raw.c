#include "udp_echo_raw.h"

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "app_network_config.h"
#include "network_diag.h"

static struct udp_pcb *udp_echo_pcb;

static void udp_echo_receive(void *argument,
                             struct udp_pcb *pcb,
                             struct pbuf *p,
                             const ip_addr_t *address,
                             u16_t port)
{
  err_t result;
  (void)argument;

  if (p == NULL) return;
  if ((pcb == NULL) || (address == NULL) ||
      (p->tot_len > APP_UDP_ECHO_MAX_LENGTH))
  {
    network_diag_add(NETWORK_DIAG_UDP_ERROR, 1U);
    pbuf_free(p);
    return;
  }

  network_diag_add(NETWORK_DIAG_UDP_RX_PACKET, 1U);
  network_diag_add(NETWORK_DIAG_UDP_RX_BYTES, p->tot_len);
  result = udp_sendto(pcb, p, address, port);
  if (result == ERR_OK)
  {
    network_diag_add(NETWORK_DIAG_UDP_TX_PACKET, 1U);
    network_diag_add(NETWORK_DIAG_UDP_TX_BYTES, p->tot_len);
  }
  else
  {
    network_diag_add(NETWORK_DIAG_UDP_ERROR, 1U);
  }
  pbuf_free(p);
}

err_t udp_echo_raw_start(void)
{
  err_t result;
  struct udp_pcb *pcb;

  if (udp_echo_pcb != NULL) return ERR_ALREADY;
  pcb = udp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == NULL) return ERR_MEM;
  result = udp_bind(pcb, IP_ANY_TYPE, APP_UDP_ECHO_PORT);
  if (result != ERR_OK)
  {
    udp_remove(pcb);
    return result;
  }
  udp_recv(pcb, udp_echo_receive, NULL);
  udp_echo_pcb = pcb;
  return ERR_OK;
}

void udp_echo_raw_stop(void)
{
  struct udp_pcb *pcb = udp_echo_pcb;
  if (pcb == NULL) return;
  udp_echo_pcb = NULL;
  udp_recv(pcb, NULL, NULL);
  udp_remove(pcb);
}

bool udp_echo_raw_is_running(void)
{
  return udp_echo_pcb != NULL;
}
