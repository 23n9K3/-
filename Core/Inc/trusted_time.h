#ifndef TRUSTED_TIME_H
#define TRUSTED_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_rtc.h"

typedef enum
{
  TIME_TRUST_INVALID = 0,
  TIME_TRUST_RTC_UNVERIFIED,
  TIME_TRUST_RTC_HOLDOVER,
  TIME_TRUST_SNTP_SYNCED,
  TIME_TRUST_SUSPECT
} time_trust_state_t;

typedef enum
{
  TIME_INVALID_NONE = 0,
  TIME_INVALID_RTC_READ,
  TIME_INVALID_RANGE,
  TIME_INVALID_METADATA,
  TIME_INVALID_BACKWARD,
  TIME_INVALID_HOLDOVER_EXPIRED,
  TIME_INVALID_SAMPLE_INCONSISTENT,
  TIME_INVALID_FAULT_INJECTION
} time_invalid_reason_t;

typedef enum
{
  TIME_SAMPLE_REJECTED = 0,
  TIME_SAMPLE_CONFIRMING,
  TIME_SAMPLE_ACCEPTED
} trusted_time_sample_result_t;

typedef enum
{
  TIME_FAULT_WRONG_DATE = 0,
  TIME_FAULT_CLEAR_METADATA,
  TIME_FAULT_RTC_INVALID,
  TIME_FAULT_LARGE_STEP,
  TIME_FAULT_BACKWARD,
  TIME_FAULT_INCONSISTENT,
  TIME_FAULT_HOLDOVER_EXPIRED
} time_fault_t;

#define TIME_RESET_FLAG_PIN       (1UL << 0)
#define TIME_RESET_FLAG_BOR       (1UL << 1)
#define TIME_RESET_FLAG_SOFTWARE  (1UL << 2)
#define TIME_RESET_FLAG_IWDG      (1UL << 3)
#define TIME_RESET_FLAG_WWDG      (1UL << 4)
#define TIME_RESET_FLAG_LOW_POWER (1UL << 5)

bool trusted_time_init(void);
time_trust_state_t trusted_time_get_state(void);
const char *trusted_time_state_text(time_trust_state_t state);
bool trusted_time_is_trusted(void);
bool trusted_time_get_epoch(uint64_t *epoch);
bool trusted_time_get_utc(bsp_rtc_datetime_t *datetime);
bool trusted_time_allows_x509_validation(void);
uint32_t trusted_time_get_age_seconds(void);
void trusted_time_invalidate(time_invalid_reason_t reason);
time_invalid_reason_t trusted_time_get_invalid_reason(void);
trusted_time_sample_result_t trusted_time_process_sntp_sample(
  uint64_t epoch, uint32_t microseconds);
void trusted_time_on_network_down(void);
void trusted_time_maintenance(void);
bool security_time_precheck(uint64_t *current_epoch);
bool trusted_time_fault_inject(time_fault_t fault);
uint32_t trusted_time_get_reset_flags(void);

#endif /* TRUSTED_TIME_H */
