#include "platform_log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_debug_uart.h"
#include "platform_time.h"
#include "queue.h"
#include "task.h"

typedef struct
{
  char *data;
  size_t capacity;
  size_t length;
  bool truncated;
} log_builder_t;

typedef struct
{
  uint32_t timestamp_ms;
  uint16_t length;
  uint8_t level;
  uint8_t reserved;
  char text[PROJECT_LOG_MESSAGE_MAX_LENGTH];
} platform_log_record_t;

static StaticQueue_t log_queue_control;
static uint8_t log_queue_storage[
  PROJECT_LOG_QUEUE_LENGTH * sizeof(platform_log_record_t)];
static QueueHandle_t log_queue;
static bool log_is_initialized;
static uint32_t queued_count;
static uint32_t sent_count;
static uint32_t drop_count;
static uint32_t truncated_count;
static UBaseType_t queue_high_water_mark;

typedef char platform_log_record_size_check[
  (sizeof(platform_log_record_t) == PROJECT_LOG_RECORD_SIZE) ? 1 : -1];

static bool platform_log_scheduler_active(void)
{
  return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

static void platform_log_increment(uint32_t *counter)
{
  if (platform_log_scheduler_active())
  {
    taskENTER_CRITICAL();
    (*counter)++;
    taskEXIT_CRITICAL();
  }
  else
  {
    (*counter)++;
  }
}

static void platform_log_reset_statistics(void)
{
  queued_count = 0U;
  sent_count = 0U;
  drop_count = 0U;
  truncated_count = 0U;
  queue_high_water_mark = 0U;
}

static const char *platform_log_level_text(platform_log_level_t level)
{
  static const char *const level_text[] =
  {
    "ERROR", "WARN ", "INFO ", "DEBUG"
  };

  if ((uint32_t)level > (uint32_t)PLATFORM_LOG_LEVEL_DEBUG)
  {
    return NULL;
  }
  return level_text[(uint32_t)level];
}

static void log_builder_append_char(log_builder_t *builder, char character)
{
  if (builder->length < builder->capacity)
  {
    builder->data[builder->length++] = character;
  }
  else
  {
    builder->truncated = true;
  }
}

static void log_builder_append_text(log_builder_t *builder, const char *text)
{
  while (*text != '\0')
  {
    log_builder_append_char(builder, *text++);
  }
}

static void log_builder_append_unsigned(log_builder_t *builder,
                                        unsigned long value,
                                        uint32_t base,
                                        bool uppercase,
                                        size_t minimum_width,
                                        char padding)
{
  static const char lowercase_digits[] = "0123456789abcdef";
  static const char uppercase_digits[] = "0123456789ABCDEF";
  const char *digits_table = uppercase ? uppercase_digits : lowercase_digits;
  char digits[3U * sizeof(unsigned long)];
  size_t digit_count = 0U;

  do
  {
    digits[digit_count++] = digits_table[value % base];
    value /= base;
  }
  while (value != 0U);

  while (digit_count < minimum_width)
  {
    log_builder_append_char(builder, padding);
    minimum_width--;
  }
  while (digit_count > 0U)
  {
    log_builder_append_char(builder, digits[--digit_count]);
  }
}

static void log_builder_append_signed(log_builder_t *builder,
                                      long value,
                                      size_t minimum_width,
                                      char padding)
{
  unsigned long magnitude;

  if (value < 0)
  {
    log_builder_append_char(builder, '-');
    if (minimum_width > 0U)
    {
      minimum_width--;
    }
    magnitude = (unsigned long)(-(value + 1L)) + 1UL;
  }
  else
  {
    magnitude = (unsigned long)value;
  }
  log_builder_append_unsigned(builder, magnitude, 10U, false,
                              minimum_width, padding);
}

static bool platform_log_format(log_builder_t *builder,
                                const char *format,
                                va_list arguments)
{
  const char *cursor = format;

  while (*cursor != '\0')
  {
    bool long_argument = false;
    char padding = ' ';
    size_t minimum_width = 0U;

    if (*cursor != '%')
    {
      log_builder_append_char(builder, *cursor++);
      continue;
    }
    cursor++;
    if (*cursor == '%')
    {
      log_builder_append_char(builder, '%');
      cursor++;
      continue;
    }
    if (*cursor == '0')
    {
      padding = '0';
      cursor++;
    }
    while ((*cursor >= '0') && (*cursor <= '9'))
    {
      if (minimum_width < PROJECT_LOG_MESSAGE_MAX_LENGTH)
      {
        minimum_width = (minimum_width * 10U) + (size_t)(*cursor - '0');
      }
      else
      {
        builder->truncated = true;
      }
      cursor++;
    }
    if (minimum_width > PROJECT_LOG_MESSAGE_MAX_LENGTH)
    {
      minimum_width = PROJECT_LOG_MESSAGE_MAX_LENGTH;
      builder->truncated = true;
    }
    if (*cursor == 'l')
    {
      long_argument = true;
      cursor++;
    }

    switch (*cursor)
    {
      case 's':
      {
        const char *text = va_arg(arguments, const char *);
        log_builder_append_text(builder, (text != NULL) ? text : "(null)");
        break;
      }
      case 'c':
        log_builder_append_char(builder, (char)va_arg(arguments, int));
        break;
      case 'd':
        log_builder_append_signed(builder,
          long_argument ? va_arg(arguments, long) : (long)va_arg(arguments, int),
          minimum_width, padding);
        break;
      case 'u':
        log_builder_append_unsigned(builder,
          long_argument ? va_arg(arguments, unsigned long) :
                          (unsigned long)va_arg(arguments, unsigned int),
          10U, false, minimum_width, padding);
        break;
      case 'x':
      case 'X':
        log_builder_append_unsigned(builder,
          long_argument ? va_arg(arguments, unsigned long) :
                          (unsigned long)va_arg(arguments, unsigned int),
          16U, (*cursor == 'X'), minimum_width, padding);
        break;
      case 'p':
        log_builder_append_text(builder, "0x");
        log_builder_append_unsigned(builder,
          (unsigned long)(uintptr_t)va_arg(arguments, void *),
          16U, false, 2U * sizeof(void *), '0');
        break;
      default:
        return false;
    }
    cursor++;
  }
  return true;
}

static bool platform_log_enqueue(platform_log_record_t *record)
{
  UBaseType_t waiting;

  if ((log_queue == NULL) ||
      (xQueueSend(log_queue, record, 0U) != pdPASS))
  {
    platform_log_increment(&drop_count);
    return false;
  }

  platform_log_increment(&queued_count);
  waiting = uxQueueMessagesWaiting(log_queue);
  taskENTER_CRITICAL();
  if (waiting > queue_high_water_mark)
  {
    queue_high_water_mark = waiting;
  }
  taskEXIT_CRITICAL();
  return true;
}

bool platform_log_init(void)
{
  log_queue = NULL;
  platform_log_reset_statistics();
  log_is_initialized = bsp_debug_uart_init();
  return log_is_initialized;
}

bool platform_log_queue_init(void)
{
  if (!log_is_initialized)
  {
    return false;
  }

  log_queue = xQueueCreateStatic(PROJECT_LOG_QUEUE_LENGTH,
                                 sizeof(platform_log_record_t),
                                 log_queue_storage,
                                 &log_queue_control);
  if (log_queue == NULL)
  {
    return false;
  }
  platform_log_reset_statistics();
  vQueueAddToRegistry(log_queue, "PlatformLogQ");
  return true;
}

void platform_log_write(platform_log_level_t level,
                        const char *module,
                        const char *format,
                        ...)
{
  const char *level_text;
  platform_log_record_t record;
  log_builder_t builder;
  size_t module_length = 0U;
  size_t message_start;
  va_list arguments;

  if ((!log_is_initialized) || (module == NULL) || (module[0] == '\0') ||
      (format == NULL) ||
      ((uint32_t)level > (uint32_t)PLATFORM_LOG_LEVEL_DEBUG) ||
      (PROJECT_LOG_LEVEL == PROJECT_LOG_LEVEL_NONE_VALUE) ||
      ((uint32_t)level > PROJECT_LOG_LEVEL))
  {
    return;
  }

  level_text = platform_log_level_text(level);
  if (level_text == NULL)
  {
    return;
  }

  record.timestamp_ms = platform_time_get_ms();
  record.level = (uint8_t)level;
  record.reserved = 0U;
  builder.data = record.text;
  builder.capacity = sizeof(record.text) - 3U;
  builder.length = 0U;
  builder.truncated = false;

  log_builder_append_char(&builder, '[');
  log_builder_append_unsigned(&builder, (unsigned long)record.timestamp_ms,
                              10U, false, 10U, '0');
  log_builder_append_text(&builder, "][");
  log_builder_append_text(&builder, level_text);
  log_builder_append_text(&builder, "][");
  while ((module[module_length] != '\0') &&
         (module_length < PROJECT_LOG_MODULE_MAX_LENGTH))
  {
    log_builder_append_char(&builder, module[module_length++]);
  }
  while (module_length++ < 4U)
  {
    log_builder_append_char(&builder, ' ');
  }
  log_builder_append_text(&builder, "] ");
  message_start = builder.length;

  va_start(arguments, format);
  if (!platform_log_format(&builder, format, arguments))
  {
    va_end(arguments);
    return;
  }
  va_end(arguments);

  while ((builder.length > message_start) &&
         ((record.text[builder.length - 1U] == '\r') ||
          (record.text[builder.length - 1U] == '\n')))
  {
    builder.length--;
  }
  if (builder.truncated)
  {
    platform_log_increment(&truncated_count);
  }
  record.text[builder.length++] = '\r';
  record.text[builder.length++] = '\n';
  record.text[builder.length] = '\0';
  record.length = (uint16_t)builder.length;

  if (platform_log_scheduler_active())
  {
    (void)platform_log_enqueue(&record);
  }
  else if (bsp_debug_uart_write((const uint8_t *)record.text,
                                record.length,
                                PROJECT_UART_TX_TIMEOUT_MS))
  {
    platform_log_increment(&sent_count);
  }
}

void platform_log_raw(const char *text)
{
  platform_log_record_t record;
  size_t length = 0U;

  if ((!log_is_initialized) || (text == NULL))
  {
    return;
  }
  if (!platform_log_scheduler_active())
  {
    if (bsp_debug_uart_write_string(text, PROJECT_UART_TX_TIMEOUT_MS))
    {
      platform_log_increment(&sent_count);
    }
    return;
  }

  while ((text[length] != '\0') && (length < (sizeof(record.text) - 1U)))
  {
    record.text[length] = text[length];
    length++;
  }
  if (text[length] != '\0')
  {
    platform_log_increment(&truncated_count);
  }
  record.text[length] = '\0';
  record.timestamp_ms = platform_time_get_ms();
  record.length = (uint16_t)length;
  record.level = (uint8_t)PLATFORM_LOG_LEVEL_INFO;
  record.reserved = 0U;
  (void)platform_log_enqueue(&record);
}

bool platform_log_process_next(TickType_t timeout_ticks)
{
  platform_log_record_t record;

  if ((log_queue == NULL) ||
      (xQueueReceive(log_queue, &record, timeout_ticks) != pdPASS))
  {
    return false;
  }
  if ((record.length == 0U) || (record.length >= sizeof(record.text)))
  {
    platform_log_increment(&drop_count);
    return false;
  }
  if (!bsp_debug_uart_write((const uint8_t *)record.text,
                            record.length,
                            PROJECT_UART_TX_TIMEOUT_MS))
  {
    return false;
  }
  platform_log_increment(&sent_count);
  return true;
}

bool platform_log_queue_self_test(uint32_t *test_drop_count)
{
  static platform_log_record_t record;
  uint32_t index;

  if ((test_drop_count == NULL) || (log_queue == NULL))
  {
    return false;
  }
  *test_drop_count = 0U;
  (void)xQueueReset(log_queue);
  for (index = 0U; index < PROJECT_LOG_QUEUE_LENGTH; index++)
  {
    record.timestamp_ms = index;
    record.length = 1U;
    record.level = (uint8_t)PLATFORM_LOG_LEVEL_INFO;
    record.reserved = 0U;
    record.text[0] = 'T';
    record.text[1] = '\0';
    if (xQueueSend(log_queue, &record, 0U) != pdPASS)
    {
      return false;
    }
  }
  if (xQueueSend(log_queue, &record, 0U) == pdPASS)
  {
    return false;
  }
  *test_drop_count = 1U;
  for (index = 0U; index < PROJECT_LOG_QUEUE_LENGTH; index++)
  {
    if ((xQueueReceive(log_queue, &record, 0U) != pdPASS) ||
        (record.timestamp_ms != index) ||
        (record.length != 1U) || (record.text[0] != 'T'))
    {
      return false;
    }
  }
  if (uxQueueMessagesWaiting(log_queue) != 0U)
  {
    return false;
  }
  (void)xQueueReset(log_queue);
  platform_log_reset_statistics();
  return true;
}

uint32_t platform_log_get_queued_count(void) { return queued_count; }
uint32_t platform_log_get_sent_count(void) { return sent_count; }
uint32_t platform_log_get_drop_count(void) { return drop_count; }
uint32_t platform_log_get_truncated_count(void) { return truncated_count; }
uint32_t platform_log_get_uart_error_count(void)
{
  return bsp_debug_uart_get_error_count();
}
uint32_t platform_log_get_tx_error_count(void)
{
  return platform_log_get_uart_error_count();
}
UBaseType_t platform_log_get_queue_waiting(void)
{
  return (log_queue != NULL) ? uxQueueMessagesWaiting(log_queue) : 0U;
}
UBaseType_t platform_log_get_queue_high_water_mark(void)
{
  return queue_high_water_mark;
}
