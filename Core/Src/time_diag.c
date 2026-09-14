#include "time_diag.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static time_diag_t diagnostics;

void time_diag_init(void)
{
  memset(&diagnostics, 0, sizeof(diagnostics));
}

void time_diag_add(time_diag_counter_t counter, uint32_t value)
{
  bool scheduler_running =
    xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
  if (scheduler_running) taskENTER_CRITICAL();
  switch (counter)
  {
    case TIME_DIAG_DNS_REQUEST: diagnostics.dns_requests += value; break;
    case TIME_DIAG_DNS_SUCCESS: diagnostics.dns_success += value; break;
    case TIME_DIAG_DNS_FAILURE: diagnostics.dns_failures += value; break;
    case TIME_DIAG_DNS_TIMEOUT: diagnostics.dns_timeouts += value; break;
    case TIME_DIAG_SNTP_START: diagnostics.sntp_start_count += value; break;
    case TIME_DIAG_SNTP_SUCCESS: diagnostics.sntp_sync_success += value; break;
    case TIME_DIAG_SNTP_DROP: diagnostics.sntp_sample_drop += value; break;
    case TIME_DIAG_SNTP_INVALID: diagnostics.sntp_invalid_samples += value; break;
    case TIME_DIAG_RTC_UPDATE_SUCCESS: diagnostics.rtc_update_success += value; break;
    case TIME_DIAG_RTC_UPDATE_FAIL: diagnostics.rtc_update_fail += value; break;
    case TIME_DIAG_RTC_READ_FAIL: diagnostics.rtc_read_fail += value; break;
    case TIME_DIAG_DRIFT_EXCEEDED: diagnostics.drift_exceeded_count += value; break;
    case TIME_DIAG_LARGE_STEP: diagnostics.large_step_count += value; break;
    case TIME_DIAG_BACKWARD: diagnostics.backward_time_count += value; break;
    case TIME_DIAG_TRUST_LOST: diagnostics.trust_lost_count += value; break;
    case TIME_DIAG_HOLDOVER: diagnostics.holdover_count += value; break;
    default: break;
  }
  if (scheduler_running) taskEXIT_CRITICAL();
}

bool time_diag_get(time_diag_t *snapshot)
{
  if (snapshot == NULL) return false;
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    taskENTER_CRITICAL();
    *snapshot = diagnostics;
    taskEXIT_CRITICAL();
    return true;
  }
  *snapshot = diagnostics;
  return true;
}
