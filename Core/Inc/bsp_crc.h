#ifndef BSP_CRC_H
#define BSP_CRC_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  BSP_CRC_STATUS_OK = 0,
  BSP_CRC_STATUS_INVALID_ARGUMENT,
  BSP_CRC_STATUS_HAL_ERROR,
  BSP_CRC_STATUS_SELF_TEST_FAILED
} bsp_crc_status_t;

/* CRC-32/MPEG-2; intended for one caller at a time in this stage. */
bsp_crc_status_t bsp_crc_init(void);
bsp_crc_status_t bsp_crc32_compute(const uint8_t *data,
                                   size_t length,
                                   uint32_t *crc_value);
bsp_crc_status_t bsp_crc_self_test(uint32_t *calculated_crc);
uint32_t bsp_crc_get_error_count(void);

#endif /* BSP_CRC_H */
