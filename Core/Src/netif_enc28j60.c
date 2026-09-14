#include "netif_enc28j60.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "lwip/etharp.h"
#include "lwip/ethip6.h"
#include "lwip/pbuf.h"
#include "lwip/snmp.h"
#include "app_tasks.h"
#include "bsp_enc28j60.h"
#include "netif_stats.h"
#include "network_diag.h"

typedef struct
{
  uint16_t length;
  uint8_t data[ETH_MAX_FRAME_SIZE];
  uint8_t used;
} eth_tx_slot_t;

static eth_tx_slot_t tx_slots[ETH_TX_SLOT_COUNT];
static StaticQueue_t tx_queue_control;
static uint8_t tx_queue_storage[ETH_TX_SLOT_COUNT * sizeof(uint8_t)];
static QueueHandle_t tx_queue;
static bool transport_initialized;
static uint8_t netif_mac[6];
static bool netif_mac_valid;

static int32_t allocate_tx_slot(void)
{
  uint32_t index;
  int32_t result = -1;
  taskENTER_CRITICAL();
  for (index = 0U; index < ETH_TX_SLOT_COUNT; index++)
  {
    if (tx_slots[index].used == 0U)
    {
      tx_slots[index].used = 1U;
      result = (int32_t)index;
      break;
    }
  }
  taskEXIT_CRITICAL();
  return result;
}

static void release_tx_slot(uint8_t index)
{
  if (index >= ETH_TX_SLOT_COUNT) return;
  taskENTER_CRITICAL();
  tx_slots[index].length = 0U;
  tx_slots[index].used = 0U;
  taskEXIT_CRITICAL();
}

static err_t enc28j60_low_level_output(struct netif *netif, struct pbuf *p)
{
  const struct pbuf *part;
  uint16_t offset = 0U;
  uint8_t slot_index;
  int32_t allocated;

  (void)netif;
  if ((p == NULL) || (p->tot_len == 0U) ||
      (p->tot_len > ETH_MAX_FRAME_SIZE) || (!transport_initialized))
  {
    netif_stats_add(NETIF_STAT_TX_DROPPED, 1U);
    return ERR_IF;
  }
  allocated = allocate_tx_slot();
  if (allocated < 0)
  {
    netif_stats_add(NETIF_STAT_TX_DROPPED, 1U);
    return ERR_MEM;
  }
  slot_index = (uint8_t)allocated;
  for (part = p; part != NULL; part = part->next)
  {
    if (((uint32_t)offset + part->len) > ETH_MAX_FRAME_SIZE)
    {
      release_tx_slot(slot_index);
      netif_stats_add(NETIF_STAT_TX_DROPPED, 1U);
      return ERR_BUF;
    }
    memcpy(&tx_slots[slot_index].data[offset], part->payload, part->len);
    offset = (uint16_t)(offset + part->len);
  }
  if (offset != p->tot_len)
  {
    release_tx_slot(slot_index);
    netif_stats_add(NETIF_STAT_TX_DROPPED, 1U);
    return ERR_BUF;
  }
  tx_slots[slot_index].length = offset;
  if (xQueueSend(tx_queue, &slot_index, 0U) != pdPASS)
  {
    release_tx_slot(slot_index);
    netif_stats_add(NETIF_STAT_TX_DROPPED, 1U);
    return ERR_MEM;
  }
  (void)app_tasks_notify_netif();
  return ERR_OK;
}

bool netif_enc28j60_transport_init(void)
{
  if (transport_initialized) return true;
  memset(tx_slots, 0, sizeof(tx_slots));
  tx_queue = xQueueCreateStatic(ETH_TX_SLOT_COUNT, sizeof(uint8_t),
                                tx_queue_storage, &tx_queue_control);
  transport_initialized = (tx_queue != NULL);
  if (transport_initialized)
  {
    vQueueAddToRegistry(tx_queue, "EthTx");
  }
  return transport_initialized;
}

bool netif_enc28j60_set_mac(const uint8_t mac[6])
{
  if ((mac == NULL) || ((mac[0] & 0x03U) != 0x02U)) return false;
  memcpy(netif_mac, mac, sizeof(netif_mac));
  netif_mac_valid = true;
  return true;
}

err_t netif_enc28j60_init(struct netif *netif)
{
  if ((netif == NULL) || (!transport_initialized) || (!netif_mac_valid))
  {
    return ERR_IF;
  }

  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, netif_mac, sizeof(netif_mac));
  netif->mtu = 1500U;
  netif->output = etharp_output;
  netif->linkoutput = enc28j60_low_level_output;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                 NETIF_FLAG_ETHERNET;
#if LWIP_NETIF_HOSTNAME
  netif->hostname = "iotproject";
#endif
  MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, 10000000U);
  return ERR_OK;
}

uint32_t netif_enc28j60_process_tx(uint32_t maximum_frames)
{
  uint32_t processed = 0U;
  uint8_t slot_index;

  if (!transport_initialized) return 0U;
  while ((processed < maximum_frames) &&
         (xQueueReceive(tx_queue, &slot_index, 0U) == pdTRUE))
  {
    if ((slot_index < ETH_TX_SLOT_COUNT) &&
        (tx_slots[slot_index].used != 0U) &&
        (tx_slots[slot_index].length != 0U))
    {
      uint16_t length = tx_slots[slot_index].length;
      if (ENC28J60_SendFrame(tx_slots[slot_index].data, length) == ENC28J60_OK)
      {
        network_diag_inspect_ethernet(tx_slots[slot_index].data, length, true);
        netif_stats_add(NETIF_STAT_TX_FRAMES, 1U);
        netif_stats_add(NETIF_STAT_TX_BYTES, length);
      }
      else
      {
        netif_stats_add(NETIF_STAT_TX_ERRORS, 1U);
      }
      memset(tx_slots[slot_index].data, 0, length);
    }
    else
    {
      netif_stats_add(NETIF_STAT_TX_ERRORS, 1U);
    }
    release_tx_slot(slot_index);
    processed++;
  }
  return processed;
}

uint32_t netif_enc28j60_tx_pending(void)
{
  return (tx_queue == NULL) ? 0U : (uint32_t)uxQueueMessagesWaiting(tx_queue);
}
