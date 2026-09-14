#ifndef UNIX_TIME_H
#define UNIX_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_rtc.h"

bool unix_time_to_utc(uint64_t epoch, bsp_rtc_datetime_t *utc);
bool utc_to_unix_time(const bsp_rtc_datetime_t *utc, uint64_t *epoch);
bool unix_time_self_test(void);

#endif /* UNIX_TIME_H */
