#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define SECURITY_NOTIFY_START    (1UL << 0)
#define SECURITY_NOTIFY_RX_READY (1UL << 1)
#define SECURITY_NOTIFY_TIMEOUT  (1UL << 2)
#define SECURITY_NOTIFY_STOP     (1UL << 3)
#define SECURITY_NOTIFY_TIME_STATE (1UL << 4)
#define SECURITY_NOTIFY_ALL      (SECURITY_NOTIFY_START | \
                                  SECURITY_NOTIFY_RX_READY | \
                                  SECURITY_NOTIFY_TIMEOUT | \
                                  SECURITY_NOTIFY_STOP | \
                                  SECURITY_NOTIFY_TIME_STATE)

bool app_tasks_init(void);
TaskHandle_t app_tasks_get_can_task_handle(void);
TaskHandle_t app_tasks_get_security_task_handle(void);
TaskHandle_t app_tasks_get_net_task_handle(void);
TaskHandle_t app_tasks_get_netif_task_handle(void);
bool app_tasks_notify_can(void);
bool app_tasks_notify_can_from_isr(BaseType_t *higher_priority_task_woken);
bool app_tasks_notify_net_from_isr(BaseType_t *higher_priority_task_woken);
bool app_tasks_notify_netif(void);
bool app_tasks_notify_netif_from_isr(BaseType_t *higher_priority_task_woken);
bool app_tasks_notify_security(uint32_t notification_bits);
bool app_tasks_notify_security_from_isr(
  uint32_t notification_bits,
  BaseType_t *higher_priority_task_woken);

#endif /* APP_TASKS_H */
