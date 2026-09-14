#ifndef RAW_ETH_TEST_H
#define RAW_ETH_TEST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t test_tx_pass;
  uint32_t test_tx_fail;
  uint32_t test_rx_pass;
  uint32_t test_rx_fail;
  uint32_t echoed_frames;
  uint32_t other_ethertype_frames;
  uint32_t last_tx_sequence;
  uint32_t last_rx_sequence;
} raw_eth_test_stats_t;

bool raw_eth_test_init(void);
void raw_eth_test_process(bool interrupt_wake);
const raw_eth_test_stats_t *raw_eth_test_get_stats(void);
bool raw_eth_test_link_is_up(void);

#endif /* RAW_ETH_TEST_H */
