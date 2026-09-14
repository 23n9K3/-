#ifndef FREERTOS_STATIC_H
#define FREERTOS_STATIC_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

void vApplicationGetIdleTaskMemory(StaticTask_t **idle_tcb,
                                   StackType_t **idle_stack,
                                   uint32_t *idle_stack_size);

#endif /* FREERTOS_STATIC_H */
