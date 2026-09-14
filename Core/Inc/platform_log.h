#ifndef PLATFORM_LOG_H
#define PLATFORM_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "project_config.h"

typedef enum
{
  PLATFORM_LOG_LEVEL_ERROR = 0,
  PLATFORM_LOG_LEVEL_WARN,
  PLATFORM_LOG_LEVEL_INFO,
  PLATFORM_LOG_LEVEL_DEBUG,
  PLATFORM_LOG_LEVEL_NONE
} platform_log_level_t;

bool platform_log_init(void);
bool platform_log_queue_init(void);
bool platform_log_queue_self_test(uint32_t *test_drop_count);
bool platform_log_process_next(TickType_t timeout_ticks);

void platform_log_write(platform_log_level_t level,
                        const char *module,
                        const char *format,
                        ...);

void platform_log_raw(const char *text);

uint32_t platform_log_get_tx_error_count(void);
uint32_t platform_log_get_queued_count(void);
uint32_t platform_log_get_sent_count(void);
uint32_t platform_log_get_drop_count(void);
uint32_t platform_log_get_truncated_count(void);
uint32_t platform_log_get_uart_error_count(void);
UBaseType_t platform_log_get_queue_waiting(void);
UBaseType_t platform_log_get_queue_high_water_mark(void);

#if (PROJECT_LOG_LEVEL != PROJECT_LOG_LEVEL_NONE_VALUE) && \
    (PROJECT_LOG_LEVEL >= PROJECT_LOG_LEVEL_ERROR_VALUE)
#define LOG_ERROR(module, ...) \
  platform_log_write(PLATFORM_LOG_LEVEL_ERROR, (module), __VA_ARGS__)
#else
#define LOG_ERROR(module, ...) ((void)0)
#endif

#if (PROJECT_LOG_LEVEL != PROJECT_LOG_LEVEL_NONE_VALUE) && \
    (PROJECT_LOG_LEVEL >= PROJECT_LOG_LEVEL_WARN_VALUE)
#define LOG_WARN(module, ...) \
  platform_log_write(PLATFORM_LOG_LEVEL_WARN, (module), __VA_ARGS__)
#else
#define LOG_WARN(module, ...) ((void)0)
#endif

#if (PROJECT_LOG_LEVEL != PROJECT_LOG_LEVEL_NONE_VALUE) && \
    (PROJECT_LOG_LEVEL >= PROJECT_LOG_LEVEL_INFO_VALUE)
#define LOG_INFO(module, ...) \
  platform_log_write(PLATFORM_LOG_LEVEL_INFO, (module), __VA_ARGS__)
#else
#define LOG_INFO(module, ...) ((void)0)
#endif

#if (PROJECT_LOG_LEVEL != PROJECT_LOG_LEVEL_NONE_VALUE) && \
    (PROJECT_LOG_LEVEL >= PROJECT_LOG_LEVEL_DEBUG_VALUE)
#define LOG_DEBUG(module, ...) \
  platform_log_write(PLATFORM_LOG_LEVEL_DEBUG, (module), __VA_ARGS__)
#else
#define LOG_DEBUG(module, ...) ((void)0)
#endif

#endif /* PLATFORM_LOG_H */
