#include "netif_stats.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static netif_stats_t statistics;

static void add_unprotected(netif_stat_counter_t counter, uint32_t value)
{
  switch (counter)
  {
    case NETIF_STAT_RX_FRAMES: statistics.rx_frames += value; break;
    case NETIF_STAT_RX_BYTES: statistics.rx_bytes += value; break;
    case NETIF_STAT_RX_PBUF_ALLOC_OK: statistics.rx_pbuf_alloc_ok += value; break;
    case NETIF_STAT_RX_PBUF_ALLOC_FAIL: statistics.rx_pbuf_alloc_fail += value; break;
    case NETIF_STAT_RX_INPUT_OK: statistics.rx_input_ok += value; break;
    case NETIF_STAT_RX_INPUT_FAIL: statistics.rx_input_fail += value; break;
    case NETIF_STAT_RX_DROPPED: statistics.rx_dropped += value; break;
    case NETIF_STAT_TX_FRAMES: statistics.tx_frames += value; break;
    case NETIF_STAT_TX_BYTES: statistics.tx_bytes += value; break;
    case NETIF_STAT_TX_DROPPED: statistics.tx_dropped += value; break;
    case NETIF_STAT_TX_ERRORS: statistics.tx_errors += value; break;
    case NETIF_STAT_IRQ_COUNT: statistics.irq_count += value; break;
    case NETIF_STAT_MBOX_FULL: statistics.mbox_full += value; break;
    case NETIF_STAT_CALLBACK_DROP: statistics.callback_drop += value; break;
    case NETIF_STAT_LINK_UP_COUNT: statistics.link_up_count += value; break;
    case NETIF_STAT_LINK_DOWN_COUNT: statistics.link_down_count += value; break;
    default: break;
  }
}

void netif_stats_init(void)
{
  taskENTER_CRITICAL();
  memset(&statistics, 0, sizeof(statistics));
  taskEXIT_CRITICAL();
}

void netif_stats_add(netif_stat_counter_t counter, uint32_t value)
{
  taskENTER_CRITICAL();
  add_unprotected(counter, value);
  taskEXIT_CRITICAL();
}

void netif_stats_add_from_isr(netif_stat_counter_t counter, uint32_t value)
{
  UBaseType_t saved_mask = taskENTER_CRITICAL_FROM_ISR();
  add_unprotected(counter, value);
  taskEXIT_CRITICAL_FROM_ISR(saved_mask);
}

bool netif_stats_get(netif_stats_t *snapshot)
{
  if (snapshot == NULL) return false;
  taskENTER_CRITICAL();
  *snapshot = statistics;
  taskEXIT_CRITICAL();
  return true;
}
