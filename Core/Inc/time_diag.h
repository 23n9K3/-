#ifndef TIME_DIAG_H
#define TIME_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t dns_requests;
  uint32_t dns_success;
  uint32_t dns_failures;
  uint32_t dns_timeouts;
  uint32_t sntp_start_count;
  uint32_t sntp_sync_success;
  uint32_t sntp_sample_drop;
  uint32_t sntp_invalid_samples;
  uint32_t rtc_update_success;
  uint32_t rtc_update_fail;
  uint32_t rtc_read_fail;
  uint32_t drift_exceeded_count;
  uint32_t large_step_count;
  uint32_t backward_time_count;
  uint32_t trust_lost_count;
  uint32_t holdover_count;
} time_diag_t;

typedef enum
{
  TIME_DIAG_DNS_REQUEST = 0,
  TIME_DIAG_DNS_SUCCESS,
  TIME_DIAG_DNS_FAILURE,
  TIME_DIAG_DNS_TIMEOUT,
  TIME_DIAG_SNTP_START,
  TIME_DIAG_SNTP_SUCCESS,
  TIME_DIAG_SNTP_DROP,
  TIME_DIAG_SNTP_INVALID,
  TIME_DIAG_RTC_UPDATE_SUCCESS,
  TIME_DIAG_RTC_UPDATE_FAIL,
  TIME_DIAG_RTC_READ_FAIL,
  TIME_DIAG_DRIFT_EXCEEDED,
  TIME_DIAG_LARGE_STEP,
  TIME_DIAG_BACKWARD,
  TIME_DIAG_TRUST_LOST,
  TIME_DIAG_HOLDOVER
} time_diag_counter_t;

void time_diag_init(void);
void time_diag_add(time_diag_counter_t counter, uint32_t value);
bool time_diag_get(time_diag_t *snapshot);

#endif /* TIME_DIAG_H */
