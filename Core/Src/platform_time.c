#include "platform_time.h"

#include "stm32l4xx_hal.h"

uint32_t platform_time_get_ms(void)
{
  return HAL_GetTick();
}

bool platform_time_elapsed(uint32_t now,
                           uint32_t previous,
                           uint32_t interval_ms)
{
  /* 无符号减法可在 32 位 Tick 回绕时保持正确。 */
  return (uint32_t)(now - previous) >= interval_ms;
}
