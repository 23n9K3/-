#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_can.h"

typedef enum
{
  CAN_SERVICE_STATE_UNINITIALIZED = 0,
  CAN_SERVICE_STATE_STOPPED,
  CAN_SERVICE_STATE_STARTING,
  CAN_SERVICE_STATE_ACTIVE,
  CAN_SERVICE_STATE_BUS_OFF,
  CAN_SERVICE_STATE_RECOVERING,
  CAN_SERVICE_STATE_FAILED
} can_service_state_t;

typedef struct
{
  uint32_t rx_irq_count;
  uint32_t rx_hal_count;
  uint32_t rx_queued_count;
  uint32_t rx_processed_count;
  uint32_t rx_invalid_count;
  uint32_t rx_ignored_count;
  uint32_t rx_ring_overflow_count;
  uint32_t rx_fifo_overrun_count;
  uint32_t tx_request_count;
  uint32_t tx_queued_count;
  uint32_t tx_submit_count;
  uint32_t tx_complete_count;
  uint32_t tx_busy_count;
  uint32_t tx_error_count;
  uint32_t tx_queue_drop_count;
  uint32_t error_irq_count;
  uint32_t warning_count;
  uint32_t passive_count;
  uint32_t bus_off_count;
  uint32_t recovery_attempt_count;
  uint32_t recovery_success_count;
  uint32_t recovery_failure_count;
  uint32_t last_hal_error;
  can_service_state_t state;
} can_service_stats_t;

bool can_service_init(void);
void can_service_process(void);
bool can_service_send_async(const bsp_can_frame_t *frame,
                            TickType_t timeout_ticks);
bool can_service_send_from_isr(
  const bsp_can_frame_t *frame,
  BaseType_t *higher_priority_task_woken);
bool can_service_get_stats(can_service_stats_t *snapshot);
size_t can_service_get_rx_ring_high_water_mark(void);
size_t can_service_get_rx_ring_capacity(void);
const char *can_service_state_text(can_service_state_t state);

#endif /* CAN_SERVICE_H */
