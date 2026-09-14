#include "can_service.h"

#include <string.h>

#include "app_event.h"
#include "app_ipc.h"
#include "app_tasks.h"
#include "can.h"
#include "can_protocol.h"
#include "platform_log.h"
#include "project_config.h"
#include "queue.h"
#include "static_ring_buffer.h"
#include "task.h"

#if (PROJECT_CAN_BUSOFF_TEST_ENABLE && \
     (PROJECT_CAN_MODE != PROJECT_CAN_MODE_NORMAL))
#error "Bus-Off test is only valid in normal CAN mode"
#endif

#define CAN_SERVICE_ERROR_LOG_INTERVAL_MS 1000U

static static_ring_buffer_t rx_ring;
static bsp_can_frame_t rx_storage[PROJECT_CAN_RX_RING_CAPACITY];
static StaticQueue_t tx_queue_control;
static uint8_t tx_queue_storage[
  PROJECT_CAN_TX_QUEUE_LENGTH * sizeof(bsp_can_frame_t)];
static QueueHandle_t tx_queue;
static can_service_stats_t statistics;
static volatile bool service_initialized;
static bsp_can_mode_t service_mode;

static volatile bool error_pending;
static volatile bool bus_off_pending;
static volatile bool bus_off_latched;
static volatile uint32_t completed_mailboxes;

static bsp_can_frame_t mailbox_frames[3];
static bool mailbox_frame_valid[3];
static bsp_can_frame_t pending_tx_frame;
static bool pending_tx_valid;
static uint32_t pending_tx_retry_count;
static TickType_t pending_tx_retry_tick;
static TickType_t recovery_next_tick;
static uint32_t recovery_attempts_current;
static TickType_t last_error_log_tick;
static uint32_t diagnostic_frame_log_count;

typedef char can_tx_queue_storage_size_check[
  (sizeof(tx_queue_storage) ==
   (PROJECT_CAN_TX_QUEUE_LENGTH * sizeof(bsp_can_frame_t))) ? 1 : -1];
typedef char can_frame_size_check[
  (sizeof(bsp_can_frame_t) == 20U) ? 1 : -1];

static bsp_can_mode_t can_service_configured_mode(void)
{
  return (PROJECT_CAN_MODE == PROJECT_CAN_MODE_INTERNAL_LOOPBACK) ?
         BSP_CAN_MODE_INTERNAL_LOOPBACK : BSP_CAN_MODE_NORMAL;
}

static void can_service_set_state(can_service_state_t state)
{
  statistics.state = state;
}

static void can_service_publish_event(app_event_type_t type,
                                      uint32_t code,
                                      uint32_t detail)
{
  app_event_t event;
  memset(&event, 0, sizeof(event));
  event.type = type;
  event.source = APP_EVENT_SOURCE_CAN;
  event.payload.error.code = code;
  event.payload.error.detail = detail;
  (void)app_event_publish(&event, 0U);
}

static int32_t can_service_mailbox_index(uint32_t mailbox)
{
  if (mailbox == CAN_TX_MAILBOX0)
  {
    return 0;
  }
  if (mailbox == CAN_TX_MAILBOX1)
  {
    return 1;
  }
  if (mailbox == CAN_TX_MAILBOX2)
  {
    return 2;
  }
  return -1;
}

static void can_service_clear_runtime_objects(void)
{
  memset(&statistics, 0, sizeof(statistics));
  memset(mailbox_frame_valid, 0, sizeof(mailbox_frame_valid));
  error_pending = false;
  bus_off_pending = false;
  bus_off_latched = false;
  completed_mailboxes = 0U;
  pending_tx_valid = false;
  pending_tx_retry_count = 0U;
  pending_tx_retry_tick = 0U;
  recovery_next_tick = 0U;
  recovery_attempts_current = 0U;
  last_error_log_tick = 0U;
  diagnostic_frame_log_count = 0U;
  can_service_set_state(CAN_SERVICE_STATE_UNINITIALIZED);
}

bool can_service_init(void)
{
  bsp_can_status_t status;

  service_initialized = false;
  can_service_clear_runtime_objects();
  if (!static_ring_buffer_init(&rx_ring, rx_storage,
                               sizeof(bsp_can_frame_t),
                               PROJECT_CAN_RX_RING_CAPACITY))
  {
    return false;
  }
  tx_queue = xQueueCreateStatic(PROJECT_CAN_TX_QUEUE_LENGTH,
                                sizeof(bsp_can_frame_t),
                                tx_queue_storage,
                                &tx_queue_control);
  if (tx_queue == NULL)
  {
    return false;
  }
  vQueueAddToRegistry(tx_queue, "CanTxQ");

  service_mode = can_service_configured_mode();
  can_service_set_state(CAN_SERVICE_STATE_STARTING);
  LOG_INFO("CAN", "CAN1 initialization");
  status = bsp_can_init(service_mode);
  if (status != BSP_CAN_STATUS_OK)
  {
    LOG_ERROR("CAN", "CAN init/timing validation failed status=%u",
              (unsigned int)status);
    can_service_set_state(CAN_SERVICE_STATE_FAILED);
    return false;
  }
  LOG_INFO("CAN", "PCLK1: %lu Hz",
           (unsigned long)bsp_can_get_pclk_hz());
  LOG_INFO("CAN", "Bitrate: %lu bit/s",
           (unsigned long)bsp_can_get_bitrate());
  LOG_INFO("CAN", "Sample point: %lu.%lu%%",
           (unsigned long)(bsp_can_get_sample_point_permille() / 10U),
           (unsigned long)(bsp_can_get_sample_point_permille() % 10U));
  LOG_INFO("CAN", "Mode: %s",
           (service_mode == BSP_CAN_MODE_INTERNAL_LOOPBACK) ?
           "INTERNAL LOOPBACK" : "NORMAL");
  if (service_mode == BSP_CAN_MODE_NORMAL)
  {
    LOG_INFO("CAN", "Test request: STD DATA id=0x123 dlc=8");
    LOG_INFO("CAN", "Test data: 11 22 33 44 55 66 77 88");
    LOG_INFO("CAN", "Expected response: STD id=0x321 dlc=8");
  }

  status = bsp_can_configure_filter();
  if (status != BSP_CAN_STATUS_OK)
  {
    LOG_ERROR("CAN", "Filter configuration failed status=%u",
              (unsigned int)status);
    can_service_set_state(CAN_SERVICE_STATE_FAILED);
    return false;
  }
  LOG_INFO("CAN", "Filter: ACCEPT ALL -> FIFO0");
  status = bsp_can_start();
  if (status != BSP_CAN_STATUS_OK)
  {
    LOG_ERROR("CAN", "CAN start failed status=%u", (unsigned int)status);
    can_service_set_state(CAN_SERVICE_STATE_FAILED);
    return false;
  }
  status = bsp_can_activate_notifications();
  if (status != BSP_CAN_STATUS_OK)
  {
    LOG_ERROR("CAN", "Notification activation failed status=%u",
              (unsigned int)status);
    (void)bsp_can_stop();
    can_service_set_state(CAN_SERVICE_STATE_FAILED);
    return false;
  }

  service_initialized = true;
  can_service_set_state(CAN_SERVICE_STATE_ACTIVE);
  LOG_INFO("CAN", "Notifications enabled");
  LOG_INFO("CAN", "CAN state: ACTIVE");
  can_service_publish_event(APP_EVENT_CAN_READY, 0U, 0U);
  if (!can_protocol_start(service_mode))
  {
    statistics.tx_error_count++;
    return false;
  }
  return true;
}

bool can_service_send_async(const bsp_can_frame_t *frame,
                            TickType_t timeout_ticks)
{
  if ((!service_initialized) || (!bsp_can_frame_is_valid(frame)) ||
      (tx_queue == NULL) || (timeout_ticks == portMAX_DELAY))
  {
    return false;
  }
  taskENTER_CRITICAL();
  statistics.tx_request_count++;
  taskEXIT_CRITICAL();
  if (xQueueSend(tx_queue, frame, timeout_ticks) != pdPASS)
  {
    taskENTER_CRITICAL();
    statistics.tx_queue_drop_count++;
    taskEXIT_CRITICAL();
    return false;
  }
  taskENTER_CRITICAL();
  statistics.tx_queued_count++;
  taskEXIT_CRITICAL();
  (void)app_tasks_notify_can();
  return true;
}

bool can_service_send_from_isr(
  const bsp_can_frame_t *frame,
  BaseType_t *higher_priority_task_woken)
{
  UBaseType_t saved_interrupt_status;

  if ((!service_initialized) || (!bsp_can_frame_is_valid(frame)) ||
      (tx_queue == NULL) || (higher_priority_task_woken == NULL))
  {
    return false;
  }
  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  statistics.tx_request_count++;
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  if (xQueueSendFromISR(tx_queue, frame,
                        higher_priority_task_woken) != pdPASS)
  {
    saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
    statistics.tx_queue_drop_count++;
    taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
    return false;
  }
  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  statistics.tx_queued_count++;
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  (void)app_tasks_notify_can_from_isr(higher_priority_task_woken);
  return true;
}

static void can_service_process_rx(void)
{
  bsp_can_frame_t frame;
  can_protocol_result_t result;

  while (static_ring_buffer_pop(&rx_ring, &frame))
  {
    taskENTER_CRITICAL();
    statistics.rx_processed_count++;
    taskEXIT_CRITICAL();
    if (diagnostic_frame_log_count <
        PROJECT_CAN_DIAGNOSTIC_FRAME_LOG_LIMIT)
    {
      diagnostic_frame_log_count++;
      LOG_INFO("CAN", "RX[%lu] %s %s id=0x%08lX dlc=%u",
               (unsigned long)diagnostic_frame_log_count,
               (frame.id_type == BSP_CAN_ID_STANDARD) ? "STD" : "EXT",
               (frame.frame_type == BSP_CAN_FRAME_DATA) ? "DATA" : "RTR",
               (unsigned long)frame.id,
               (unsigned int)frame.dlc);
      LOG_INFO("CAN", "RX data: %02X %02X %02X %02X %02X %02X %02X %02X",
               (unsigned int)frame.data[0],
               (unsigned int)frame.data[1],
               (unsigned int)frame.data[2],
               (unsigned int)frame.data[3],
               (unsigned int)frame.data[4],
               (unsigned int)frame.data[5],
               (unsigned int)frame.data[6],
               (unsigned int)frame.data[7]);
    }
    result = can_protocol_process_frame(&frame);
    if (result == CAN_PROTOCOL_RESULT_INVALID)
    {
      taskENTER_CRITICAL();
      statistics.rx_invalid_count++;
      taskEXIT_CRITICAL();
    }
    else if (result == CAN_PROTOCOL_RESULT_IGNORED)
    {
      taskENTER_CRITICAL();
      statistics.rx_ignored_count++;
      taskEXIT_CRITICAL();
      if (statistics.rx_ignored_count <=
          PROJECT_CAN_DIAGNOSTIC_FRAME_LOG_LIMIT)
      {
        LOG_WARN("CAN", "RX ignored: expected STD DATA 0x123 DLC8 exact test data");
      }
    }
  }
}

static void can_service_process_tx_completions(void)
{
  uint32_t completed;
  uint32_t mask;
  uint32_t index;

  taskENTER_CRITICAL();
  completed = completed_mailboxes;
  completed_mailboxes = 0U;
  taskEXIT_CRITICAL();
  for (index = 0U; index < 3U; index++)
  {
    mask = (1UL << index);
    if (((completed & mask) != 0U) && mailbox_frame_valid[index])
    {
      can_protocol_tx_completed(&mailbox_frames[index]);
      mailbox_frame_valid[index] = false;
    }
  }
}

static void can_service_process_tx(void)
{
  bsp_can_status_t status;
  uint32_t mailbox = 0U;
  int32_t mailbox_index;
  TickType_t now = xTaskGetTickCount();

  can_service_process_tx_completions();
  if (statistics.state != CAN_SERVICE_STATE_ACTIVE)
  {
    return;
  }
  if (!pending_tx_valid)
  {
    if (xQueueReceive(tx_queue, &pending_tx_frame, 0U) != pdPASS)
    {
      return;
    }
    pending_tx_valid = true;
    pending_tx_retry_count = 0U;
    pending_tx_retry_tick = now;
  }
  if ((int32_t)(now - pending_tx_retry_tick) < 0)
  {
    return;
  }

  taskENTER_CRITICAL();
  status = bsp_can_send(&pending_tx_frame, &mailbox);
  if (status == BSP_CAN_STATUS_OK)
  {
    mailbox_index = can_service_mailbox_index(mailbox);
    if (mailbox_index >= 0)
    {
      mailbox_frames[mailbox_index] = pending_tx_frame;
      mailbox_frame_valid[mailbox_index] = true;
    }
  }
  taskEXIT_CRITICAL();

  if (status == BSP_CAN_STATUS_OK)
  {
    statistics.tx_submit_count++;
    pending_tx_valid = false;
    if (uxQueueMessagesWaiting(tx_queue) > 0U)
    {
      (void)app_tasks_notify_can();
    }
    return;
  }
  if (status == BSP_CAN_STATUS_BUSY)
  {
    statistics.tx_busy_count++;
    pending_tx_retry_count++;
    if (pending_tx_retry_count <= PROJECT_CAN_TX_RETRY_MAX)
    {
      pending_tx_retry_tick = now +
                              pdMS_TO_TICKS(PROJECT_CAN_TX_RETRY_DELAY_MS);
      return;
    }
  }
  statistics.tx_error_count++;
  LOG_WARN("CAN", "TX submit failed status=%u hal=0x%08lX",
           (unsigned int)status,
           (unsigned long)bsp_can_get_hal_error());
  pending_tx_valid = false;
}

static void can_service_enter_bus_off(void)
{
  if (statistics.state == CAN_SERVICE_STATE_BUS_OFF ||
      statistics.state == CAN_SERVICE_STATE_RECOVERING)
  {
    return;
  }
  can_service_set_state(CAN_SERVICE_STATE_BUS_OFF);
  recovery_next_tick = xTaskGetTickCount() +
                       pdMS_TO_TICKS(PROJECT_CAN_RECOVERY_TIMEOUT_MS);
  recovery_attempts_current = 0U;
  LOG_WARN("CAN", "Bus-Off detected");
  can_service_publish_event(APP_EVENT_CAN_BUS_OFF,
                            statistics.last_hal_error, 0U);
}

static void can_service_recovery_succeeded(bool restarted)
{
  (void)bsp_can_reset_hal_error();
  taskENTER_CRITICAL();
  bus_off_pending = false;
  bus_off_latched = false;
  taskEXIT_CRITICAL();
  recovery_attempts_current = 0U;
  statistics.recovery_success_count++;
  can_service_set_state(CAN_SERVICE_STATE_ACTIVE);
  can_service_publish_event(APP_EVENT_CAN_RECOVERED, 0U,
                            restarted ? 1U : 0U);
  if (restarted)
  {
    LOG_INFO("CAN", "CAN restarted");
  }
  LOG_INFO("CAN", "CAN state: ACTIVE");
  LOG_INFO("CAN", "CAN bus-off recovery: PASS");
}

static void can_service_process_recovery(void)
{
  static const uint16_t retry_backoff_ms[3] = {100U, 200U, 500U};
  TickType_t now;
  bsp_can_status_t status;
  bool pending;

  taskENTER_CRITICAL();
  pending = bus_off_pending;
  bus_off_pending = false;
  taskEXIT_CRITICAL();
  if (pending)
  {
    can_service_enter_bus_off();
  }
  if ((statistics.state != CAN_SERVICE_STATE_BUS_OFF) &&
      (statistics.state != CAN_SERVICE_STATE_RECOVERING))
  {
    return;
  }
  if (!bsp_can_is_bus_off())
  {
    can_service_recovery_succeeded(false);
    return;
  }

  now = xTaskGetTickCount();
  if ((int32_t)(now - recovery_next_tick) < 0)
  {
    return;
  }
  if (recovery_attempts_current >= PROJECT_CAN_RECOVERY_MAX_ATTEMPTS)
  {
    can_service_set_state(CAN_SERVICE_STATE_FAILED);
    LOG_ERROR("CAN", "Bus-Off recovery failed after %u attempts",
              (unsigned int)PROJECT_CAN_RECOVERY_MAX_ATTEMPTS);
    return;
  }

  can_service_set_state(CAN_SERVICE_STATE_RECOVERING);
  recovery_attempts_current++;
  statistics.recovery_attempt_count++;
  LOG_INFO("CAN", "Recovery attempt: %lu",
           (unsigned long)recovery_attempts_current);
  status = bsp_can_restart(service_mode);
  if (status == BSP_CAN_STATUS_OK)
  {
    can_service_recovery_succeeded(true);
    return;
  }
  statistics.recovery_failure_count++;
  can_service_set_state(CAN_SERVICE_STATE_BUS_OFF);
  recovery_next_tick = now + pdMS_TO_TICKS(
    retry_backoff_ms[recovery_attempts_current - 1U]);
}

static void can_service_process_errors(void)
{
  bool pending;
  TickType_t now;

  taskENTER_CRITICAL();
  pending = error_pending;
  error_pending = false;
  taskEXIT_CRITICAL();
  if (!pending)
  {
    return;
  }
  if ((statistics.last_hal_error & HAL_CAN_ERROR_BOF) != 0U)
  {
    can_service_enter_bus_off();
    return;
  }
  now = xTaskGetTickCount();
  if ((last_error_log_tick == 0U) ||
      ((now - last_error_log_tick) >=
       pdMS_TO_TICKS(CAN_SERVICE_ERROR_LOG_INTERVAL_MS)))
  {
    last_error_log_tick = now;
    LOG_WARN("CAN", "HAL error=0x%08lX",
             (unsigned long)statistics.last_hal_error);
    can_service_publish_event(APP_EVENT_CAN_ERROR,
                              statistics.last_hal_error, 0U);
  }
  (void)bsp_can_reset_hal_error();
}

void can_service_process(void)
{
  if (!service_initialized)
  {
    return;
  }
  can_service_process_rx();
  can_service_process_errors();
  can_service_process_recovery();
  can_service_process_tx();
  can_protocol_poll();
}

bool can_service_get_stats(can_service_stats_t *snapshot)
{
  if (snapshot == NULL)
  {
    return false;
  }
  /* 24 aligned word loads form a short, bounded snapshot on Cortex-M4. */
  taskENTER_CRITICAL();
  *snapshot = statistics;
  taskEXIT_CRITICAL();
  return true;
}

size_t can_service_get_rx_ring_high_water_mark(void)
{
  return static_ring_buffer_high_water_mark(&rx_ring);
}

size_t can_service_get_rx_ring_capacity(void)
{
  return static_ring_buffer_capacity(&rx_ring);
}

const char *can_service_state_text(can_service_state_t state)
{
  switch (state)
  {
    case CAN_SERVICE_STATE_UNINITIALIZED: return "UNINIT";
    case CAN_SERVICE_STATE_STOPPED: return "STOPPED";
    case CAN_SERVICE_STATE_STARTING: return "STARTING";
    case CAN_SERVICE_STATE_ACTIVE: return "ACTIVE";
    case CAN_SERVICE_STATE_BUS_OFF: return "BUS_OFF";
    case CAN_SERVICE_STATE_RECOVERING: return "RECOVERING";
    case CAN_SERVICE_STATE_FAILED: return "FAILED";
    default: return "INVALID";
  }
}

static void can_service_tx_complete_from_isr(uint32_t mailbox_mask)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  UBaseType_t saved_interrupt_status;

  if (!service_initialized)
  {
    return;
  }
  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  statistics.tx_complete_count++;
  completed_mailboxes |= mailbox_mask;
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  (void)app_tasks_notify_can_from_isr(&higher_priority_task_woken);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  CAN_RxHeaderTypeDef header;
  bsp_can_frame_t frame;
  uint32_t drained = 0U;
  bool queued_any = false;

  if ((!service_initialized) || (hcan != &hcan1))
  {
    return;
  }
  statistics.rx_irq_count++;
  while ((HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U) &&
         (drained < PROJECT_CAN_ISR_MAX_DRAIN))
  {
    memset(&header, 0, sizeof(header));
    memset(&frame, 0, sizeof(frame));
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0,
                             &header, frame.data) != HAL_OK)
    {
      statistics.rx_invalid_count++;
      break;
    }
    statistics.rx_hal_count++;
    drained++;
    if ((header.DLC > 8U) ||
        ((header.IDE != CAN_ID_STD) && (header.IDE != CAN_ID_EXT)) ||
        ((header.RTR != CAN_RTR_DATA) && (header.RTR != CAN_RTR_REMOTE)))
    {
      statistics.rx_invalid_count++;
      continue;
    }
    frame.id = (header.IDE == CAN_ID_STD) ? header.StdId : header.ExtId;
    frame.id_type = (header.IDE == CAN_ID_STD) ?
                    BSP_CAN_ID_STANDARD : BSP_CAN_ID_EXTENDED;
    frame.frame_type = (header.RTR == CAN_RTR_DATA) ?
                       BSP_CAN_FRAME_DATA : BSP_CAN_FRAME_REMOTE;
    frame.dlc = (uint8_t)header.DLC;
    frame.rx_tick = xTaskGetTickCountFromISR();
    if (static_ring_buffer_push_from_isr(&rx_ring, &frame))
    {
      statistics.rx_queued_count++;
      queued_any = true;
    }
    else
    {
      statistics.rx_ring_overflow_count++;
    }
  }
  if (queued_any)
  {
    (void)app_tasks_notify_can_from_isr(&higher_priority_task_woken);
  }
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void HAL_CAN_TxMailbox0CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if (hcan == &hcan1)
  {
    can_service_tx_complete_from_isr(CAN_TX_MAILBOX0);
  }
}

void HAL_CAN_TxMailbox1CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if (hcan == &hcan1)
  {
    can_service_tx_complete_from_isr(CAN_TX_MAILBOX1);
  }
}

void HAL_CAN_TxMailbox2CompleteCallback(CAN_HandleTypeDef *hcan)
{
  if (hcan == &hcan1)
  {
    can_service_tx_complete_from_isr(CAN_TX_MAILBOX2);
  }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  uint32_t error;

  if ((!service_initialized) || (hcan != &hcan1))
  {
    return;
  }
  error = HAL_CAN_GetError(hcan);
  statistics.error_irq_count++;
  statistics.last_hal_error = error;
  if ((error & HAL_CAN_ERROR_EWG) != 0U)
  {
    statistics.warning_count++;
  }
  if ((error & HAL_CAN_ERROR_EPV) != 0U)
  {
    statistics.passive_count++;
  }
  if ((error & HAL_CAN_ERROR_RX_FOV0) != 0U)
  {
    statistics.rx_fifo_overrun_count++;
  }
  if (((error & HAL_CAN_ERROR_BOF) != 0U) && (!bus_off_latched))
  {
    bus_off_latched = true;
    bus_off_pending = true;
    statistics.bus_off_count++;
  }
  error_pending = true;
  (void)app_tasks_notify_can_from_isr(&higher_priority_task_woken);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}
