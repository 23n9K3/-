#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

typedef enum
{
  BSP_CAN_ID_STANDARD = 0,
  BSP_CAN_ID_EXTENDED
} bsp_can_id_type_t;

typedef enum
{
  BSP_CAN_FRAME_DATA = 0,
  BSP_CAN_FRAME_REMOTE
} bsp_can_frame_type_t;

typedef struct
{
  uint32_t id;
  bsp_can_id_type_t id_type;
  bsp_can_frame_type_t frame_type;
  uint8_t dlc;
  uint8_t data[8];
  TickType_t rx_tick;
} bsp_can_frame_t;

typedef enum
{
  BSP_CAN_STATUS_OK = 0,
  BSP_CAN_STATUS_INVALID_ARGUMENT,
  BSP_CAN_STATUS_NOT_INITIALIZED,
  BSP_CAN_STATUS_NOT_STARTED,
  BSP_CAN_STATUS_BUSY,
  BSP_CAN_STATUS_TIMEOUT,
  BSP_CAN_STATUS_HAL_ERROR,
  BSP_CAN_STATUS_BUS_OFF
} bsp_can_status_t;

typedef enum
{
  BSP_CAN_MODE_NORMAL = 0,
  BSP_CAN_MODE_INTERNAL_LOOPBACK
} bsp_can_mode_t;

typedef struct
{
  uint32_t hal_state;
  uint32_t hal_error;
  uint32_t esr;
  uint32_t msr;
  uint32_t tsr;
  uint32_t rf0r;
  uint32_t ier;
  uint32_t tx_mailboxes_free;
  uint32_t rx_fifo0_fill;
  uint32_t rx_pin_level;
} bsp_can_diagnostics_t;

bool bsp_can_frame_is_valid(const bsp_can_frame_t *frame);
bsp_can_status_t bsp_can_init(bsp_can_mode_t mode);
bsp_can_status_t bsp_can_configure_filter(void);
bsp_can_status_t bsp_can_start(void);
bsp_can_status_t bsp_can_stop(void);
bsp_can_status_t bsp_can_activate_notifications(void);
bsp_can_status_t bsp_can_restart(bsp_can_mode_t mode);
bsp_can_status_t bsp_can_send(const bsp_can_frame_t *frame,
                              uint32_t *mailbox);
bool bsp_can_is_bus_off(void);
uint32_t bsp_can_get_hal_error(void);
bsp_can_status_t bsp_can_reset_hal_error(void);
uint32_t bsp_can_get_pclk_hz(void);
uint32_t bsp_can_get_bitrate(void);
uint32_t bsp_can_get_sample_point_permille(void);
bsp_can_mode_t bsp_can_get_mode(void);
bool bsp_can_get_diagnostics(bsp_can_diagnostics_t *diagnostics);

#endif /* BSP_CAN_H */
