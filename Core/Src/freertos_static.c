#include "freertos_static.h"

static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **idle_tcb,
                                   StackType_t **idle_stack,
                                   uint32_t *idle_stack_size)
{
  configASSERT(idle_tcb != NULL);
  configASSERT(idle_stack != NULL);
  configASSERT(idle_stack_size != NULL);

  *idle_tcb = &idle_task_tcb;
  *idle_stack = idle_task_stack;
  *idle_stack_size = (uint32_t)configMINIMAL_STACK_SIZE;
}
