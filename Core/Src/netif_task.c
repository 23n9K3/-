#include "netif_task.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_event.h"
#include "app_ipc.h"
#include "app_tasks.h"
#include "bsp_enc28j60.h"
#include "enc28j60_port.h"
#include "lwip_port.h"
#include "netif_enc28j60.h"
#include "netif_stats.h"
#include "network_diag.h"
#include "platform_log.h"
#include "project_config.h"

#if defined(__CC_ARM)
__align(4) static uint8_t rx_frame[ETH_MAX_FRAME_SIZE];
#else
static uint8_t rx_frame[ETH_MAX_FRAME_SIZE] __attribute__((aligned(4)));
#endif

static bool initialized;
static bool stable_link_up;
static bool candidate_link_up;
static uint8_t candidate_count;
static TickType_t next_link_poll;
static bool broadcast_test_seen;

static bool is_broadcast_test_frame(const uint8_t *frame, uint16_t length)
{
  uint32_t index;
  if ((frame == NULL) || (length < 14U) ||
      (frame[12] != 0x88U) || (frame[13] != 0xB5U))
  {
    return false;
  }
  for (index = 0U; index < 6U; index++)
  {
    if (frame[index] != 0xFFU) return false;
  }
  return true;
}

static void publish_network_event(app_event_type_t type)
{
  app_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  event.source = APP_EVENT_SOURCE_NETWORK;
  event.tick = xTaskGetTickCount();
  (void)app_event_publish(&event, 0U);
}

static void poll_link_state(void)
{
  bool link_up;
  if (ENC28J60_GetLinkState(&link_up) != ENC28J60_OK)
  {
    netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    return;
  }
  if (candidate_link_up != link_up)
  {
    candidate_link_up = link_up;
    candidate_count = 1U;
    return;
  }
  if (candidate_count < 2U) candidate_count++;
  if ((candidate_count >= 2U) && (stable_link_up != candidate_link_up))
  {
    stable_link_up = candidate_link_up;
    ENC28J60_RecordLinkChange(stable_link_up);
    if (stable_link_up)
    {
      netif_stats_add(NETIF_STAT_LINK_UP_COUNT, 1U);
      publish_network_event(APP_EVENT_NET_LINK_UP);
      LOG_INFO("ENC", "PHY Link: UP");
    }
    else
    {
      netif_stats_add(NETIF_STAT_LINK_DOWN_COUNT, 1U);
      publish_network_event(APP_EVENT_NET_LINK_DOWN);
      LOG_INFO("ENC", "PHY Link: DOWN");
    }
  }
}

bool netif_task_initialize(void)
{
  uint8_t revision;
  uint8_t mac[6];
  bool link_up;
  enc28j60_status_t status;

  initialized = false;
  broadcast_test_seen = false;
  netif_stats_init();
  if (!netif_enc28j60_transport_init())
  {
    LOG_ERROR("NETIF", "static TX queue creation failed");
    return false;
  }
  LOG_INFO("ENC", "ENC28J60 driver start");
  LOG_INFO("ENC", "SPI1 clock: %lu Hz",
           (unsigned long)enc28j60_port_get_spi_clock_hz());
  status = ENC28J60_Init();
  if (status != ENC28J60_OK)
  {
    LOG_ERROR("ENC", "initialization failed: %s",
              ENC28J60_StatusText(status));
    return false;
  }
  if (ENC28J60_ReadRevision(&revision) != ENC28J60_OK)
  {
    LOG_ERROR("ENC", "revision read failed");
    return false;
  }
  ENC28J60_GetMacAddress(mac);
  if (!netif_enc28j60_set_mac(mac))
  {
    LOG_ERROR("NETIF", "invalid locally administered MAC");
    return false;
  }
  LOG_INFO("ENC", "ENC28J60: READY revision=0x%02X", (unsigned int)revision);
  LOG_INFO("ENC", "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned int)mac[0], (unsigned int)mac[1],
           (unsigned int)mac[2], (unsigned int)mac[3],
           (unsigned int)mac[4], (unsigned int)mac[5]);
  if (ENC28J60_GetLinkState(&link_up) != ENC28J60_OK) return false;
  stable_link_up = link_up;
  candidate_link_up = link_up;
  candidate_count = 2U;
  next_link_poll = xTaskGetTickCount() +
                   pdMS_TO_TICKS(PROJECT_ENC_LINK_POLL_MS);
  initialized = true;
  publish_network_event(APP_EVENT_NET_ENC_READY);
  publish_network_event(link_up ? APP_EVENT_NET_LINK_UP :
                                  APP_EVENT_NET_LINK_DOWN);
  LOG_INFO("ENC", "PHY Link: %s", link_up ? "UP" : "DOWN");
  return true;
}

void netif_task_process(uint32_t notification_count)
{
  uint32_t processed = 0U;
  TickType_t now;

  if (!initialized) return;
  if (notification_count > 0U)
  {
    netif_stats_add(NETIF_STAT_IRQ_COUNT, notification_count);
    if (ENC28J60_ProcessInterruptFlags() != ENC28J60_OK)
    {
      netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
    }
  }
  (void)netif_enc28j60_process_tx(ETH_TX_SLOT_COUNT);
  while ((processed < PROJECT_ENC_MAX_RX_PER_RUN) &&
         (ENC28J60_GetPendingPacketCount() > 0U))
  {
    uint16_t length = 0U;
    enc28j60_status_t status = ENC28J60_ReceiveFrame(
      rx_frame, sizeof(rx_frame), &length);
    if ((status != ENC28J60_OK) || (length == 0U))
    {
      netif_stats_add(NETIF_STAT_RX_DROPPED, 1U);
      break;
    }
    netif_stats_add(NETIF_STAT_RX_FRAMES, 1U);
    netif_stats_add(NETIF_STAT_RX_BYTES, length);
    network_diag_inspect_ethernet(rx_frame, length, false);
    if (lwip_port_input_frame(rx_frame, length) &&
        (!broadcast_test_seen) &&
        is_broadcast_test_frame(rx_frame, length))
    {
      broadcast_test_seen = true;
      LOG_INFO("NETIF", "Broadcast RX: PASS EtherType=0x88B5 length=%u",
               (unsigned int)length);
    }
    processed++;
  }
  if ((ENC28J60_GetPendingPacketCount() > 0U) ||
      (netif_enc28j60_tx_pending() > 0U))
  {
    (void)app_tasks_notify_netif();
    taskYIELD();
  }
  now = xTaskGetTickCount();
  if ((int32_t)(now - next_link_poll) >= 0)
  {
    next_link_poll = now + pdMS_TO_TICKS(PROJECT_ENC_LINK_POLL_MS);
    poll_link_state();
  }
}

bool netif_task_link_is_up(void)
{
  return initialized && stable_link_up;
}
