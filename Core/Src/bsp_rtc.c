#include "bsp_rtc.h"

#include "rtc.h"

#define BSP_RTC_MAGIC_REGISTER RTC_BKP_DR0
#define BSP_RTC_TRUST_REGISTER RTC_BKP_DR1
#define BSP_RTC_TRUSTED_MAGIC  0x54525553U

static bool rtc_is_initialized;
static uint32_t rtc_error_count;

static bool bsp_rtc_is_leap_year(uint16_t year)
{
  return ((year % 4U) == 0U);
}

static bool bsp_rtc_datetime_is_valid(const bsp_rtc_datetime_t *datetime)
{
  static const uint8_t days_per_month[12] =
  {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };
  uint8_t maximum_day;

  if ((datetime->year < 2000U) || (datetime->year > 2099U) ||
      (datetime->month < 1U) || (datetime->month > 12U) ||
      (datetime->hour > 23U) || (datetime->minute > 59U) ||
      (datetime->second > 59U) ||
      (datetime->weekday < RTC_WEEKDAY_MONDAY) ||
      (datetime->weekday > RTC_WEEKDAY_SUNDAY))
  {
    return false;
  }

  maximum_day = days_per_month[datetime->month - 1U];
  if ((datetime->month == 2U) && bsp_rtc_is_leap_year(datetime->year))
  {
    maximum_day++;
  }

  return (datetime->day >= 1U) && (datetime->day <= maximum_day);
}

bsp_rtc_status_t bsp_rtc_init(void)
{
  static const bsp_rtc_datetime_t default_datetime =
  {
    2026U, 1U, 1U, 0U, 0U, 0U, RTC_WEEKDAY_THURSDAY
  };

  if ((hrtc.Instance != RTC) ||
      (HAL_RTC_GetState(&hrtc) == HAL_RTC_STATE_RESET) ||
      (HAL_RTC_GetState(&hrtc) == HAL_RTC_STATE_ERROR))
  {
    rtc_error_count++;
    return BSP_RTC_STATUS_HAL_ERROR;
  }

  rtc_is_initialized = true;
  if (HAL_RTCEx_BKUPRead(&hrtc, BSP_RTC_MAGIC_REGISTER) !=
      BSP_RTC_INITIALIZED_MAGIC)
  {
    bsp_rtc_status_t status = bsp_rtc_set_utc(&default_datetime, false);
    if (status != BSP_RTC_STATUS_OK)
    {
      rtc_is_initialized = false;
      return status;
    }
  }

  return BSP_RTC_STATUS_OK;
}

bsp_rtc_status_t bsp_rtc_set_utc(const bsp_rtc_datetime_t *datetime,
                                 bool trusted)
{
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if (datetime == NULL)
  {
    return BSP_RTC_STATUS_INVALID_ARGUMENT;
  }
  if (!rtc_is_initialized)
  {
    return BSP_RTC_STATUS_NOT_INITIALIZED;
  }
  if (!bsp_rtc_datetime_is_valid(datetime))
  {
    return BSP_RTC_STATUS_INVALID_DATETIME;
  }

  rtc_time.Hours = datetime->hour;
  rtc_time.Minutes = datetime->minute;
  rtc_time.Seconds = datetime->second;
  rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  rtc_time.StoreOperation = RTC_STOREOPERATION_RESET;

  rtc_date.WeekDay = datetime->weekday;
  rtc_date.Month = datetime->month;
  rtc_date.Date = datetime->day;
  rtc_date.Year = (uint8_t)(datetime->year - 2000U);

  if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    rtc_error_count++;
    return BSP_RTC_STATUS_HAL_ERROR;
  }
  if (HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    rtc_error_count++;
    HAL_RTCEx_BKUPWrite(&hrtc, BSP_RTC_TRUST_REGISTER, 0U);
    HAL_RTCEx_BKUPWrite(&hrtc, BSP_RTC_MAGIC_REGISTER, 0U);
    return BSP_RTC_STATUS_HAL_ERROR;
  }

  HAL_RTCEx_BKUPWrite(&hrtc,
                      BSP_RTC_TRUST_REGISTER,
                      trusted ? BSP_RTC_TRUSTED_MAGIC : 0U);
  HAL_RTCEx_BKUPWrite(&hrtc,
                      BSP_RTC_MAGIC_REGISTER,
                      BSP_RTC_INITIALIZED_MAGIC);
  return BSP_RTC_STATUS_OK;
}

bsp_rtc_status_t bsp_rtc_get_utc(bsp_rtc_datetime_t *datetime)
{
  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  if (datetime == NULL)
  {
    return BSP_RTC_STATUS_INVALID_ARGUMENT;
  }
  if (!rtc_is_initialized)
  {
    return BSP_RTC_STATUS_NOT_INITIALIZED;
  }

  if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    rtc_error_count++;
    return BSP_RTC_STATUS_HAL_ERROR;
  }
  /* Date must be read after Time to unlock the RTC shadow registers. */
  if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    rtc_error_count++;
    return BSP_RTC_STATUS_HAL_ERROR;
  }

  datetime->year = (uint16_t)rtc_date.Year + 2000U;
  datetime->month = rtc_date.Month;
  datetime->day = rtc_date.Date;
  datetime->hour = rtc_time.Hours;
  datetime->minute = rtc_time.Minutes;
  datetime->second = rtc_time.Seconds;
  datetime->weekday = rtc_date.WeekDay;

  if (!bsp_rtc_datetime_is_valid(datetime))
  {
    rtc_error_count++;
    return BSP_RTC_STATUS_INVALID_DATETIME;
  }

  return BSP_RTC_STATUS_OK;
}

bool bsp_rtc_is_trusted(void)
{
  if (!rtc_is_initialized)
  {
    return false;
  }

  return HAL_RTCEx_BKUPRead(&hrtc, BSP_RTC_TRUST_REGISTER) ==
         BSP_RTC_TRUSTED_MAGIC;
}

void bsp_rtc_mark_untrusted(void)
{
  if (rtc_is_initialized)
  {
    HAL_RTCEx_BKUPWrite(&hrtc, BSP_RTC_TRUST_REGISTER, 0U);
  }
}

uint32_t bsp_rtc_get_error_count(void)
{
  return rtc_error_count;
}
