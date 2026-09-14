#include "app_main.h"

#include "app_event.h"
#include "app_ipc.h"
#include "app_system_state.h"
#include "bsp_crc.h"
#include "bsp_rng.h"
#include "bsp_rtc.h"
#include "device_identity.h"
#include "project_config.h"
#include "time_diag.h"
#include "trusted_time.h"
#include "unix_time.h"

#if PROJECT_ENABLE_PRINTF_REDIRECT_TEST
#include <stdio.h>
#endif

#include "platform_log.h"
#include "project_version.h"
#include "stm32l4xx_hal.h"

static bool hardware_security_ready;

static void app_main_log_startup(void)
{
  platform_log_raw("================================================\r\n");
  LOG_INFO("BOOT", "%s starting", PROJECT_NAME);
  LOG_INFO("BOOT", "Version: %s", PROJECT_VERSION_STRING);
  LOG_INFO("SYS", "Board: NUCLEO-L4R5ZI-P");
  LOG_INFO("SYS", "MCU: STM32L4R5ZIT6P");
  LOG_INFO("SYS", "SYSCLK: %lu Hz",
           (unsigned long)HAL_RCC_GetSysClockFreq());
  LOG_INFO("UART", "LPUART1 ready, 115200-8-N-1");
  LOG_DEBUG("RTOS", "seven-task static framework ready");
  platform_log_raw("================================================\r\n");

#if PROJECT_ENABLE_PRINTF_REDIRECT_TEST
  (void)printf("printf redirect test\r\n");
#endif
}

static bool app_main_init_identity(void)
{
  if (!device_identity_init())
  {
    LOG_ERROR("IDENTITY", "UID formatting failed");
    return false;
  }

  LOG_INFO("IDENTITY", "UID: %s", device_identity_get_uid_hex());
  LOG_INFO("IDENTITY", "Device ID: %s", device_identity_get_device_id());
  return true;
}

static bool app_main_init_rng(void)
{
  bsp_rng_status_t status;
  uint32_t diagnostic_sample = 0U;

  status = bsp_rng_init();
  if (status != BSP_RNG_STATUS_OK)
  {
    LOG_ERROR("RNG", "RNG init failed, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_rng_get_error_count());
    return false;
  }

#if PROJECT_HWSEC_SELF_TEST_ENABLE
  status = bsp_rng_self_test(&diagnostic_sample);
  if (status != BSP_RNG_STATUS_OK)
  {
    LOG_ERROR("RNG", "RNG self-test: FAIL, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_rng_get_error_count());
    diagnostic_sample = 0U;
    return false;
  }
  LOG_INFO("RNG", "RNG self-test: PASS");
#if PROJECT_HWSEC_DIAGNOSTIC_LOG_ENABLE
  LOG_DEBUG("RNG", "Diagnostic sample: 0x%08lX (discarded)",
            (unsigned long)diagnostic_sample);
#endif
#else
  LOG_WARN("RNG", "RNG self-test disabled");
#endif

  /* The diagnostic sample is deliberately discarded and must never be reused. */
  diagnostic_sample = 0U;
  return true;
}

static bool app_main_init_rtc(void)
{
  bsp_rtc_datetime_t datetime;
  bsp_rtc_status_t status = bsp_rtc_init();

  if (status != BSP_RTC_STATUS_OK)
  {
    LOG_ERROR("RTC", "RTC init failed, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_rtc_get_error_count());
    return false;
  }

  status = bsp_rtc_get_utc(&datetime);
  if (status != BSP_RTC_STATUS_OK)
  {
    LOG_ERROR("RTC", "RTC read failed, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_rtc_get_error_count());
    return false;
  }

  LOG_INFO("RTC", "RTC: %04u-%02u-%02u %02u:%02u:%02u UTC",
           (unsigned int)datetime.year,
           (unsigned int)datetime.month,
           (unsigned int)datetime.day,
           (unsigned int)datetime.hour,
           (unsigned int)datetime.minute,
           (unsigned int)datetime.second);
  time_diag_init();
  if (!unix_time_self_test())
  {
    LOG_ERROR("TIME", "Unix/UTC conversion self-test: FAIL");
    return false;
  }
  LOG_INFO("TIME", "Unix/UTC conversion self-test: PASS");
  if (!trusted_time_init())
  {
    LOG_ERROR("TIME", "Trusted time state initialization failed");
    return false;
  }
  LOG_INFO("TIME", "Reset flags: 0x%02lX",
           (unsigned long)trusted_time_get_reset_flags());
  LOG_INFO("TIME", "Initial time state: %s",
           trusted_time_state_text(trusted_time_get_state()));
  LOG_INFO("TIME", "Time trusted: %s",
           trusted_time_is_trusted() ? "YES" : "NO");
  return true;
}

static bool app_main_init_crc(void)
{
  bsp_crc_status_t status = bsp_crc_init();
  uint32_t calculated_crc = 0U;

  if (status != BSP_CRC_STATUS_OK)
  {
    LOG_ERROR("CRC", "CRC init failed, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_crc_get_error_count());
    return false;
  }

#if PROJECT_HWSEC_SELF_TEST_ENABLE
  status = bsp_crc_self_test(&calculated_crc);
  LOG_INFO("CRC", "CRC test vector: 0x%08lX",
           (unsigned long)calculated_crc);
  if (status != BSP_CRC_STATUS_OK)
  {
    LOG_ERROR("CRC", "CRC self-test: FAIL, status=%u errors=%lu",
              (unsigned int)status,
              (unsigned long)bsp_crc_get_error_count());
    return false;
  }
  LOG_INFO("CRC", "CRC self-test: PASS");
#else
  LOG_WARN("CRC", "CRC self-test disabled");
#endif
  return true;
}

static void app_main_init_hardware_security(void)
{
  bool identity_ok;
  bool rng_ok;
  bool rtc_ok;
  bool crc_ok;

  LOG_INFO("HWSEC", "Hardware security base init");
  identity_ok = app_main_init_identity();
  rng_ok = app_main_init_rng();
  rtc_ok = app_main_init_rtc();
  crc_ok = app_main_init_crc();
  hardware_security_ready = identity_ok && rng_ok && rtc_ok && crc_ok;

  if (hardware_security_ready)
  {
    LOG_INFO("HWSEC", "Hardware security base: PASS");
  }
  else
  {
    LOG_ERROR("HWSEC", "Hardware security base: FAIL");
  }
}

void app_main_init(void)
{
  hardware_security_ready = false;
  app_main_log_startup();
  app_main_init_hardware_security();
}

bool app_main_hardware_security_ready(void)
{
  return hardware_security_ready;
}

bool app_main_static_ipc_init(void)
{
  app_ipc_self_test_result_t ipc_test_result;
  static app_event_t ready_event;
  uint32_t log_test_drop_count = 0U;
  bool passed = true;

  LOG_INFO("IPC", "Static IPC initialization");
  if (!app_ipc_init())
  {
    LOG_ERROR("IPC", "Static event queue or event group creation failed");
    return false;
  }
  LOG_INFO("IPC", "Event queue created: length=%u item_size=%u",
           (unsigned int)APP_EVENT_QUEUE_LENGTH,
           (unsigned int)sizeof(app_event_t));
  LOG_INFO("IPC", "Static event group created");

  if (!app_system_state_init())
  {
    LOG_ERROR("IPC", "Static state mutex creation failed");
    return false;
  }
  LOG_INFO("IPC", "Static state mutex created");

  if (!platform_log_queue_init())
  {
    LOG_ERROR("LOGGER", "Static log queue creation failed");
    return false;
  }
  LOG_INFO("LOGGER", "Static log queue created: length=%u item_size=%u",
           (unsigned int)PROJECT_LOG_QUEUE_LENGTH,
           (unsigned int)PROJECT_LOG_RECORD_SIZE);

#if PROJECT_IPC_SELF_TEST_ENABLE
  if (!app_ipc_self_test(&ipc_test_result))
  {
    LOG_ERROR("IPC", "Event queue, event group or ring self-test: FAIL");
    passed = false;
  }
  else
  {
    LOG_INFO("RING", "Ring buffer self-test: PASS");
    LOG_INFO("IPC", "Event queue FIFO test: PASS");
    LOG_INFO("IPC", "Event queue full test: PASS");
    LOG_INFO("IPC", "Event drop test count: %lu (test only)",
             (unsigned long)ipc_test_result.event_drop_count);
    LOG_INFO("IPC", "Event group self-test: PASS");
  }

  if (!app_system_state_self_test())
  {
    LOG_ERROR("IPC", "Static mutex self-test: FAIL");
    passed = false;
  }
  else
  {
    LOG_INFO("IPC", "Static mutex self-test: PASS");
  }
#endif

#if PROJECT_LOG_QUEUE_FULL_TEST_ENABLE
  if (!platform_log_queue_self_test(&log_test_drop_count))
  {
    LOG_ERROR("LOGGER", "Log queue full test: FAIL");
    passed = false;
  }
  else
  {
    LOG_INFO("LOGGER", "Log queue full test: PASS, test_drop=%lu",
             (unsigned long)log_test_drop_count);
  }
#endif

  if (hardware_security_ready)
  {
    (void)app_state_set_bits(APP_STATE_BIT_HWSEC_READY);
  }
  else
  {
    (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
  }
  if (trusted_time_is_trusted())
  {
    (void)app_state_set_bits(APP_STATE_BIT_TIME_TRUSTED);
  }

  ready_event.type = passed ? APP_EVENT_SYSTEM_READY : APP_EVENT_SYSTEM_ERROR;
  ready_event.source = APP_EVENT_SOURCE_SYSTEM;
  if (!app_event_publish(&ready_event, 0U))
  {
    LOG_ERROR("IPC", "Initial system event publish failed");
    passed = false;
  }

  if (passed)
  {
    LOG_INFO("IPC", "Static IPC framework: PASS");
  }
  else
  {
    (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
    LOG_ERROR("IPC", "Static IPC framework: FAIL");
  }
  return passed;
}
