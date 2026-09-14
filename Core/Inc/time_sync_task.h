#ifndef TIME_SYNC_TASK_H
#define TIME_SYNC_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define TIME_SYNC_NOTIFY_NETWORK_UP  (1UL << 0)
#define TIME_SYNC_NOTIFY_NETWORK_DOWN (1UL << 1)
#define TIME_SYNC_NOTIFY_DNS_RESULT  (1UL << 2)
#define TIME_SYNC_NOTIFY_SNTP_SAMPLE (1UL << 3)

bool time_sync_task_create(void);
TaskHandle_t time_sync_task_get_handle(void);
void time_sync_task_signal(uint32_t notification_bits);
void time_sync_task_network_state_from_tcpip(bool network_ready);

#endif /* TIME_SYNC_TASK_H */
