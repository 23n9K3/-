#include "trusted_time.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "rtc.h"
#include "platform_log.h"
#include "time_diag.h"
#include "time_policy.h"
#include "unix_time.h"

#define TIME_METADATA_MAGIC       0x54494D45UL
#define TIME_METADATA_VERSION     1UL
#define TIME_METADATA_SOURCE_SNTP 1UL
#define TIME_LOCK_TIMEOUT_MS      20U

#define TIME_META_MAGIC_REG       RTC_BKP_DR2
#define TIME_META_VERSION_REG     RTC_BKP_DR3
#define TIME_META_SYNC_REG        RTC_BKP_DR4
#define TIME_META_RTC_REG         RTC_BKP_DR5
#define TIME_META_GENERATION_REG  RTC_BKP_DR6
#define TIME_META_SOURCE_REG      RTC_BKP_DR7
#define TIME_META_CRC_REG         RTC_BKP_DR8

static StaticSemaphore_t time_mutex_storage;
static SemaphoreHandle_t time_mutex;
static bool initialized;
static time_trust_state_t trust_state;
static time_invalid_reason_t invalid_reason;
static uint64_t last_sync_epoch;
static uint32_t metadata_generation;
static uint64_t candidate_epoch;
static TickType_t candidate_tick;
static uint32_t candidate_count;
static uint32_t reset_flags;

static uint32_t capture_reset_flags(void)
{
  uint32_t flags = 0U;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET) flags |= TIME_RESET_FLAG_PIN;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != RESET) flags |= TIME_RESET_FLAG_BOR;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET) flags |= TIME_RESET_FLAG_SOFTWARE;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) flags |= TIME_RESET_FLAG_IWDG;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET) flags |= TIME_RESET_FLAG_WWDG;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET) flags |= TIME_RESET_FLAG_LOW_POWER;
  return flags;
}

static uint32_t metadata_crc(const uint32_t *words, uint32_t count)
{
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t index;
  uint32_t bit;
  for (index = 0U; index < count; index++)
  {
    crc ^= words[index];
    for (bit = 0U; bit < 32U; bit++)
      crc = ((crc & 0x80000000UL) != 0U) ?
            ((crc << 1U) ^ 0x04C11DB7UL) : (crc << 1U);
  }
  return crc;
}

static bool lock_time(void)
{
  if (!initialized || (time_mutex == NULL)) return false;
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return true;
  return xSemaphoreTake(time_mutex, pdMS_TO_TICKS(TIME_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void unlock_time(void)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    (void)xSemaphoreGive(time_mutex);
}

static uint64_t absolute_difference(uint64_t first, uint64_t second)
{
  return (first >= second) ? (first - second) : (second - first);
}

static bool read_rtc_epoch(uint64_t *epoch, bsp_rtc_datetime_t *datetime)
{
  bsp_rtc_datetime_t local;
  if (epoch == NULL) return false;
  if (bsp_rtc_get_utc(&local) != BSP_RTC_STATUS_OK)
  {
    time_diag_add(TIME_DIAG_RTC_READ_FAIL, 1U);
    return false;
  }
  if (!utc_to_unix_time(&local, epoch))
  {
    time_diag_add(TIME_DIAG_RTC_READ_FAIL, 1U);
    return false;
  }
  if (datetime != NULL) *datetime = local;
  return true;
}

static void clear_metadata(void)
{
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_CRC_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_MAGIC_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_VERSION_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_SYNC_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_RTC_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_GENERATION_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_SOURCE_REG, 0U);
}

static bool load_metadata(uint64_t *sync_epoch, uint64_t *rtc_epoch,
                          uint32_t *generation)
{
  uint32_t words[6];
  uint32_t stored_crc;
  words[0] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_MAGIC_REG);
  words[1] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_VERSION_REG);
  words[2] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_SYNC_REG);
  words[3] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_RTC_REG);
  words[4] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_GENERATION_REG);
  words[5] = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_SOURCE_REG);
  stored_crc = HAL_RTCEx_BKUPRead(&hrtc, TIME_META_CRC_REG);
  if ((words[0] != TIME_METADATA_MAGIC) ||
      (words[1] != TIME_METADATA_VERSION) ||
      (words[5] != TIME_METADATA_SOURCE_SNTP) ||
      (metadata_crc(words, 6U) != stored_crc)) return false;
  *sync_epoch = (uint64_t)words[2];
  *rtc_epoch = (uint64_t)words[3];
  *generation = words[4];
  return true;
}

static void save_metadata(uint64_t sync_epoch, uint64_t rtc_epoch)
{
  uint32_t words[6];
  metadata_generation++;
  words[0] = TIME_METADATA_MAGIC;
  words[1] = TIME_METADATA_VERSION;
  words[2] = (uint32_t)sync_epoch;
  words[3] = (uint32_t)rtc_epoch;
  words[4] = metadata_generation;
  words[5] = TIME_METADATA_SOURCE_SNTP;
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_CRC_REG, 0U);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_MAGIC_REG, words[0]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_VERSION_REG, words[1]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_SYNC_REG, words[2]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_RTC_REG, words[3]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_GENERATION_REG, words[4]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_SOURCE_REG, words[5]);
  HAL_RTCEx_BKUPWrite(&hrtc, TIME_META_CRC_REG, metadata_crc(words, 6U));
}

static bool sample_in_range(uint64_t epoch, bsp_rtc_datetime_t *utc)
{
  return unix_time_to_utc(epoch, utc) &&
         (utc->year >= TIME_MIN_VALID_YEAR) &&
         (utc->year <= TIME_MAX_VALID_YEAR);
}

bool trusted_time_init(void)
{
  uint64_t rtc_epoch;
  uint64_t saved_sync;
  uint64_t saved_rtc;
  uint32_t saved_generation;
  bsp_rtc_datetime_t utc;
  time_mutex = xSemaphoreCreateMutexStatic(&time_mutex_storage);
  if (time_mutex == NULL) return false;
  initialized = true;
  trust_state = TIME_TRUST_INVALID;
  invalid_reason = TIME_INVALID_NONE;
  last_sync_epoch = 0ULL;
  metadata_generation = 0U;
  candidate_epoch = 0ULL;
  candidate_tick = 0U;
  candidate_count = 0U;
  reset_flags = capture_reset_flags();
  if ((!read_rtc_epoch(&rtc_epoch, &utc)) ||
      (utc.year < TIME_MIN_VALID_YEAR) || (utc.year > TIME_MAX_VALID_YEAR))
  {
    trust_state = TIME_TRUST_INVALID;
    invalid_reason = TIME_INVALID_RANGE;
    bsp_rtc_mark_untrusted();
    return true;
  }
  if (!load_metadata(&saved_sync, &saved_rtc, &saved_generation))
  {
    clear_metadata();
    trust_state = TIME_TRUST_RTC_UNVERIFIED;
    invalid_reason = TIME_INVALID_METADATA;
    bsp_rtc_mark_untrusted();
    return true;
  }
  metadata_generation = saved_generation;
  last_sync_epoch = saved_sync;
  if ((rtc_epoch + TIME_CONFIRM_TOLERANCE_SEC) < saved_rtc)
  {
    trust_state = TIME_TRUST_SUSPECT;
    invalid_reason = TIME_INVALID_BACKWARD;
    bsp_rtc_mark_untrusted();
    time_diag_add(TIME_DIAG_BACKWARD, 1U);
  }
  else if ((rtc_epoch >= saved_sync) &&
           ((rtc_epoch - saved_sync) <= TIME_MAX_HOLDOVER_SEC))
  {
    trust_state = TIME_TRUST_RTC_HOLDOVER;
    time_diag_add(TIME_DIAG_HOLDOVER, 1U);
  }
  else
  {
    trust_state = TIME_TRUST_RTC_UNVERIFIED;
    invalid_reason = TIME_INVALID_HOLDOVER_EXPIRED;
    bsp_rtc_mark_untrusted();
  }
  return true;
}

uint32_t trusted_time_get_reset_flags(void)
{
  return reset_flags;
}

time_trust_state_t trusted_time_get_state(void)
{
  time_trust_state_t state = TIME_TRUST_INVALID;
  if (lock_time())
  {
    state = trust_state;
    unlock_time();
  }
  return state;
}

const char *trusted_time_state_text(time_trust_state_t state)
{
  switch (state)
  {
    case TIME_TRUST_INVALID: return "INVALID";
    case TIME_TRUST_RTC_UNVERIFIED: return "RTC_UNVERIFIED";
    case TIME_TRUST_RTC_HOLDOVER: return "RTC_HOLDOVER";
    case TIME_TRUST_SNTP_SYNCED: return "SNTP_SYNCED";
    case TIME_TRUST_SUSPECT: return "SUSPECT";
    default: return "UNKNOWN";
  }
}

bool trusted_time_is_trusted(void)
{
  time_trust_state_t state = trusted_time_get_state();
  return (state == TIME_TRUST_SNTP_SYNCED) ||
         ((state == TIME_TRUST_RTC_HOLDOVER) &&
          (trusted_time_get_age_seconds() <= TIME_MAX_HOLDOVER_SEC));
}

bool trusted_time_get_epoch(uint64_t *epoch)
{
  bool result = false;
  if ((epoch != NULL) && lock_time())
  {
    result = read_rtc_epoch(epoch, NULL);
    unlock_time();
  }
  return result;
}

bool trusted_time_get_utc(bsp_rtc_datetime_t *datetime)
{
  bool result = false;
  if ((datetime != NULL) && lock_time())
  {
    result = bsp_rtc_get_utc(datetime) == BSP_RTC_STATUS_OK;
    if (!result) time_diag_add(TIME_DIAG_RTC_READ_FAIL, 1U);
    unlock_time();
  }
  return result;
}

bool trusted_time_allows_x509_validation(void)
{
  return trusted_time_is_trusted();
}

uint32_t trusted_time_get_age_seconds(void)
{
  uint64_t now;
  uint64_t age;
  uint32_t result = 0xFFFFFFFFUL;
  if (lock_time())
  {
    if ((last_sync_epoch != 0ULL) && read_rtc_epoch(&now, NULL) &&
        (now >= last_sync_epoch))
    {
      age = now - last_sync_epoch;
      result = (age > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)age;
    }
    unlock_time();
  }
  return result;
}

void trusted_time_invalidate(time_invalid_reason_t reason)
{
  if (!lock_time()) return;
  trust_state = TIME_TRUST_INVALID;
  invalid_reason = reason;
  candidate_count = 0U;
  bsp_rtc_mark_untrusted();
  time_diag_add(TIME_DIAG_TRUST_LOST, 1U);
  unlock_time();
}

time_invalid_reason_t trusted_time_get_invalid_reason(void)
{
  time_invalid_reason_t reason = TIME_INVALID_RTC_READ;
  if (lock_time())
  {
    reason = invalid_reason;
    unlock_time();
  }
  return reason;
}

trusted_time_sample_result_t trusted_time_process_sntp_sample(
  uint64_t epoch, uint32_t microseconds)
{
  bsp_rtc_datetime_t sample_utc;
  bsp_rtc_datetime_t verify_utc;
  uint64_t rtc_epoch;
  uint64_t verify_epoch;
  uint64_t difference;
  uint64_t expected;
  TickType_t now_tick;
  bool previously_trusted;
  bool needs_confirmation;
  if ((microseconds >= 1000000U) || (!sample_in_range(epoch, &sample_utc)))
  {
    time_diag_add(TIME_DIAG_SNTP_INVALID, 1U);
    return TIME_SAMPLE_REJECTED;
  }
  if (!lock_time()) return TIME_SAMPLE_REJECTED;
  if (!read_rtc_epoch(&rtc_epoch, NULL))
  {
    trust_state = TIME_TRUST_INVALID;
    invalid_reason = TIME_INVALID_RTC_READ;
    unlock_time();
    return TIME_SAMPLE_REJECTED;
  }
  previously_trusted = (trust_state == TIME_TRUST_SNTP_SYNCED) ||
                       (trust_state == TIME_TRUST_RTC_HOLDOVER);
  difference = absolute_difference(epoch, rtc_epoch);
  if (difference > TIME_RESYNC_DRIFT_SEC)
    time_diag_add(TIME_DIAG_DRIFT_EXCEEDED, 1U);
  if ((epoch + TIME_CONFIRM_TOLERANCE_SEC) < rtc_epoch)
    time_diag_add(TIME_DIAG_BACKWARD, 1U);
  needs_confirmation = (!previously_trusted) ||
                       (difference > TIME_LARGE_STEP_SEC);
  if (difference > TIME_LARGE_STEP_SEC)
    time_diag_add(TIME_DIAG_LARGE_STEP, 1U);
  now_tick = xTaskGetTickCount();
  if (needs_confirmation)
  {
    if (candidate_count == 0U)
    {
      candidate_epoch = epoch;
      candidate_tick = now_tick;
      candidate_count = 1U;
    }
    else
    {
      expected = candidate_epoch +
        ((uint64_t)(now_tick - candidate_tick) / (uint64_t)configTICK_RATE_HZ);
      if (absolute_difference(epoch, expected) <= TIME_CONFIRM_TOLERANCE_SEC)
        candidate_count++;
      else
      {
        candidate_epoch = epoch;
        candidate_tick = now_tick;
        candidate_count = 1U;
        invalid_reason = TIME_INVALID_SAMPLE_INCONSISTENT;
      }
    }
    if (candidate_count < TIME_CONFIRM_SAMPLE_COUNT)
    {
      trust_state = TIME_TRUST_SUSPECT;
      bsp_rtc_mark_untrusted();
      unlock_time();
      return TIME_SAMPLE_CONFIRMING;
    }
  }
  if (bsp_rtc_set_utc(&sample_utc, true) != BSP_RTC_STATUS_OK)
  {
    trust_state = TIME_TRUST_INVALID;
    invalid_reason = TIME_INVALID_RTC_READ;
    time_diag_add(TIME_DIAG_RTC_UPDATE_FAIL, 1U);
    unlock_time();
    return TIME_SAMPLE_REJECTED;
  }
  if ((bsp_rtc_get_utc(&verify_utc) != BSP_RTC_STATUS_OK) ||
      (!utc_to_unix_time(&verify_utc, &verify_epoch)) ||
      (absolute_difference(verify_epoch, epoch) > 1ULL))
  {
    trust_state = TIME_TRUST_INVALID;
    invalid_reason = TIME_INVALID_RTC_READ;
    bsp_rtc_mark_untrusted();
    time_diag_add(TIME_DIAG_RTC_UPDATE_FAIL, 1U);
    unlock_time();
    return TIME_SAMPLE_REJECTED;
  }
  last_sync_epoch = verify_epoch;
  candidate_count = 0U;
  trust_state = TIME_TRUST_SNTP_SYNCED;
  invalid_reason = TIME_INVALID_NONE;
  save_metadata(verify_epoch, verify_epoch);
  time_diag_add(TIME_DIAG_RTC_UPDATE_SUCCESS, 1U);
  time_diag_add(TIME_DIAG_SNTP_SUCCESS, 1U);
  unlock_time();
  return TIME_SAMPLE_ACCEPTED;
}

void trusted_time_on_network_down(void)
{
  if (!lock_time()) return;
  candidate_count = 0U;
  if (trust_state == TIME_TRUST_SNTP_SYNCED)
  {
    trust_state = TIME_TRUST_RTC_HOLDOVER;
    time_diag_add(TIME_DIAG_HOLDOVER, 1U);
  }
  unlock_time();
  trusted_time_maintenance();
}

void trusted_time_maintenance(void)
{
  uint64_t now;
  if (!lock_time()) return;
  if ((trust_state == TIME_TRUST_RTC_HOLDOVER) &&
      ((!read_rtc_epoch(&now, NULL)) || (now < last_sync_epoch) ||
       ((now - last_sync_epoch) > TIME_MAX_HOLDOVER_SEC)))
  {
    trust_state = TIME_TRUST_RTC_UNVERIFIED;
    invalid_reason = TIME_INVALID_HOLDOVER_EXPIRED;
    bsp_rtc_mark_untrusted();
    time_diag_add(TIME_DIAG_TRUST_LOST, 1U);
  }
  unlock_time();
}

bool security_time_precheck(uint64_t *current_epoch)
{
  bsp_rtc_datetime_t utc;
  if ((current_epoch == NULL) || (!trusted_time_allows_x509_validation()) ||
      (!trusted_time_get_epoch(current_epoch)) ||
      (!trusted_time_get_utc(&utc)))
  {
    LOG_WARN("TIME", "X509 time prerequisite: REJECTED");
    LOG_WARN("TIME", "Reason: TIME_UNTRUSTED");
    return false;
  }
  LOG_INFO("TIME", "X509 time prerequisite: READY");
  LOG_INFO("TIME", "X509 current UTC: %04u-%02u-%02u %02u:%02u:%02u",
           (unsigned int)utc.year, (unsigned int)utc.month,
           (unsigned int)utc.day, (unsigned int)utc.hour,
           (unsigned int)utc.minute, (unsigned int)utc.second);
  return true;
}

bool trusted_time_fault_inject(time_fault_t fault)
{
#if TIME_FAULT_INJECTION_ENABLE
  bsp_rtc_datetime_t wrong = {2000U, 1U, 1U, 0U, 0U, 0U, 6U};
  uint64_t now;
  if (!lock_time()) return false;
  switch (fault)
  {
    case TIME_FAULT_WRONG_DATE:
      clear_metadata();
      (void)bsp_rtc_set_utc(&wrong, false);
      trust_state = TIME_TRUST_RTC_UNVERIFIED;
      break;
    case TIME_FAULT_CLEAR_METADATA:
      clear_metadata();
      trust_state = TIME_TRUST_RTC_UNVERIFIED;
      bsp_rtc_mark_untrusted();
      break;
    case TIME_FAULT_RTC_INVALID:
      trust_state = TIME_TRUST_INVALID;
      invalid_reason = TIME_INVALID_FAULT_INJECTION;
      bsp_rtc_mark_untrusted();
      break;
    case TIME_FAULT_LARGE_STEP:
    case TIME_FAULT_BACKWARD:
    case TIME_FAULT_INCONSISTENT:
      trust_state = TIME_TRUST_SUSPECT;
      invalid_reason = TIME_INVALID_FAULT_INJECTION;
      bsp_rtc_mark_untrusted();
      break;
    case TIME_FAULT_HOLDOVER_EXPIRED:
      if (read_rtc_epoch(&now, NULL))
        last_sync_epoch = (now > TIME_MAX_HOLDOVER_SEC) ?
          now - TIME_MAX_HOLDOVER_SEC - 1ULL : 0ULL;
      trust_state = TIME_TRUST_RTC_HOLDOVER;
      break;
    default:
      unlock_time();
      return false;
  }
  unlock_time();
  trusted_time_maintenance();
  return true;
#else
  (void)fault;
  return false;
#endif
}
