#ifndef RUNTIME_STATS_H
#define RUNTIME_STATS_H

#include <stdbool.h>
#include <stdint.h>

bool runtime_stats_init(void);
void runtime_stats_configure_for_freertos(void);
uint32_t runtime_stats_get_counter(void);
uint32_t runtime_stats_get_frequency_hz(void);

#endif /* RUNTIME_STATS_H */
