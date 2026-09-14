#ifndef APP_IPC_H
#define APP_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "event_groups.h"
#include "app_event.h"

#define APP_EVENT_QUEUE_LENGTH 16U

#define APP_STATE_BIT_HWSEC_READY    ((EventBits_t)(1UL << 0))
#define APP_STATE_BIT_TIME_TRUSTED   ((EventBits_t)(1UL << 1))
#define APP_STATE_BIT_CAN_READY      ((EventBits_t)(1UL << 2))
#define APP_STATE_BIT_NET_LINK_UP    ((EventBits_t)(1UL << 3))
#define APP_STATE_BIT_IP_READY       ((EventBits_t)(1UL << 4))
#define APP_STATE_BIT_TLS_CONNECTED  ((EventBits_t)(1UL << 5))
#define APP_STATE_BIT_DEGRADED       ((EventBits_t)(1UL << 6))
#define APP_STATE_BIT_FATAL_ERROR    ((EventBits_t)(1UL << 7))
#define APP_STATE_BIT_ENC_READY      ((EventBits_t)(1UL << 8))
#define APP_STATE_BIT_SERVICES_READY ((EventBits_t)(1UL << 9))
#define APP_STATE_USER_BITS_MASK     ((EventBits_t)0x3FFUL)

typedef struct
{
  uint32_t event_drop_count;
  uint32_t ring_drop_count;
} app_ipc_self_test_result_t;

bool app_ipc_init(void);
bool app_ipc_self_test(app_ipc_self_test_result_t *result);

bool app_event_publish(const app_event_t *event, TickType_t timeout_ticks);
bool app_event_publish_from_isr(const app_event_t *event,
                                BaseType_t *higher_priority_task_woken);
bool app_event_receive(app_event_t *event, TickType_t timeout_ticks);
uint32_t app_event_get_publish_count(void);
uint32_t app_event_get_receive_count(void);
uint32_t app_event_get_drop_count(void);
UBaseType_t app_event_get_waiting_count(void);
UBaseType_t app_event_get_high_water_mark(void);

EventBits_t app_state_set_bits(EventBits_t bits);
EventBits_t app_state_clear_bits(EventBits_t bits);
EventBits_t app_state_get_bits(void);
EventBits_t app_state_wait_bits(EventBits_t bits,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all,
                                TickType_t timeout_ticks);

#endif /* APP_IPC_H */
