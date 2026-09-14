#ifndef NETIF_STATS_H
#define NETIF_STATS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t rx_frames;
  uint32_t rx_bytes;
  uint32_t rx_pbuf_alloc_ok;
  uint32_t rx_pbuf_alloc_fail;
  uint32_t rx_input_ok;
  uint32_t rx_input_fail;
  uint32_t rx_dropped;
  uint32_t tx_frames;
  uint32_t tx_bytes;
  uint32_t tx_dropped;
  uint32_t tx_errors;
  uint32_t irq_count;
  uint32_t mbox_full;
  uint32_t callback_drop;
  uint32_t link_up_count;
  uint32_t link_down_count;
} netif_stats_t;

typedef enum
{
  NETIF_STAT_RX_FRAMES = 0,
  NETIF_STAT_RX_BYTES,
  NETIF_STAT_RX_PBUF_ALLOC_OK,
  NETIF_STAT_RX_PBUF_ALLOC_FAIL,
  NETIF_STAT_RX_INPUT_OK,
  NETIF_STAT_RX_INPUT_FAIL,
  NETIF_STAT_RX_DROPPED,
  NETIF_STAT_TX_FRAMES,
  NETIF_STAT_TX_BYTES,
  NETIF_STAT_TX_DROPPED,
  NETIF_STAT_TX_ERRORS,
  NETIF_STAT_IRQ_COUNT,
  NETIF_STAT_MBOX_FULL,
  NETIF_STAT_CALLBACK_DROP,
  NETIF_STAT_LINK_UP_COUNT,
  NETIF_STAT_LINK_DOWN_COUNT
} netif_stat_counter_t;

void netif_stats_init(void);
void netif_stats_add(netif_stat_counter_t counter, uint32_t value);
void netif_stats_add_from_isr(netif_stat_counter_t counter, uint32_t value);
bool netif_stats_get(netif_stats_t *snapshot);

#endif /* NETIF_STATS_H */
