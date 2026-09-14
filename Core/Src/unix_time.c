#include "unix_time.h"

#include <stddef.h>

static bool is_leap(uint32_t year)
{
  return ((year % 4U) == 0U) &&
         (((year % 100U) != 0U) || ((year % 400U) == 0U));
}

static uint32_t days_in_month(uint32_t year, uint32_t month)
{
  static const uint8_t days[12] =
  {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };
  if ((month == 0U) || (month > 12U)) return 0U;
  if ((month == 2U) && is_leap(year)) return 29U;
  return days[month - 1U];
}

static bool utc_valid(const bsp_rtc_datetime_t *utc)
{
  uint32_t maximum;
  if ((utc == NULL) || (utc->year < 2000U) || (utc->year > 2099U) ||
      (utc->month < 1U) || (utc->month > 12U) ||
      (utc->hour > 23U) || (utc->minute > 59U) ||
      (utc->second > 59U) || (utc->weekday < 1U) || (utc->weekday > 7U))
    return false;
  maximum = days_in_month(utc->year, utc->month);
  return (utc->day >= 1U) && ((uint32_t)utc->day <= maximum);
}

bool utc_to_unix_time(const bsp_rtc_datetime_t *utc, uint64_t *epoch)
{
  uint64_t days = 0ULL;
  uint32_t year;
  uint32_t month;
  uint8_t expected_weekday;
  if ((epoch == NULL) || (!utc_valid(utc))) return false;
  for (year = 1970U; year < utc->year; year++)
    days += is_leap(year) ? 366ULL : 365ULL;
  for (month = 1U; month < utc->month; month++)
    days += (uint64_t)days_in_month(utc->year, month);
  days += (uint64_t)utc->day - 1ULL;
  expected_weekday = (uint8_t)(((days + 3ULL) % 7ULL) + 1ULL);
  if (utc->weekday != expected_weekday) return false;
  if (days > (0xFFFFFFFFFFFFFFFFULL / 86400ULL)) return false;
  *epoch = days * 86400ULL + (uint64_t)utc->hour * 3600ULL +
           (uint64_t)utc->minute * 60ULL + (uint64_t)utc->second;
  return true;
}

bool unix_time_to_utc(uint64_t epoch, bsp_rtc_datetime_t *utc)
{
  uint64_t days;
  uint64_t seconds;
  uint32_t year = 1970U;
  uint32_t month = 1U;
  uint32_t year_days;
  uint32_t month_days;
  if (utc == NULL) return false;
  days = epoch / 86400ULL;
  seconds = epoch % 86400ULL;
  while (year <= 2099U)
  {
    year_days = is_leap(year) ? 366U : 365U;
    if (days < year_days) break;
    days -= year_days;
    year++;
  }
  if ((year < 2000U) || (year > 2099U)) return false;
  while (month <= 12U)
  {
    month_days = days_in_month(year, month);
    if (days < month_days) break;
    days -= month_days;
    month++;
  }
  if (month > 12U) return false;
  utc->year = (uint16_t)year;
  utc->month = (uint8_t)month;
  utc->day = (uint8_t)(days + 1ULL);
  utc->hour = (uint8_t)(seconds / 3600ULL);
  seconds %= 3600ULL;
  utc->minute = (uint8_t)(seconds / 60ULL);
  utc->second = (uint8_t)(seconds % 60ULL);
  utc->weekday = (uint8_t)((((epoch / 86400ULL) + 3ULL) % 7ULL) + 1ULL);
  return true;
}

static bool vector_ok(uint64_t epoch, const bsp_rtc_datetime_t *expected)
{
  bsp_rtc_datetime_t actual;
  uint64_t round_trip;
  return unix_time_to_utc(epoch, &actual) &&
         (actual.year == expected->year) &&
         (actual.month == expected->month) &&
         (actual.day == expected->day) &&
         (actual.hour == expected->hour) &&
         (actual.minute == expected->minute) &&
         (actual.second == expected->second) &&
         (actual.weekday == expected->weekday) &&
         utc_to_unix_time(&actual, &round_trip) && (round_trip == epoch);
}

bool unix_time_self_test(void)
{
  static const bsp_rtc_datetime_t vectors[] =
  {
    {2000U, 2U, 29U, 0U, 0U, 0U, 2U},
    {2024U, 2U, 29U, 0U, 0U, 0U, 4U},
    {2023U, 2U, 28U, 0U, 0U, 0U, 2U},
    {2023U, 12U, 31U, 23U, 59U, 59U, 7U},
    {2024U, 1U, 1U, 0U, 0U, 0U, 1U},
    {2038U, 1U, 19U, 3U, 14U, 7U, 2U},
    {2038U, 1U, 19U, 3U, 14U, 8U, 2U},
    {2099U, 12U, 31U, 23U, 59U, 59U, 4U}
  };
  static const uint64_t epochs[] =
  {
    951782400ULL, 1709164800ULL, 1677542400ULL, 1704067199ULL,
    1704067200ULL, 2147483647ULL, 2147483648ULL, 4102444799ULL
  };
  bsp_rtc_datetime_t invalid = {2023U, 2U, 29U, 0U, 0U, 0U, 3U};
  uint64_t ignored;
  uint32_t index;
  for (index = 0U; index < (sizeof(vectors) / sizeof(vectors[0])); index++)
  {
    if (!vector_ok(epochs[index], &vectors[index])) return false;
  }
  return !utc_to_unix_time(&invalid, &ignored);
}
