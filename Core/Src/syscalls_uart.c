#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp_debug_uart.h"
#include "FreeRTOS.h"
#include "project_config.h"
#include "task.h"

int _write(int file, char *ptr, int len)
{
  (void)file;

  if ((ptr == NULL) || (len <= 0))
  {
    return (len == 0) ? 0 : -1;
  }

  /* printf redirection is synchronous and therefore startup-only. */
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    return -1;
  }

  if (!bsp_debug_uart_write((const uint8_t *)ptr,
                            (size_t)len,
                            PROJECT_UART_TX_TIMEOUT_MS))
  {
    return -1;
  }

  return len;
}

#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
int fputc(int character, FILE *stream)
{
  uint8_t byte = (uint8_t)character;

  (void)stream;
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
    return EOF;
  }
  if (!bsp_debug_uart_write(&byte, 1U, PROJECT_UART_TX_TIMEOUT_MS))
  {
    return EOF;
  }

  return character;
}
#endif
