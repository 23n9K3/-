#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

#include "bsp_led.h"

typedef enum
{
  FREERTOS_FAULT_NONE = 0,
  FREERTOS_FAULT_STACK_OVERFLOW,
  FREERTOS_FAULT_MALLOC_FAILED
} freertos_fault_reason_t;

typedef struct
{
  TaskHandle_t task_handle;
  const char *task_name;
  uint32_t reason;
} freertos_fault_record_t;

static volatile freertos_fault_record_t freertos_fault_record;

static void freertos_fault_stop(void)
{
  taskDISABLE_INTERRUPTS();
  bsp_led_on();
  for (;;)
  {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task,
                                   char *task_name)
{
  freertos_fault_record.task_handle = task;
  freertos_fault_record.task_name = task_name;
  freertos_fault_record.reason = (uint32_t)FREERTOS_FAULT_STACK_OVERFLOW;

  freertos_fault_stop();
}

void vApplicationMallocFailedHook(void)
{
  /* The project is completely static, so normal FreeRTOS operation must never
   * reach this hook.  It remains enabled as a defensive integration check. */
  freertos_fault_record.task_handle = NULL;
  freertos_fault_record.task_name = NULL;
  freertos_fault_record.reason = (uint32_t)FREERTOS_FAULT_MALLOC_FAILED;

  freertos_fault_stop();
}
