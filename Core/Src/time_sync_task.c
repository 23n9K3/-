#include "time_sync_task.h"

#include <string.h>

#include "app_event.h"
#include "app_ipc.h"
#include "app_tasks.h"
#include "bsp_rng.h"
#include "lwip/ip4_addr.h"
#include "network_dns.h"
#include "network_sntp.h"
#include "platform_log.h"
#include "time_diag.h"
#include "time_policy.h"
#include "trusted_time.h"

#define TIME_SYNC_ASSERT(name, condition) \
  typedef char time_sync_assert_##name[(condition) ? 1 : -1]

static StaticTask_t time_sync_tcb;
static StackType_t time_sync_stack[TIME_SYNC_TASK_STACK_WORDS];
static TaskHandle_t time_sync_handle;

TIME_SYNC_ASSERT(stack_is_2_kib, sizeof(time_sync_stack) == 2048U);

static bool network_ready;
static volatile bool requested_network_ready;
static bool dns_pending;
static bool sntp_active;
static bool resolved_address_valid;
static ip_addr_t resolved_address;
static uint32_t network_generation;
static uint32_t retry_exponent;
static uint32_t server_index;
static TickType_t dns_started_tick;
static TickType_t next_action_tick;
static TickType_t sntp_deadline_tick;

static bool tick_due(TickType_t now, TickType_t target)
{
  return (int32_t)(now - target) >= 0;
}

static const char *current_server(void)
{
  return (server_index == 0U) ? APP_NTP_SERVER_1 : APP_NTP_SERVER_2;
}

static void publish_time_event(bool trusted)
{
  app_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = trusted ? APP_EVENT_TIME_SYNCED : APP_EVENT_TIME_TRUST_LOST;
  event.source = APP_EVENT_SOURCE_TIME;
  event.tick = xTaskGetTickCount();
  (void)app_event_publish(&event, 0U);
}

static uint32_t retry_delay_ms(void)
{
  uint32_t delay_seconds;
  uint32_t random_value = 0U;
  uint32_t jitter = 0U;
  if (retry_exponent < 6U)
    delay_seconds = 1UL << retry_exponent;
  else
    delay_seconds = TIME_RETRY_MAX_SEC;
  if (delay_seconds > TIME_RETRY_MAX_SEC)
    delay_seconds = TIME_RETRY_MAX_SEC;
  if (retry_exponent < 6U) retry_exponent++;
  if (bsp_rng_get_u32(&random_value) == BSP_RNG_STATUS_OK)
    jitter = random_value % (TIME_RETRY_JITTER_MAX_MS + 1U);
  return delay_seconds * 1000U + jitter;
}

static void schedule_retry(void)
{
  server_index ^= 1U;
  next_action_tick = xTaskGetTickCount() +
                     pdMS_TO_TICKS(retry_delay_ms());
}

static void begin_dns(void)
{
  if (!network_ready) return;
  dns_pending = network_dns_request(current_server(), network_generation);
  if (dns_pending)
  {
    dns_started_tick = xTaskGetTickCount();
    LOG_INFO("TIME", "DNS resolve start: %s", current_server());
  }
  else
  {
    time_diag_add(TIME_DIAG_DNS_FAILURE, 1U);
    LOG_WARN("TIME", "DNS request submit failed: %s", current_server());
    schedule_retry();
  }
}

static void log_ipv4_address(const ip_addr_t *address)
{
  uint32_t host = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(address)));
  LOG_INFO("TIME", "DNS address: %lu.%lu.%lu.%lu",
           (unsigned long)((host >> 24U) & 0xFFU),
           (unsigned long)((host >> 16U) & 0xFFU),
           (unsigned long)((host >> 8U) & 0xFFU),
           (unsigned long)(host & 0xFFU));
}

static void handle_dns_result(void)
{
  network_dns_result_t result;
  if (!network_dns_take_result(&result)) return;
  if ((!network_ready) || (result.generation != network_generation)) return;
  dns_pending = false;
  if (!result.success || (!IP_IS_V4(&result.address)))
  {
    time_diag_add(TIME_DIAG_DNS_FAILURE, 1U);
    LOG_WARN("TIME", "DNS resolve failed: %s", current_server());
    schedule_retry();
    return;
  }
  time_diag_add(TIME_DIAG_DNS_SUCCESS, 1U);
  retry_exponent = 0U;
  resolved_address = result.address;
  resolved_address_valid = true;
  LOG_INFO("TIME", "DNS resolve: PASS");
  log_ipv4_address(&resolved_address);
  if (network_sntp_request_start(&resolved_address, network_generation))
  {
    sntp_active = true;
    sntp_deadline_tick = xTaskGetTickCount() +
      pdMS_TO_TICKS(TIME_SNTP_RESPONSE_TIMEOUT_SEC * 1000U);
  }
  else
  {
    sntp_active = false;
    LOG_WARN("TIME", "SNTP start submit failed");
    schedule_retry();
  }
}

static void log_accepted_time(const network_time_sample_t *sample)
{
  bsp_rtc_datetime_t utc;
  uint64_t current_epoch;
  LOG_INFO("TIME", "SNTP sync: PASS");
  LOG_INFO("TIME", "SNTP epoch: %lu",
           (unsigned long)sample->epoch);
  if (trusted_time_get_epoch(&current_epoch) &&
      trusted_time_get_utc(&utc))
  {
    LOG_INFO("TIME", "RTC updated: PASS");
    LOG_INFO("TIME", "RTC: %04u-%02u-%02u %02u:%02u:%02u UTC",
             (unsigned int)utc.year, (unsigned int)utc.month,
             (unsigned int)utc.day, (unsigned int)utc.hour,
             (unsigned int)utc.minute, (unsigned int)utc.second);
    LOG_INFO("TIME", "Time source: SNTP");
    LOG_INFO("TIME", "Time trusted: YES age=%lu s",
             (unsigned long)trusted_time_get_age_seconds());
  }
}

static void handle_sntp_samples(void)
{
  network_time_sample_t sample;
  while (network_sntp_take_sample(&sample))
  {
    trusted_time_sample_result_t result;
    if ((!network_ready) || (sample.generation != network_generation))
    {
      time_diag_add(TIME_DIAG_SNTP_DROP, 1U);
      continue;
    }
    result = trusted_time_process_sntp_sample(sample.epoch,
                                               sample.microseconds);
    if (result == TIME_SAMPLE_ACCEPTED)
    {
      retry_exponent = 0U;
      (void)app_state_set_bits(APP_STATE_BIT_TIME_TRUSTED);
      publish_time_event(true);
      (void)app_tasks_notify_security(SECURITY_NOTIFY_TIME_STATE);
      log_accepted_time(&sample);
      sntp_deadline_tick = xTaskGetTickCount() +
        pdMS_TO_TICKS((TIME_SYNC_INTERVAL_SEC +
                       TIME_SNTP_RESPONSE_TIMEOUT_SEC) * 1000U);
    }
    else if (result == TIME_SAMPLE_CONFIRMING)
    {
      (void)app_state_clear_bits(APP_STATE_BIT_TIME_TRUSTED);
      LOG_WARN("TIME", "SNTP sample requires confirmation");
      (void)network_sntp_request_stop();
      sntp_active = false;
      next_action_tick = xTaskGetTickCount() +
                         pdMS_TO_TICKS(TIME_CONFIRM_RETRY_SEC * 1000U);
    }
    else
    {
      (void)app_state_clear_bits(APP_STATE_BIT_TIME_TRUSTED);
      LOG_WARN("TIME", "SNTP sample rejected by time policy");
      (void)network_sntp_request_stop();
      sntp_active = false;
      schedule_retry();
    }
  }
}

static void handle_network_down(void)
{
  time_trust_state_t state;
  network_ready = false;
  dns_pending = false;
  sntp_active = false;
  resolved_address_valid = false;
  network_generation++;
  network_dns_cancel(network_generation);
  trusted_time_on_network_down();
  state = trusted_time_get_state();
  if (trusted_time_is_trusted())
  {
    (void)app_state_set_bits(APP_STATE_BIT_TIME_TRUSTED);
    LOG_INFO("TIME", "Time source: RTC_HOLDOVER");
    LOG_INFO("TIME", "Time trusted: YES");
    LOG_INFO("TIME", "Holdover age: %lu seconds",
             (unsigned long)trusted_time_get_age_seconds());
  }
  else
  {
    (void)app_state_clear_bits(APP_STATE_BIT_TIME_TRUSTED);
    publish_time_event(false);
    LOG_WARN("TIME", "Time trusted: NO state=%s",
             trusted_time_state_text(state));
  }
  (void)app_tasks_notify_security(SECURITY_NOTIFY_TIME_STATE);
}

static void handle_network_up(void)
{
  network_ready = true;
  dns_pending = false;
  sntp_active = false;
  resolved_address_valid = false;
  retry_exponent = 0U;
  server_index = 0U;
  network_generation++;
  network_dns_cancel(network_generation);
  next_action_tick = xTaskGetTickCount();
}

static void time_sync_task(void *argument)
{
  uint32_t notification_bits;
  time_trust_state_t previous_state = trusted_time_get_state();
  (void)argument;
  LOG_INFO("TASK", "TimeSyncTask started");
  for (;;)
  {
    TickType_t now;
    notification_bits = 0U;
    (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notification_bits,
                          pdMS_TO_TICKS(1000U));
    if ((notification_bits & (TIME_SYNC_NOTIFY_NETWORK_UP |
                              TIME_SYNC_NOTIFY_NETWORK_DOWN)) != 0U)
    {
      bool requested_ready;
      taskENTER_CRITICAL();
      requested_ready = requested_network_ready;
      taskEXIT_CRITICAL();
      if (requested_ready) handle_network_up();
      else handle_network_down();
    }
    if ((notification_bits & TIME_SYNC_NOTIFY_DNS_RESULT) != 0U)
      handle_dns_result();
    if ((notification_bits & TIME_SYNC_NOTIFY_SNTP_SAMPLE) != 0U)
      handle_sntp_samples();

    trusted_time_maintenance();
    if (previous_state != trusted_time_get_state())
    {
      previous_state = trusted_time_get_state();
      if (!trusted_time_is_trusted())
      {
        (void)app_state_clear_bits(APP_STATE_BIT_TIME_TRUSTED);
        (void)app_tasks_notify_security(SECURITY_NOTIFY_TIME_STATE);
      }
    }
    now = xTaskGetTickCount();
    if (network_ready && dns_pending &&
        ((uint32_t)(now - dns_started_tick) >=
         pdMS_TO_TICKS(TIME_DNS_TIMEOUT_SEC * 1000U)))
    {
      dns_pending = false;
      network_generation++;
      network_dns_cancel(network_generation);
      time_diag_add(TIME_DIAG_DNS_TIMEOUT, 1U);
      LOG_WARN("TIME", "DNS resolve timeout: %s", current_server());
      schedule_retry();
    }
    if (network_ready && (!dns_pending) && (!sntp_active) &&
        tick_due(now, next_action_tick))
    {
      if (resolved_address_valid)
      {
        if (network_sntp_request_start(&resolved_address,
                                       network_generation))
        {
          sntp_active = true;
          sntp_deadline_tick = now +
            pdMS_TO_TICKS(TIME_SNTP_RESPONSE_TIMEOUT_SEC * 1000U);
        }
        else
          schedule_retry();
      }
      else
      {
        begin_dns();
      }
    }
    if (network_ready && sntp_active && tick_due(now, sntp_deadline_tick))
    {
      LOG_WARN("TIME", "SNTP response timeout");
      (void)network_sntp_request_stop();
      sntp_active = false;
      resolved_address_valid = false;
      schedule_retry();
    }
  }
}

bool time_sync_task_create(void)
{
  network_dns_init();
  if (!network_sntp_init()) return false;
  network_ready = false;
  requested_network_ready = false;
  dns_pending = false;
  sntp_active = false;
  resolved_address_valid = false;
  network_generation = 1U;
  retry_exponent = 0U;
  server_index = 0U;
  dns_started_tick = 0U;
  next_action_tick = 0U;
  sntp_deadline_tick = 0U;
  time_sync_handle = xTaskCreateStatic(time_sync_task, "TimeSync",
    TIME_SYNC_TASK_STACK_WORDS, NULL, TIME_SYNC_TASK_PRIORITY,
    time_sync_stack, &time_sync_tcb);
  return time_sync_handle != NULL;
}

TaskHandle_t time_sync_task_get_handle(void)
{
  return time_sync_handle;
}

void time_sync_task_signal(uint32_t notification_bits)
{
  if (time_sync_handle != NULL)
    (void)xTaskNotify(time_sync_handle, notification_bits, eSetBits);
}

void time_sync_task_network_state_from_tcpip(bool ready)
{
  taskENTER_CRITICAL();
  requested_network_ready = ready;
  taskEXIT_CRITICAL();
  if (!ready) network_sntp_stop_from_tcpip();
  time_sync_task_signal(ready ? TIME_SYNC_NOTIFY_NETWORK_UP :
                                TIME_SYNC_NOTIFY_NETWORK_DOWN);
}
