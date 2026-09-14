#include "bsp_rng.h"

#include <stdbool.h>

#include "project_config.h"
#include "rng.h"

static bool rng_is_initialized;
static uint32_t rng_error_count;

static bsp_rng_status_t bsp_rng_status_from_hal(void)
{
  if ((HAL_RNG_GetError(&hrng) & HAL_RNG_ERROR_TIMEOUT) != 0U)
  {
    return BSP_RNG_STATUS_TIMEOUT;
  }

  return BSP_RNG_STATUS_HAL_ERROR;
}

static bool bsp_rng_recover(void)
{
  if (HAL_RNG_DeInit(&hrng) != HAL_OK)
  {
    rng_error_count++;
    return false;
  }

  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    rng_error_count++;
    return false;
  }

  return true;
}

static bool bsp_rng_hardware_error_is_active(void)
{
  return (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_SECS) != RESET) ||
         (__HAL_RNG_GET_FLAG(&hrng, RNG_FLAG_CECS) != RESET);
}

bsp_rng_status_t bsp_rng_init(void)
{
  rng_is_initialized = false;

  if ((hrng.Instance != RNG) ||
      (HAL_RNG_GetState(&hrng) != HAL_RNG_STATE_READY))
  {
    if (!bsp_rng_recover())
    {
      return bsp_rng_status_from_hal();
    }
  }

  rng_is_initialized = true;
  return BSP_RNG_STATUS_OK;
}

bsp_rng_status_t bsp_rng_get_u32(uint32_t *value)
{
  uint32_t attempt;
  bsp_rng_status_t last_status = BSP_RNG_STATUS_HAL_ERROR;

  if (value == NULL)
  {
    return BSP_RNG_STATUS_INVALID_ARGUMENT;
  }

  *value = 0U;
  if (!rng_is_initialized)
  {
    return BSP_RNG_STATUS_HAL_ERROR;
  }

  for (attempt = 0U; attempt <= PROJECT_RNG_MAX_RETRY_COUNT; attempt++)
  {
    if ((!bsp_rng_hardware_error_is_active()) &&
        (HAL_RNG_GenerateRandomNumber(&hrng, value) == HAL_OK) &&
        (!bsp_rng_hardware_error_is_active()))
    {
      return BSP_RNG_STATUS_OK;
    }

    rng_error_count++;
    last_status = bsp_rng_status_from_hal();
    *value = 0U;

    if ((attempt == PROJECT_RNG_MAX_RETRY_COUNT) ||
        (!bsp_rng_recover()))
    {
      break;
    }
  }

  rng_is_initialized = false;
  return last_status;
}

bsp_rng_status_t bsp_rng_fill(uint8_t *buffer, size_t length)
{
  size_t offset = 0U;
  uint32_t random_word = 0U;

  if ((buffer == NULL) && (length != 0U))
  {
    return BSP_RNG_STATUS_INVALID_ARGUMENT;
  }

  while (offset < length)
  {
    uint32_t byte_index;
    bsp_rng_status_t status = bsp_rng_get_u32(&random_word);

    if (status != BSP_RNG_STATUS_OK)
    {
      random_word = 0U;
      return status;
    }

    for (byte_index = 0U;
         (byte_index < sizeof(random_word)) && (offset < length);
         byte_index++)
    {
      buffer[offset++] = (uint8_t)(random_word >> (8U * byte_index));
    }
    random_word = 0U;
  }

  return BSP_RNG_STATUS_OK;
}

bsp_rng_status_t bsp_rng_self_test(uint32_t *diagnostic_sample)
{
  uint32_t index;
  uint32_t first_value = 0U;
  uint32_t current_value = 0U;
  bool all_zero = true;
  bool all_ones = true;
  bool all_same = true;

  if (diagnostic_sample == NULL)
  {
    return BSP_RNG_STATUS_INVALID_ARGUMENT;
  }

  *diagnostic_sample = 0U;
  for (index = 0U; index < PROJECT_RNG_SELF_TEST_WORD_COUNT; index++)
  {
    bsp_rng_status_t status = bsp_rng_get_u32(&current_value);

    if (status != BSP_RNG_STATUS_OK)
    {
      first_value = 0U;
      current_value = 0U;
      return status;
    }

    if (index == 0U)
    {
      first_value = current_value;
      *diagnostic_sample = current_value;
    }
    else if (current_value != first_value)
    {
      all_same = false;
    }

    if (current_value != 0U)
    {
      all_zero = false;
    }
    if (current_value != 0xFFFFFFFFU)
    {
      all_ones = false;
    }
  }

  first_value = 0U;
  current_value = 0U;
  if (all_zero || all_ones || all_same)
  {
    rng_error_count++;
    rng_is_initialized = false;
    *diagnostic_sample = 0U;
    return BSP_RNG_STATUS_HEALTH_CHECK_FAILED;
  }

  return BSP_RNG_STATUS_OK;
}

uint32_t bsp_rng_get_error_count(void)
{
  return rng_error_count;
}
