#ifndef PLATFORM_TIME_H
#define PLATFORM_TIME_H

#include <stdbool.h>
#include <stdint.h>

uint32_t platform_time_get_ms(void);
bool platform_time_elapsed(uint32_t now,
                           uint32_t previous,
                           uint32_t interval_ms);

#endif /* PLATFORM_TIME_H */
