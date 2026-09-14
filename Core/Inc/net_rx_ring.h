#ifndef NET_RX_RING_H
#define NET_RX_RING_H

#include <stdbool.h>
#include <stdint.h>

void net_rx_ring_init(void);
uint32_t net_rx_ring_used(void);
uint32_t net_rx_ring_free(void);
bool net_rx_ring_write(const uint8_t *data, uint32_t length);
uint32_t net_rx_ring_read(uint8_t *data, uint32_t length);
uint32_t net_rx_ring_peek(uint8_t *data, uint32_t length);
bool net_rx_ring_discard(uint32_t length);
void net_rx_ring_reset(void);
bool net_rx_ring_self_test(void);

#endif /* NET_RX_RING_H */
