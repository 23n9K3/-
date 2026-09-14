#include "bsp_debug_uart.h"

#include <string.h>

#include "usart.h"

static uint32_t transmitted_byte_count;
static uint32_t transmit_error_count;

bool bsp_debug_uart_init(void)
{
  /* 硬件初始化由 CubeMX 生成的 MX_LPUART1_UART_Init() 完成。 */
  return (hlpuart1.Instance == LPUART1) &&
         (HAL_UART_GetState(&hlpuart1) == HAL_UART_STATE_READY);
}

bool bsp_debug_uart_write(const uint8_t *data,
                          size_t length,
                          uint32_t timeout_ms)
{
  HAL_StatusTypeDef status;

  if (length == 0U)
  {
    return true;
  }

  if ((data == NULL) || (length > UINT16_MAX))
  {
    transmit_error_count++;
    return false;
  }

  status = HAL_UART_Transmit(&hlpuart1,
                             (uint8_t *)data,
                             (uint16_t)length,
                             timeout_ms);
  if (status != HAL_OK)
  {
    transmit_error_count++;
    return false;
  }

  transmitted_byte_count += (uint32_t)length;
  return true;
}

bool bsp_debug_uart_write_string(const char *text,
                                 uint32_t timeout_ms)
{
  if (text == NULL)
  {
    transmit_error_count++;
    return false;
  }

  return bsp_debug_uart_write((const uint8_t *)text,
                              strlen(text),
                              timeout_ms);
}

uint32_t bsp_debug_uart_get_transmitted_byte_count(void)
{
  return transmitted_byte_count;
}

uint32_t bsp_debug_uart_get_error_count(void)
{
  return transmit_error_count;
}
