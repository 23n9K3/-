#include "network_sntp.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "lwip/apps/sntp.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "time_diag.h"
#include "time_policy.h"
#include "time_sync_task.h"

static StaticQueue_t sample_queue_storage;
static network_time_sample_t sample_queue_items[TIME_SAMPLE_QUEUE_LENGTH];
static QueueHandle_t sample_queue;
static ip_addr_t requested_address;
static volatile uint32_t requested_generation;
static volatile uint32_t active_generation;
static volatile bool start_requested;
static volatile bool stop_requested;
static volatile bool callback_pending;

static bool schedule_callback(void);

static void sntp_control_callback(void *argument)
{
  bool do_start;
  bool do_stop;
  ip_addr_t address;
  uint32_t generation;
  bool retry;
  (void)argument;
  taskENTER_CRITICAL();
  callback_pending = false;
  do_start = start_requested;
  do_stop = stop_requested;
  start_requested = false;
  stop_requested = false;
  address = requested_address;
  generation = requested_generation;
  taskEXIT_CRITICAL();
  if (do_stop && sntp_enabled()) sntp_stop();
  if (do_start)
  {
    if (sntp_enabled()) sntp_stop();
    active_generation = generation;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setserver(0U, &address);
#if SNTP_MAX_SERVERS > 1
    sntp_setserver(1U, NULL);
#endif
    sntp_init();
    time_diag_add(TIME_DIAG_SNTP_START, 1U);
  }
  taskENTER_CRITICAL();
  retry = start_requested || stop_requested;
  taskEXIT_CRITICAL();
  if (retry) (void)schedule_callback();
}

static bool schedule_callback(void)
{
  bool submit = false;
  taskENTER_CRITICAL();
  if (!callback_pending)
  {
    callback_pending = true;
    submit = true;
  }
  taskEXIT_CRITICAL();
  if (submit && (tcpip_try_callback(sntp_control_callback, NULL) != ERR_OK))
  {
    taskENTER_CRITICAL();
    callback_pending = false;
    taskEXIT_CRITICAL();
    return false;
  }
  return true;
}

bool network_sntp_init(void)
{
  sample_queue = xQueueCreateStatic(TIME_SAMPLE_QUEUE_LENGTH,
    sizeof(network_time_sample_t), (uint8_t *)sample_queue_items,
    &sample_queue_storage);
  if (sample_queue == NULL) return false;
  ip_addr_set_zero(&requested_address);
  requested_generation = 0U;
  active_generation = 0U;
  start_requested = false;
  stop_requested = false;
  callback_pending = false;
  return true;
}

bool network_sntp_request_start(const ip_addr_t *address,
                                uint32_t generation)
{
  if (address == NULL) return false;
  taskENTER_CRITICAL();
  requested_address = *address;
  requested_generation = generation;
  start_requested = true;
  taskEXIT_CRITICAL();
  return schedule_callback();
}

bool network_sntp_request_stop(void)
{
  taskENTER_CRITICAL();
  stop_requested = true;
  start_requested = false;
  taskEXIT_CRITICAL();
  return schedule_callback();
}

void network_sntp_stop_from_tcpip(void)
{
  taskENTER_CRITICAL();
  start_requested = false;
  stop_requested = false;
  callback_pending = false;
  active_generation++;
  taskEXIT_CRITICAL();
  if (sntp_enabled()) sntp_stop();
  if (sample_queue != NULL) (void)xQueueReset(sample_queue);
}

bool network_sntp_take_sample(network_time_sample_t *sample)
{
  return (sample != NULL) && (sample_queue != NULL) &&
         (xQueueReceive(sample_queue, sample, 0U) == pdTRUE);
}

void network_sntp_lwip_time_received(uint32_t seconds,
                                     uint32_t microseconds)
{
  network_time_sample_t sample;
  if ((sample_queue == NULL) || (microseconds >= 1000000U))
  {
    time_diag_add(TIME_DIAG_SNTP_INVALID, 1U);
    return;
  }
  sample.epoch = (uint64_t)seconds;
  sample.microseconds = microseconds;
  sample.generation = active_generation;
  if (xQueueSend(sample_queue, &sample, 0U) != pdTRUE)
  {
    time_diag_add(TIME_DIAG_SNTP_DROP, 1U);
    return;
  }
  time_sync_task_signal(TIME_SYNC_NOTIFY_SNTP_SAMPLE);
}
