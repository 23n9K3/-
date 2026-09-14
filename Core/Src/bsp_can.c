#include "bsp_can.h"

#include "can.h"
#include "stm32l4xx_hal.h"

#define BSP_CAN_TARGET_PCLK_HZ 120000000U
#define BSP_CAN_TARGET_BITRATE 500000U

static bool can_initialized;
static bool can_started;
static bsp_can_mode_t current_mode;

static uint32_t bsp_can_hal_mode(bsp_can_mode_t mode)
{
  return (mode == BSP_CAN_MODE_INTERNAL_LOOPBACK) ?
         CAN_MODE_LOOPBACK : CAN_MODE_NORMAL;
}

static bool bsp_can_timing_is_valid(void)
{
  return (hcan1.Instance == CAN1) &&
         (hcan1.Init.Prescaler == 15U) &&
         (hcan1.Init.SyncJumpWidth == CAN_SJW_1TQ) &&
         (hcan1.Init.TimeSeg1 == CAN_BS1_13TQ) &&
         (hcan1.Init.TimeSeg2 == CAN_BS2_2TQ) &&
         (hcan1.Init.AutoBusOff == ENABLE) &&
         (hcan1.Init.AutoRetransmission == ENABLE) &&
         (hcan1.Init.ReceiveFifoLocked == DISABLE) &&
         (hcan1.Init.TransmitFifoPriority == DISABLE) &&
         (bsp_can_get_pclk_hz() == BSP_CAN_TARGET_PCLK_HZ) &&
         (bsp_can_get_bitrate() == BSP_CAN_TARGET_BITRATE);
}

bool bsp_can_frame_is_valid(const bsp_can_frame_t *frame)
{
  if ((frame == NULL) || (frame->dlc > 8U) ||
      ((frame->id_type != BSP_CAN_ID_STANDARD) &&
       (frame->id_type != BSP_CAN_ID_EXTENDED)) ||
      ((frame->frame_type != BSP_CAN_FRAME_DATA) &&
       (frame->frame_type != BSP_CAN_FRAME_REMOTE)))
  {
    return false;
  }
  if (frame->id_type == BSP_CAN_ID_STANDARD)
  {
    return frame->id <= 0x7FFU;
  }
  return frame->id <= 0x1FFFFFFFU;
}

bsp_can_status_t bsp_can_init(bsp_can_mode_t mode)
{
  uint32_t desired_mode;

  if ((mode != BSP_CAN_MODE_NORMAL) &&
      (mode != BSP_CAN_MODE_INTERNAL_LOOPBACK))
  {
    return BSP_CAN_STATUS_INVALID_ARGUMENT;
  }
  desired_mode = bsp_can_hal_mode(mode);
  can_initialized = false;
  can_started = false;

  if (hcan1.Init.Mode != desired_mode)
  {
    if (HAL_CAN_DeInit(&hcan1) != HAL_OK)
    {
      return BSP_CAN_STATUS_HAL_ERROR;
    }
    hcan1.Init.Mode = desired_mode;
    if (HAL_CAN_Init(&hcan1) != HAL_OK)
    {
      return BSP_CAN_STATUS_HAL_ERROR;
    }
  }

  if (!bsp_can_timing_is_valid())
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  current_mode = mode;
  can_initialized = true;
  return BSP_CAN_STATUS_OK;
}

bsp_can_status_t bsp_can_configure_filter(void)
{
  CAN_FilterTypeDef filter = {0};

  if (!can_initialized)
  {
    return BSP_CAN_STATUS_NOT_INITIALIZED;
  }
  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0U;
  filter.FilterIdLow = 0U;
  filter.FilterMaskIdHigh = 0U;
  filter.FilterMaskIdLow = 0U;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = 14U;
  return (HAL_CAN_ConfigFilter(&hcan1, &filter) == HAL_OK) ?
         BSP_CAN_STATUS_OK : BSP_CAN_STATUS_HAL_ERROR;
}

bsp_can_status_t bsp_can_start(void)
{
  if (!can_initialized)
  {
    return BSP_CAN_STATUS_NOT_INITIALIZED;
  }
  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  can_started = true;
  return BSP_CAN_STATUS_OK;
}

bsp_can_status_t bsp_can_stop(void)
{
  if (!can_initialized)
  {
    return BSP_CAN_STATUS_NOT_INITIALIZED;
  }
  if (can_started && (HAL_CAN_Stop(&hcan1) != HAL_OK))
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  can_started = false;
  return BSP_CAN_STATUS_OK;
}

bsp_can_status_t bsp_can_activate_notifications(void)
{
  const uint32_t notifications = CAN_IT_RX_FIFO0_MSG_PENDING |
                                 CAN_IT_RX_FIFO0_OVERRUN |
                                 CAN_IT_TX_MAILBOX_EMPTY |
                                 CAN_IT_ERROR_WARNING |
                                 CAN_IT_ERROR_PASSIVE |
                                 CAN_IT_BUSOFF |
                                 CAN_IT_LAST_ERROR_CODE |
                                 CAN_IT_ERROR;
  if (!can_started)
  {
    return BSP_CAN_STATUS_NOT_STARTED;
  }
  return (HAL_CAN_ActivateNotification(&hcan1, notifications) == HAL_OK) ?
         BSP_CAN_STATUS_OK : BSP_CAN_STATUS_HAL_ERROR;
}

bsp_can_status_t bsp_can_restart(bsp_can_mode_t mode)
{
  bsp_can_status_t status = bsp_can_stop();
  if (status != BSP_CAN_STATUS_OK)
  {
    return status;
  }
  if (HAL_CAN_DeInit(&hcan1) != HAL_OK)
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  hcan1.Init.Mode = bsp_can_hal_mode(mode);
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  can_initialized = true;
  can_started = false;
  current_mode = mode;
  if (!bsp_can_timing_is_valid())
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  if (bsp_can_configure_filter() != BSP_CAN_STATUS_OK)
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  if (bsp_can_start() != BSP_CAN_STATUS_OK)
  {
    return BSP_CAN_STATUS_HAL_ERROR;
  }
  return bsp_can_activate_notifications();
}

bsp_can_status_t bsp_can_send(const bsp_can_frame_t *frame,
                              uint32_t *mailbox)
{
  CAN_TxHeaderTypeDef header = {0};

  if ((mailbox == NULL) || (!bsp_can_frame_is_valid(frame)))
  {
    return BSP_CAN_STATUS_INVALID_ARGUMENT;
  }
  if (!can_initialized)
  {
    return BSP_CAN_STATUS_NOT_INITIALIZED;
  }
  if (!can_started)
  {
    return BSP_CAN_STATUS_NOT_STARTED;
  }
  if (bsp_can_is_bus_off())
  {
    return BSP_CAN_STATUS_BUS_OFF;
  }
  if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)
  {
    return BSP_CAN_STATUS_BUSY;
  }

  header.StdId = (frame->id_type == BSP_CAN_ID_STANDARD) ? frame->id : 0U;
  header.ExtId = (frame->id_type == BSP_CAN_ID_EXTENDED) ? frame->id : 0U;
  header.IDE = (frame->id_type == BSP_CAN_ID_STANDARD) ? CAN_ID_STD : CAN_ID_EXT;
  header.RTR = (frame->frame_type == BSP_CAN_FRAME_DATA) ? CAN_RTR_DATA : CAN_RTR_REMOTE;
  header.DLC = frame->dlc;
  header.TransmitGlobalTime = DISABLE;
  return (HAL_CAN_AddTxMessage(&hcan1, &header,
                               (uint8_t *)frame->data, mailbox) == HAL_OK) ?
         BSP_CAN_STATUS_OK : BSP_CAN_STATUS_HAL_ERROR;
}

bool bsp_can_is_bus_off(void)
{
  return (__HAL_CAN_GET_FLAG(&hcan1, CAN_FLAG_BOF) != RESET);
}

uint32_t bsp_can_get_hal_error(void)
{
  return HAL_CAN_GetError(&hcan1);
}

bsp_can_status_t bsp_can_reset_hal_error(void)
{
  return (HAL_CAN_ResetError(&hcan1) == HAL_OK) ?
         BSP_CAN_STATUS_OK : BSP_CAN_STATUS_HAL_ERROR;
}

uint32_t bsp_can_get_pclk_hz(void)
{
  return HAL_RCC_GetPCLK1Freq();
}

uint32_t bsp_can_get_bitrate(void)
{
  if ((hcan1.Init.Prescaler == 0U) ||
      (hcan1.Init.TimeSeg1 != CAN_BS1_13TQ) ||
      (hcan1.Init.TimeSeg2 != CAN_BS2_2TQ))
  {
    return 0U;
  }
  return bsp_can_get_pclk_hz() / (hcan1.Init.Prescaler * 16U);
}

uint32_t bsp_can_get_sample_point_permille(void)
{
  return ((1U + 13U) * 1000U) / 16U;
}

bsp_can_mode_t bsp_can_get_mode(void)
{
  return current_mode;
}

bool bsp_can_get_diagnostics(bsp_can_diagnostics_t *diagnostics)
{
  if ((diagnostics == NULL) || (hcan1.Instance != CAN1))
  {
    return false;
  }

  diagnostics->hal_state = (uint32_t)HAL_CAN_GetState(&hcan1);
  diagnostics->hal_error = HAL_CAN_GetError(&hcan1);
  diagnostics->esr = CAN1->ESR;
  diagnostics->msr = CAN1->MSR;
  diagnostics->tsr = CAN1->TSR;
  diagnostics->rf0r = CAN1->RF0R;
  diagnostics->ier = CAN1->IER;
  diagnostics->tx_mailboxes_free = HAL_CAN_GetTxMailboxesFreeLevel(&hcan1);
  diagnostics->rx_fifo0_fill = HAL_CAN_GetRxFifoFillLevel(&hcan1,
                                                           CAN_RX_FIFO0);
  diagnostics->rx_pin_level = (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) ==
                               GPIO_PIN_SET) ? 1U : 0U;
  return true;
}
