#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_RTC_INITIALIZED_MAGIC  0x52544331U

typedef struct
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday;
} bsp_rtc_datetime_t;

typedef enum
{
  BSP_RTC_STATUS_OK = 0,
  BSP_RTC_STATUS_INVALID_ARGUMENT,
  BSP_RTC_STATUS_INVALID_DATETIME,
  BSP_RTC_STATUS_HAL_ERROR,
  BSP_RTC_STATUS_NOT_INITIALIZED
} bsp_rtc_status_t;

bsp_rtc_status_t bsp_rtc_init(void);
bsp_rtc_status_t bsp_rtc_set_utc(const bsp_rtc_datetime_t *datetime,
                                 bool trusted);
bsp_rtc_status_t bsp_rtc_get_utc(bsp_rtc_datetime_t *datetime);
bool bsp_rtc_is_trusted(void);
void bsp_rtc_mark_untrusted(void);
uint32_t bsp_rtc_get_error_count(void);

#endif /* BSP_RTC_H */
