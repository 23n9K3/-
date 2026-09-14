#include "lwip_port.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/memp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"
#include "app_event.h"
#include "app_ipc.h"
#include "app_network_config.h"
#include "app_network_state.h"
#include "bsp_rng.h"
#include "network_diag.h"
#include "netif_enc28j60.h"
#include "netif_stats.h"
#include "platform_log.h"

static struct netif lwip_netif;
static struct dhcp lwip_dhcp;
static volatile bool start_requested;
static volatile bool core_ready;
static volatile bool desired_link_up;
static volatile bool applied_link_up;
static volatile bool link_callback_pending;
static TickType_t next_policy_poll;
static uint32_t fallback_random_state = 0x6D2B79F5UL;

static void netif_status_changed(struct netif *netif)
{
  (void)netif;
  app_network_state_process();
}

static void apply_link_callback(void *argument)
{
  bool link_up;
  (void)argument;

  taskENTER_CRITICAL();
  link_up = desired_link_up;
  link_callback_pending = false;
  taskEXIT_CRITICAL();

  if (applied_link_up != link_up)
  {
    app_network_state_set_link(link_up);
    applied_link_up = link_up;
  }
  app_network_state_process();
}

static void tcpip_ready_callback(void *argument)
{
  ip4_addr_t address;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
  (void)argument;

  ip4_addr_set_zero(&address);
  ip4_addr_set_zero(&netmask);
  ip4_addr_set_zero(&gateway);
  memset(&lwip_netif, 0, sizeof(lwip_netif));
  memset(&lwip_dhcp, 0, sizeof(lwip_dhcp));
  if (netif_add(&lwip_netif, &address, &netmask, &gateway, NULL,
                netif_enc28j60_init, tcpip_input) == NULL)
  {
    LOG_ERROR("LWIP", "netif_add failed");
    return;
  }
  netif_set_default(&lwip_netif);
  netif_set_status_callback(&lwip_netif, netif_status_changed);
  netif_set_up(&lwip_netif);
  app_network_state_init(&lwip_netif, &lwip_dhcp);
  core_ready = true;
  LOG_INFO("LWIP", "lwIP core: READY version=%s", LWIP_VERSION_STRING);
  LOG_INFO("LWIP", "tcpip_thread allocation: STATIC");
  LOG_INFO("LWIP", "netif: en0");
  LOG_INFO("LWIP", "PBUF pool: READY count=%u size=%u",
           (unsigned int)PBUF_POOL_SIZE,
           (unsigned int)PBUF_POOL_BUFSIZE);
  next_policy_poll = xTaskGetTickCount();
  (void)lwip_port_request_link(desired_link_up);
}

bool lwip_port_start(void)
{
  taskENTER_CRITICAL();
  if (start_requested)
  {
    taskEXIT_CRITICAL();
    return true;
  }
  start_requested = true;
  taskEXIT_CRITICAL();
  LOG_INFO("LWIP", "tcpip_thread create requested");
  tcpip_init(tcpip_ready_callback, NULL);
  return true;
}

bool lwip_port_request_link(bool link_up)
{
  err_t result;
  desired_link_up = link_up;
  if (!core_ready) return true;
  if (link_callback_pending || (applied_link_up == desired_link_up)) return true;
  link_callback_pending = true;
  result = tcpip_try_callback(apply_link_callback, NULL);
  if (result != ERR_OK)
  {
    link_callback_pending = false;
    netif_stats_add(NETIF_STAT_CALLBACK_DROP, 1U);
    network_diag_add(NETWORK_DIAG_CALLBACK_DROP, 1U);
    return false;
  }
  return true;
}

void lwip_port_process(void)
{
  TickType_t now;
  bool policy_due;
  if (!core_ready) return;
  now = xTaskGetTickCount();
  policy_due = ((int32_t)(now - next_policy_poll) >= 0);
  if ((desired_link_up != applied_link_up) || policy_due)
  {
    if (!link_callback_pending)
    {
      next_policy_poll = now + pdMS_TO_TICKS(APP_NETWORK_POLICY_POLL_MS);
      link_callback_pending = true;
      if (tcpip_try_callback(apply_link_callback, NULL) != ERR_OK)
      {
        link_callback_pending = false;
        netif_stats_add(NETIF_STAT_CALLBACK_DROP, 1U);
        network_diag_add(NETWORK_DIAG_CALLBACK_DROP, 1U);
      }
    }
  }
}

bool lwip_port_input_frame(const uint8_t *frame, uint16_t length)
{
  struct pbuf *p;
  err_t result;
  if ((frame == NULL) || (length == 0U) || (!core_ready))
  {
    netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    return false;
  }
  p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
  if (p == NULL)
  {
    netif_stats_add(NETIF_STAT_RX_PBUF_ALLOC_FAIL, 1U);
    netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    return false;
  }
  netif_stats_add(NETIF_STAT_RX_PBUF_ALLOC_OK, 1U);
  result = pbuf_take(p, frame, length);
  if (result != ERR_OK)
  {
    pbuf_free(p);
    netif_stats_add(NETIF_STAT_RX_INPUT_FAIL, 1U);
    netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    return false;
  }
  result = lwip_netif.input(p, &lwip_netif);
  if (result != ERR_OK)
  {
    pbuf_free(p);
    netif_stats_add(NETIF_STAT_RX_INPUT_FAIL, 1U);
    netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    return false;
  }
  netif_stats_add(NETIF_STAT_RX_INPUT_OK, 1U);
  return true;
}

bool lwip_port_is_ready(void)
{
  return core_ready;
}

void lwip_port_log_stats(void)
{
  netif_stats_t snapshot;
#if LWIP_STATS && MEMP_STATS
  uint32_t memp_used = 0U;
  uint32_t memp_max = 0U;
  uint32_t memp_errors = 0U;
  uint32_t index;
#endif
  if (!netif_stats_get(&snapshot)) return;
  LOG_INFO("LWIP", "RX=%lu TX=%lu RX_DROP=%lu TX_DROP=%lu CB_DROP=%lu",
           (unsigned long)snapshot.rx_frames,
           (unsigned long)snapshot.tx_frames,
           (unsigned long)snapshot.rx_dropped,
           (unsigned long)snapshot.tx_dropped,
           (unsigned long)snapshot.callback_drop);
#if LWIP_STATS
  LOG_INFO("LWIP", "MEM used=%lu max=%lu err=%lu PBUF_POOL used=%lu max=%lu err=%lu",
           (unsigned long)lwip_stats.mem.used,
           (unsigned long)lwip_stats.mem.max,
           (unsigned long)lwip_stats.mem.err,
           (unsigned long)lwip_stats.memp[MEMP_PBUF_POOL]->used,
           (unsigned long)lwip_stats.memp[MEMP_PBUF_POOL]->max,
           (unsigned long)lwip_stats.memp[MEMP_PBUF_POOL]->err);
  LOG_INFO("LWIP", "SYS mbox used=%lu max=%lu err=%lu",
           (unsigned long)lwip_stats.sys.mbox.used,
           (unsigned long)lwip_stats.sys.mbox.max,
           (unsigned long)lwip_stats.sys.mbox.err);
#if MEMP_STATS
  for (index = 0U; index < (uint32_t)MEMP_MAX; index++)
  {
    if (lwip_stats.memp[index] != NULL)
    {
      memp_used += lwip_stats.memp[index]->used;
      memp_max += lwip_stats.memp[index]->max;
      memp_errors += lwip_stats.memp[index]->err;
    }
  }
  LOG_INFO("LWIP", "MEMP used=%lu max=%lu err=%lu",
           (unsigned long)memp_used,
           (unsigned long)memp_max,
           (unsigned long)memp_errors);
#endif
#endif
}

uint32_t lwip_port_rand(void)
{
  uint32_t value;
  if (bsp_rng_get_u32(&value) == BSP_RNG_STATUS_OK) return value;
  fallback_random_state ^= fallback_random_state << 13U;
  fallback_random_state ^= fallback_random_state >> 17U;
  fallback_random_state ^= fallback_random_state << 5U;
  return fallback_random_state ^ (uint32_t)xTaskGetTickCount();
}

void lwip_port_diagf(const char *format, ...)
{
  char text[128];
  va_list arguments;
  if (format == NULL) return;
  va_start(arguments, format);
  (void)vsnprintf(text, sizeof(text), format, arguments);
  va_end(arguments);
  LOG_ERROR("LWIP", "%s", text);
}

void lwip_port_assert(const char *message, const char *file, int line)
{
  LOG_ERROR("LWIP", "assert: %s %s:%d",
            (message == NULL) ? "?" : message,
            (file == NULL) ? "?" : file, line);
  taskDISABLE_INTERRUPTS();
  for (;;)
  {
#if defined(__CC_ARM)
    __breakpoint(0);
#endif
  }
}
