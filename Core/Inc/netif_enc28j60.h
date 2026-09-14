#ifndef NETIF_ENC28J60_H
#define NETIF_ENC28J60_H

#include <stdbool.h>
#include <stdint.h>

#include "lwip/err.h"
#include "lwip/netif.h"

#define ETH_MAX_FRAME_SIZE 1536U
#define ETH_TX_SLOT_COUNT   4U

bool netif_enc28j60_transport_init(void);
bool netif_enc28j60_set_mac(const uint8_t mac[6]);
err_t netif_enc28j60_init(struct netif *netif);
uint32_t netif_enc28j60_process_tx(uint32_t maximum_frames);
uint32_t netif_enc28j60_tx_pending(void);

#endif /* NETIF_ENC28J60_H */
