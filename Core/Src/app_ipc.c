#include "app_ipc.h"

#include <stddef.h>
#include <stdint.h>

#include "queue.h"
#include "static_ring_buffer.h"
#include "task.h"

static StaticQueue_t event_queue_control;
static uint8_t event_queue_storage[
  APP_EVENT_QUEUE_LENGTH * sizeof(app_event_t)];
static QueueHandle_t event_queue;

static StaticEventGroup_t state_event_group_control;
static EventGroupHandle_t state_event_group;

static uint32_t event_sequence;
static uint32_t event_publish_count;
static uint32_t event_receive_count;
static uint32_t event_drop_count;
static UBaseType_t event_high_water_mark;

typedef char event_queue_storage_size_check[
  (sizeof(event_queue_storage) ==
   (APP_EVENT_QUEUE_LENGTH * sizeof(app_event_t))) ? 1 : -1];

static void app_event_reset_statistics(void)
{
  event_sequence = 0U;
  event_publish_count = 0U;
  event_receive_count = 0U;
  event_drop_count = 0U;
  event_high_water_mark = 0U;
}

static uint32_t app_event_next_sequence(void)
{
  uint32_t result;
  taskENTER_CRITICAL();
  event_sequence++;
  result = event_sequence;
  taskEXIT_CRITICAL();
  return result;
}

static uint32_t app_event_next_sequence_from_isr(void)
{
  UBaseType_t saved_interrupt_status;
  uint32_t result;
  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  event_sequence++;
  result = event_sequence;
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  return result;
}

static void app_event_update_high_water(UBaseType_t waiting)
{
  taskENTER_CRITICAL();
  if (waiting > event_high_water_mark)
  {
    event_high_water_mark = waiting;
  }
  taskEXIT_CRITICAL();
}

static void app_event_update_high_water_from_isr(UBaseType_t waiting)
{
  UBaseType_t saved_interrupt_status;
  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  if (waiting > event_high_water_mark)
  {
    event_high_water_mark = waiting;
  }
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
}

bool app_ipc_init(void)
{
  event_queue = xQueueCreateStatic(APP_EVENT_QUEUE_LENGTH,
                                   sizeof(app_event_t),
                                   event_queue_storage,
                                   &event_queue_control);
  state_event_group = xEventGroupCreateStatic(&state_event_group_control);
  if ((event_queue == NULL) || (state_event_group == NULL))
  {
    return false;
  }

  app_event_reset_statistics();
  vQueueAddToRegistry(event_queue, "AppEventQ");
  return true;
}

bool app_event_publish(const app_event_t *event, TickType_t timeout_ticks)
{
  app_event_t queued_event;
  UBaseType_t waiting;

  if ((event == NULL) || (event_queue == NULL) ||
      (timeout_ticks == portMAX_DELAY))
  {
    return false;
  }

  queued_event = *event;
  queued_event.tick = xTaskGetTickCount();
  queued_event.sequence = app_event_next_sequence();
  if (xQueueSend(event_queue, &queued_event, timeout_ticks) != pdPASS)
  {
    taskENTER_CRITICAL();
    event_drop_count++;
    taskEXIT_CRITICAL();
    return false;
  }

  taskENTER_CRITICAL();
  event_publish_count++;
  taskEXIT_CRITICAL();
  waiting = uxQueueMessagesWaiting(event_queue);
  app_event_update_high_water(waiting);
  return true;
}

bool app_event_publish_from_isr(const app_event_t *event,
                                BaseType_t *higher_priority_task_woken)
{
  app_event_t queued_event;
  UBaseType_t saved_interrupt_status;
  UBaseType_t waiting;

  if ((event == NULL) || (event_queue == NULL) ||
      (higher_priority_task_woken == NULL))
  {
    return false;
  }

  queued_event = *event;
  queued_event.tick = xTaskGetTickCountFromISR();
  queued_event.sequence = app_event_next_sequence_from_isr();
  if (xQueueSendFromISR(event_queue,
                        &queued_event,
                        higher_priority_task_woken) != pdPASS)
  {
    saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
    event_drop_count++;
    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
    return false;
  }

  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  event_publish_count++;
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  waiting = uxQueueMessagesWaitingFromISR(event_queue);
  app_event_update_high_water_from_isr(waiting);
  return true;
}

bool app_event_receive(app_event_t *event, TickType_t timeout_ticks)
{
  if ((event == NULL) || (event_queue == NULL) ||
      (timeout_ticks == portMAX_DELAY))
  {
    return false;
  }

  if (xQueueReceive(event_queue, event, timeout_ticks) != pdPASS)
  {
    return false;
  }

  taskENTER_CRITICAL();
  event_receive_count++;
  taskEXIT_CRITICAL();
  return true;
}

uint32_t app_event_get_publish_count(void)
{
  return event_publish_count;
}

uint32_t app_event_get_receive_count(void)
{
  return event_receive_count;
}

uint32_t app_event_get_drop_count(void)
{
  return event_drop_count;
}

UBaseType_t app_event_get_waiting_count(void)
{
  return (event_queue != NULL) ? uxQueueMessagesWaiting(event_queue) : 0U;
}

UBaseType_t app_event_get_high_water_mark(void)
{
  return event_high_water_mark;
}

EventBits_t app_state_set_bits(EventBits_t bits)
{
  bits &= APP_STATE_USER_BITS_MASK;
  return ((state_event_group != NULL) && (bits != 0U)) ?
         xEventGroupSetBits(state_event_group, bits) : 0U;
}

EventBits_t app_state_clear_bits(EventBits_t bits)
{
  bits &= APP_STATE_USER_BITS_MASK;
  return ((state_event_group != NULL) && (bits != 0U)) ?
         xEventGroupClearBits(state_event_group, bits) : 0U;
}

EventBits_t app_state_get_bits(void)
{
  return (state_event_group != NULL) ?
         xEventGroupGetBits(state_event_group) : 0U;
}

EventBits_t app_state_wait_bits(EventBits_t bits,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all,
                                TickType_t timeout_ticks)
{
  bits &= APP_STATE_USER_BITS_MASK;
  if ((state_event_group == NULL) || (bits == 0U) ||
      (timeout_ticks == portMAX_DELAY))
  {
    return 0U;
  }

  return xEventGroupWaitBits(state_event_group,
                             bits,
                             clear_on_exit,
                             wait_for_all,
                             timeout_ticks);
}

bool app_ipc_self_test(app_ipc_self_test_result_t *result)
{
  static app_event_t event;
  uint32_t index;
  EventBits_t bits;

  if ((result == NULL) || (event_queue == NULL) ||
      (state_event_group == NULL))
  {
    return false;
  }

  result->event_drop_count = 0U;
  result->ring_drop_count = 0U;
  (void)xQueueReset(event_queue);
  app_event_reset_statistics();

  event.type = APP_EVENT_SYSTEM_READY;
  event.source = APP_EVENT_SOURCE_SYSTEM;
  event.payload.generic.value0 = 0x12345678U;
  for (index = 0U; index < APP_EVENT_QUEUE_LENGTH; index++)
  {
    event.payload.generic.value1 = index;
    if (!app_event_publish(&event, 0U))
    {
      return false;
    }
  }
  if (app_event_publish(&event, 0U))
  {
    return false;
  }
  result->event_drop_count = app_event_get_drop_count();

  for (index = 0U; index < APP_EVENT_QUEUE_LENGTH; index++)
  {
    if ((!app_event_receive(&event, 0U)) ||
        (event.type != APP_EVENT_SYSTEM_READY) ||
        (event.source != APP_EVENT_SOURCE_SYSTEM) ||
        (event.payload.generic.value0 != 0x12345678U) ||
        (event.payload.generic.value1 != index) ||
        (event.sequence != (index + 1U)))
    {
      return false;
    }
  }
  if (app_event_receive(&event, 0U))
  {
    return false;
  }

  (void)xEventGroupClearBits(state_event_group, APP_STATE_USER_BITS_MASK);
  bits = app_state_set_bits(APP_STATE_BIT_HWSEC_READY |
                            APP_STATE_BIT_TIME_TRUSTED);
  if ((bits & (APP_STATE_BIT_HWSEC_READY | APP_STATE_BIT_TIME_TRUSTED)) !=
      (APP_STATE_BIT_HWSEC_READY | APP_STATE_BIT_TIME_TRUSTED))
  {
    return false;
  }
  bits = app_state_clear_bits(APP_STATE_BIT_TIME_TRUSTED);
  if ((bits & APP_STATE_BIT_TIME_TRUSTED) == 0U)
  {
    return false;
  }
  if ((app_state_get_bits() & APP_STATE_BIT_TIME_TRUSTED) != 0U)
  {
    return false;
  }
  (void)xEventGroupClearBits(state_event_group, APP_STATE_USER_BITS_MASK);

  if (!static_ring_buffer_self_test(&result->ring_drop_count))
  {
    return false;
  }

  (void)xQueueReset(event_queue);
  app_event_reset_statistics();
  return (result->event_drop_count == 1U) &&
         (result->ring_drop_count == 1U);
}
