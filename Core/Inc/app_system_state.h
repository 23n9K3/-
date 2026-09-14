#ifndef APP_SYSTEM_STATE_H
#define APP_SYSTEM_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "app_event.h"

typedef struct
{
  app_event_type_t last_event;
  app_event_source_t last_source;
  TickType_t last_event_tick;
  uint32_t processed_event_count;
  uint32_t event_error_count;
  uint32_t can_error_count;
  uint32_t network_error_count;
  uint32_t security_error_count;
  uint32_t mutex_error_count;
} app_system_state_snapshot_t;

bool app_system_state_init(void);
bool app_system_state_process_event(const app_event_t *event,
                                    TickType_t timeout_ticks);
bool app_system_state_get_snapshot(app_system_state_snapshot_t *snapshot,
                                   TickType_t timeout_ticks);
bool app_system_state_self_test(void);

#endif /* APP_SYSTEM_STATE_H */
