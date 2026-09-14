#include "bsp_crc.h"

#include <stdbool.h>

#include "crc.h"

#define BSP_CRC32_MPEG2_EMPTY_VALUE 0xFFFFFFFFU
#define BSP_CRC32_MPEG2_CHECK_VALUE 0x0376E6E7U

static bool crc_is_initialized;
static uint32_t crc_error_count;

bsp_crc_status_t bsp_crc_init(void)
{
  crc_is_initialized = false;

  if ((hcrc.Instance != CRC) ||
      (hcrc.Init.DefaultPolynomialUse != DEFAULT_POLYNOMIAL_ENABLE) ||
      (hcrc.Init.DefaultInitValueUse != DEFAULT_INIT_VALUE_ENABLE) ||
      (hcrc.Init.InputDataInversionMode != CRC_INPUTDATA_INVERSION_NONE) ||
      (hcrc.Init.OutputDataInversionMode != CRC_OUTPUTDATA_INVERSION_DISABLE) ||
      (hcrc.InputDataFormat != CRC_INPUTDATA_FORMAT_BYTES) ||
      (HAL_CRC_GetState(&hcrc) != HAL_CRC_STATE_READY))
  {
    crc_error_count++;
    return BSP_CRC_STATUS_HAL_ERROR;
  }

  crc_is_initialized = true;
  return BSP_CRC_STATUS_OK;
}

bsp_crc_status_t bsp_crc32_compute(const uint8_t *data,
                                   size_t length,
                                   uint32_t *crc_value)
{
  if ((crc_value == NULL) || ((data == NULL) && (length != 0U)))
  {
    return BSP_CRC_STATUS_INVALID_ARGUMENT;
  }
  if ((!crc_is_initialized) ||
      (hcrc.Instance != CRC) ||
      (HAL_CRC_GetState(&hcrc) != HAL_CRC_STATE_READY))
  {
    crc_error_count++;
    return BSP_CRC_STATUS_HAL_ERROR;
  }

  if (length == 0U)
  {
    __HAL_CRC_DR_RESET(&hcrc);
    *crc_value = BSP_CRC32_MPEG2_EMPTY_VALUE;
  }
  else
  {
    *crc_value = HAL_CRC_Calculate(&hcrc,
                                   (uint32_t *)(void *)data,
                                   (uint32_t)length);
  }

  return BSP_CRC_STATUS_OK;
}

bsp_crc_status_t bsp_crc_self_test(uint32_t *calculated_crc)
{
  static const uint8_t test_vector[] =
  {
    '1', '2', '3', '4', '5', '6', '7', '8', '9'
  };
  bsp_crc_status_t status;

  if (calculated_crc == NULL)
  {
    return BSP_CRC_STATUS_INVALID_ARGUMENT;
  }

  status = bsp_crc32_compute(test_vector,
                             sizeof(test_vector),
                             calculated_crc);
  if (status != BSP_CRC_STATUS_OK)
  {
    return status;
  }
  if (*calculated_crc != BSP_CRC32_MPEG2_CHECK_VALUE)
  {
    crc_error_count++;
    return BSP_CRC_STATUS_SELF_TEST_FAILED;
  }

  return BSP_CRC_STATUS_OK;
}

uint32_t bsp_crc_get_error_count(void)
{
  return crc_error_count;
}
