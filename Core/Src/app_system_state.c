#include "app_system_state.h"

#include "app_ipc.h"
#include "semphr.h"
#include "task.h"

static StaticSemaphore_t state_mutex_control;
static SemaphoreHandle_t state_mutex;
static app_system_state_snapshot_t system_state;
static uint32_t state_mutex_error_count;

static void app_system_state_clear(void)
{
  system_state.last_event = APP_EVENT_NONE;
  system_state.last_source = APP_EVENT_SOURCE_SYSTEM;
  system_state.last_event_tick = 0U;
  system_state.processed_event_count = 0U;
  system_state.event_error_count = 0U;
  system_state.can_error_count = 0U;
  system_state.network_error_count = 0U;
  system_state.security_error_count = 0U;
  system_state.mutex_error_count = 0U;
  state_mutex_error_count = 0U;
}

static void app_system_state_record_mutex_error(void)
{
  taskENTER_CRITICAL();
  state_mutex_error_count++;
  taskEXIT_CRITICAL();
}

static void app_system_state_update_bits(const app_event_t *event)
{
  switch (event->type)
  {
    case APP_EVENT_SYSTEM_ERROR:
      (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
      break;
    case APP_EVENT_CAN_READY:
    case APP_EVENT_CAN_RECOVERED:
      (void)app_state_set_bits(APP_STATE_BIT_CAN_READY);
      break;
    case APP_EVENT_CAN_ERROR:
    case APP_EVENT_CAN_BUS_OFF:
      (void)app_state_clear_bits(APP_STATE_BIT_CAN_READY);
      (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
      break;
    case APP_EVENT_NET_ENC_READY:
      (void)app_state_set_bits(APP_STATE_BIT_ENC_READY);
      break;
    case APP_EVENT_NET_LINK_UP:
      (void)app_state_set_bits(APP_STATE_BIT_NET_LINK_UP);
      break;
    case APP_EVENT_NET_LINK_DOWN:
      (void)app_state_clear_bits(APP_STATE_BIT_NET_LINK_UP |
                                 APP_STATE_BIT_IP_READY |
                                 APP_STATE_BIT_SERVICES_READY);
      break;
    case APP_EVENT_NET_IP_READY:
      (void)app_state_set_bits(APP_STATE_BIT_IP_READY);
      break;
    case APP_EVENT_NET_ERROR:
      (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
      break;
    case APP_EVENT_SECURITY_ERROR:
      (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
      break;
    case APP_EVENT_TLS_CONNECTED:
      (void)app_state_set_bits(APP_STATE_BIT_TLS_CONNECTED);
      break;
    case APP_EVENT_TLS_DISCONNECTED:
    case APP_EVENT_TLS_ERROR:
      (void)app_state_clear_bits(APP_STATE_BIT_TLS_CONNECTED);
      if (event->type == APP_EVENT_TLS_ERROR)
      {
        (void)app_state_set_bits(APP_STATE_BIT_DEGRADED);
      }
      break;
    default:
      break;
  }
}

bool app_system_state_init(void)
{
  state_mutex = xSemaphoreCreateMutexStatic(&state_mutex_control);
  if (state_mutex == NULL)
  {
    return false;
  }

  app_system_state_clear();
  vQueueAddToRegistry((QueueHandle_t)state_mutex, "SystemStateMtx");
  return true;
}

bool app_system_state_process_event(const app_event_t *event,
                                    TickType_t timeout_ticks)
{
  if ((event == NULL) || (state_mutex == NULL) ||
      (timeout_ticks == portMAX_DELAY))
  {
    return false;
  }
  if (xSemaphoreTake(state_mutex, timeout_ticks) != pdTRUE)
  {
    app_system_state_record_mutex_error();
    return false;
  }

  system_state.last_event = event->type;
  system_state.last_source = event->source;
  system_state.last_event_tick = event->tick;
  system_state.processed_event_count++;
  switch (event->type)
  {
    case APP_EVENT_SYSTEM_ERROR:
      system_state.event_error_count++;
      break;
    case APP_EVENT_CAN_ERROR:
    case APP_EVENT_CAN_BUS_OFF:
      system_state.can_error_count++;
      break;
    case APP_EVENT_NET_ERROR:
      system_state.network_error_count++;
      break;
    case APP_EVENT_SECURITY_ERROR:
    case APP_EVENT_TLS_ERROR:
      system_state.security_error_count++;
      break;
    default:
      break;
  }
  (void)xSemaphoreGive(state_mutex);

  /* Event-group operations occur after releasing the snapshot mutex. */
  app_system_state_update_bits(event);
  return true;
}

bool app_system_state_get_snapshot(app_system_state_snapshot_t *snapshot,
                                   TickType_t timeout_ticks)
{
  if ((snapshot == NULL) || (state_mutex == NULL) ||
      (timeout_ticks == portMAX_DELAY))
  {
    return false;
  }
  if (xSemaphoreTake(state_mutex, timeout_ticks) != pdTRUE)
  {
    app_system_state_record_mutex_error();
    return false;
  }

  *snapshot = system_state;
  snapshot->mutex_error_count = state_mutex_error_count;
  (void)xSemaphoreGive(state_mutex);
  return true;
}

bool app_system_state_self_test(void)
{
  static app_event_t event;
  app_system_state_snapshot_t snapshot;
  bool passed;

  if (state_mutex == NULL)
  {
    return false;
  }

  event.type = APP_EVENT_CAN_ERROR;
  event.source = APP_EVENT_SOURCE_CAN;
  event.tick = 123U;
  if ((!app_system_state_process_event(&event, 0U)) ||
      (!app_system_state_get_snapshot(&snapshot, 0U)))
  {
    return false;
  }

  passed = (snapshot.last_event == APP_EVENT_CAN_ERROR) &&
           (snapshot.last_source == APP_EVENT_SOURCE_CAN) &&
           (snapshot.last_event_tick == 123U) &&
           (snapshot.processed_event_count == 1U) &&
           (snapshot.can_error_count == 1U) &&
           (snapshot.mutex_error_count == 0U);

  if (xSemaphoreTake(state_mutex, 0U) != pdTRUE)
  {
    return false;
  }
  app_system_state_clear();
  (void)xSemaphoreGive(state_mutex);
  (void)app_state_clear_bits(APP_STATE_USER_BITS_MASK);
  return passed;
}
