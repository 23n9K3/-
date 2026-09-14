#include "app_network_state.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/sys.h"
#include "app_event.h"
#include "app_ipc.h"
#include "app_network_config.h"
#include "app_tasks.h"
#include "network_diag.h"
#include "network_services.h"
#include "net_transport.h"
#include "platform_log.h"
#include "time_sync_task.h"

static struct netif *managed_netif;
static struct dhcp *managed_dhcp;
static volatile app_net_state_t network_state = APP_NET_STATE_DOWN;
static bool physical_link_up;
static uint32_t dhcp_started_ms;

const char *app_network_state_text(app_net_state_t state)
{
  switch (state)
  {
    case APP_NET_STATE_DOWN: return "DOWN";
    case APP_NET_STATE_LINK_UP: return "LINK_UP";
    case APP_NET_STATE_STATIC_CONFIG: return "STATIC_CONFIG";
    case APP_NET_STATE_DHCP_START: return "DHCP_START";
    case APP_NET_STATE_DHCP_WAIT: return "DHCP_WAIT";
    case APP_NET_STATE_IP_READY: return "IP_READY";
    case APP_NET_STATE_SERVICES_READY: return "SERVICES_READY";
    case APP_NET_STATE_RECOVERING: return "RECOVERING";
    default: return "INVALID";
  }
}

static void set_state(app_net_state_t state)
{
  if (network_state != state)
  {
    network_state = state;
    LOG_INFO("NET", "Network state: %s", app_network_state_text(state));
  }
}

static void log_ipv4(const char *label, const ip4_addr_t *address)
{
  uint32_t host = lwip_ntohl(ip4_addr_get_u32(address));
  LOG_INFO("NET", "%s: %lu.%lu.%lu.%lu", label,
           (unsigned long)((host >> 24U) & 0xFFU),
           (unsigned long)((host >> 16U) & 0xFFU),
           (unsigned long)((host >> 8U) & 0xFFU),
           (unsigned long)(host & 0xFFU));
}

static void publish_ip_ready(void)
{
  app_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = APP_EVENT_NET_IP_READY;
  event.source = APP_EVENT_SOURCE_NETWORK;
  event.tick = xTaskGetTickCount();
  event.payload.network.address = ip4_addr_get_u32(netif_ip4_addr(managed_netif));
  event.payload.network.netmask = ip4_addr_get_u32(netif_ip4_netmask(managed_netif));
  event.payload.network.gateway = ip4_addr_get_u32(netif_ip4_gw(managed_netif));
  (void)app_event_publish(&event, 0U);
}

static void configure_static(bool fallback)
{
  ip4_addr_t address;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
  ip_addr_t dns_primary;
  ip_addr_t dns_secondary;
  set_state(APP_NET_STATE_STATIC_CONFIG);
  IP4_ADDR(&address, APP_STATIC_IP0, APP_STATIC_IP1,
           APP_STATIC_IP2, APP_STATIC_IP3);
  IP4_ADDR(&netmask, APP_NETMASK0, APP_NETMASK1,
           APP_NETMASK2, APP_NETMASK3);
  IP4_ADDR(&gateway, APP_GATEWAY0, APP_GATEWAY1,
           APP_GATEWAY2, APP_GATEWAY3);
  netif_set_addr(managed_netif, &address, &netmask, &gateway);
  IP_ADDR4(&dns_primary, APP_DNS_PRIMARY0, APP_DNS_PRIMARY1,
           APP_DNS_PRIMARY2, APP_DNS_PRIMARY3);
  IP_ADDR4(&dns_secondary, APP_DNS_SECONDARY0, APP_DNS_SECONDARY1,
           APP_DNS_SECONDARY2, APP_DNS_SECONDARY3);
  dns_setserver(0U, &dns_primary);
  dns_setserver(1U, &dns_secondary);
  netif_set_up(managed_netif);
  LOG_INFO("NET", "Address source: %s", fallback ?
           "STATIC_FALLBACK" : "STATIC");
  log_ipv4("IP", &address);
  log_ipv4("Mask", &netmask);
  log_ipv4("Gateway", &gateway);
  if (fallback) network_diag_add(NETWORK_DIAG_STATIC_FALLBACK, 1U);
  set_state(APP_NET_STATE_IP_READY);
  publish_ip_ready();
}

static void start_services(void)
{
  err_t result;
  if (network_services_are_running())
  {
    set_state(APP_NET_STATE_SERVICES_READY);
    time_sync_task_network_state_from_tcpip(true);
    return;
  }
  result = network_services_start();
  if (result == ERR_OK)
  {
    LOG_INFO("NET", "ICMP service: READY");
    set_state(APP_NET_STATE_SERVICES_READY);
    (void)app_tasks_notify_security(SECURITY_NOTIFY_START);
    time_sync_task_network_state_from_tcpip(true);
  }
  else
  {
    LOG_ERROR("NET", "Network service start failed: %d", (int)result);
  }
}

static void recover_link_down(void)
{
  if (network_state == APP_NET_STATE_DOWN) return;
  set_state(APP_NET_STATE_RECOVERING);
  net_transport_on_network_down_from_tcpip();
  time_sync_task_network_state_from_tcpip(false);
  network_services_stop(true);
  dhcp_stop(managed_netif);
  netif_set_link_down(managed_netif);
  netif_set_addr(managed_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
  (void)app_state_clear_bits(APP_STATE_BIT_NET_LINK_UP |
                             APP_STATE_BIT_IP_READY |
                             APP_STATE_BIT_SERVICES_READY);
  (void)app_tasks_notify_security(SECURITY_NOTIFY_STOP);
  network_diag_add(NETWORK_DIAG_LINK_DOWN, 1U);
  set_state(APP_NET_STATE_DOWN);
}

void app_network_state_init(struct netif *netif, struct dhcp *dhcp)
{
  managed_netif = netif;
  managed_dhcp = dhcp;
  physical_link_up = false;
  dhcp_started_ms = 0U;
  network_state = APP_NET_STATE_DOWN;
  network_diag_init();
  if ((managed_netif != NULL) && (managed_dhcp != NULL))
  {
    dhcp_set_struct(managed_netif, managed_dhcp);
  }
  LOG_INFO("NET", "Network mode: %s",
           (APP_NET_MODE == APP_NET_MODE_STATIC) ?
           "STATIC" : "DHCP_WITH_STATIC_FALLBACK");
}

void app_network_state_set_link(bool link_up)
{
  if ((managed_netif == NULL) || (managed_dhcp == NULL)) return;
  physical_link_up = link_up;
  if (!link_up)
  {
    recover_link_down();
    return;
  }
  if (network_state != APP_NET_STATE_DOWN) return;
  netif_set_link_up(managed_netif);
  netif_set_up(managed_netif);
  (void)app_state_set_bits(APP_STATE_BIT_NET_LINK_UP);
  network_diag_add(NETWORK_DIAG_LINK_UP, 1U);
  set_state(APP_NET_STATE_LINK_UP);
  if (APP_NET_MODE == APP_NET_MODE_STATIC)
  {
    configure_static(false);
  }
  else
  {
    set_state(APP_NET_STATE_DHCP_START);
    netif_set_addr(managed_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
    dhcp_stop(managed_netif);
    if (dhcp_start(managed_netif) == ERR_OK)
    {
      dhcp_started_ms = sys_now();
      LOG_INFO("NET", "DHCP: START");
      set_state(APP_NET_STATE_DHCP_WAIT);
    }
    else
    {
      LOG_ERROR("NET", "DHCP start failed; applying fallback");
      configure_static(true);
    }
  }
  app_network_state_process();
}

void app_network_state_process(void)
{
  if ((managed_netif == NULL) || (!physical_link_up)) return;
  if ((network_state == APP_NET_STATE_DHCP_WAIT) &&
      dhcp_supplied_address(managed_netif))
  {
    network_diag_add(NETWORK_DIAG_DHCP_SUCCESS, 1U);
    LOG_INFO("NET", "DHCP: BOUND");
    LOG_INFO("NET", "Address source: DHCP");
    log_ipv4("IP", netif_ip4_addr(managed_netif));
    log_ipv4("Mask", netif_ip4_netmask(managed_netif));
    log_ipv4("Gateway", netif_ip4_gw(managed_netif));
    set_state(APP_NET_STATE_IP_READY);
    publish_ip_ready();
  }
  else if ((network_state == APP_NET_STATE_DHCP_WAIT) &&
           ((uint32_t)(sys_now() - dhcp_started_ms) >= APP_DHCP_TIMEOUT_MS))
  {
    LOG_WARN("NET", "DHCP: TIMEOUT");
    network_diag_add(NETWORK_DIAG_DHCP_TIMEOUT, 1U);
    dhcp_stop(managed_netif);
    configure_static(true);
  }
  if (network_state == APP_NET_STATE_IP_READY) start_services();
}

app_net_state_t app_network_state_get(void)
{
  return network_state;
}
