#include "bsp_uid.h"

#include <stddef.h>

#include "stm32l4xx_hal.h"

static uint32_t bsp_uid_get_word(uint32_t index)
{
  if (index == 0U)
  {
    return HAL_GetUIDw0();
  }
  if (index == 1U)
  {
    return HAL_GetUIDw1();
  }
  return HAL_GetUIDw2();
}

void bsp_uid_get_words(uint32_t uid_words[BSP_UID_WORD_COUNT])
{
  if (uid_words != NULL)
  {
    uid_words[0] = bsp_uid_get_word(0U);
    uid_words[1] = bsp_uid_get_word(1U);
    uid_words[2] = bsp_uid_get_word(2U);
  }
}

bool bsp_uid_get_hex(char output[BSP_UID_HEX_LENGTH + 1U])
{
  static const char hex_digits[] = "0123456789ABCDEF";
  uint32_t word_index;
  uint32_t digit_index;
  uint32_t output_index = 0U;

  if (output == NULL)
  {
    return false;
  }

  for (word_index = 0U; word_index < BSP_UID_WORD_COUNT; word_index++)
  {
    uint32_t uid_word = bsp_uid_get_word(word_index);

    for (digit_index = 0U; digit_index < 8U; digit_index++)
    {
      uint32_t shift = 28U - (digit_index * 4U);
      output[output_index++] = hex_digits[(uid_word >> shift) & 0x0FU];
    }
    uid_word = 0U;
  }
  output[output_index] = '\0';
  return true;
}
