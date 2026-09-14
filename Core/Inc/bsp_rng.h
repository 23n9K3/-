#ifndef BSP_RNG_H
#define BSP_RNG_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  BSP_RNG_STATUS_OK = 0,
  BSP_RNG_STATUS_INVALID_ARGUMENT,
  BSP_RNG_STATUS_HAL_ERROR,
  BSP_RNG_STATUS_TIMEOUT,
  BSP_RNG_STATUS_HEALTH_CHECK_FAILED
} bsp_rng_status_t;

/* These polling APIs are currently intended for one caller at a time. */
bsp_rng_status_t bsp_rng_init(void);
bsp_rng_status_t bsp_rng_get_u32(uint32_t *value);
bsp_rng_status_t bsp_rng_fill(uint8_t *buffer, size_t length);
bsp_rng_status_t bsp_rng_self_test(uint32_t *diagnostic_sample);
uint32_t bsp_rng_get_error_count(void);

#endif /* BSP_RNG_H */
