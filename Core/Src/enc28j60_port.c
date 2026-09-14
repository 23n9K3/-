#include "enc28j60_port.h"

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp_enc28j60.h"
#include "main.h"
#include "project_config.h"
#include "spi.h"
#include "task.h"

#define ENC28J60_PORT_SPI_TIMEOUT_MS 20U
#define ENC28J60_PORT_MAX_SPI_HZ     20000000U

#if ENC28J60_USE_SPI_DMA
#error "Enable SPI1 DMA in CubeMX before implementing ENC28J60 DMA callbacks"
#endif

static uint32_t enc28j60_port_prescaler_value(void)
{
  switch (hspi1.Init.BaudRatePrescaler)
  {
    case SPI_BAUDRATEPRESCALER_2: return 2U;
    case SPI_BAUDRATEPRESCALER_4: return 4U;
    case SPI_BAUDRATEPRESCALER_8: return 8U;
    case SPI_BAUDRATEPRESCALER_16: return 16U;
    case SPI_BAUDRATEPRESCALER_32: return 32U;
    case SPI_BAUDRATEPRESCALER_64: return 64U;
    case SPI_BAUDRATEPRESCALER_128: return 128U;
    case SPI_BAUDRATEPRESCALER_256: return 256U;
    default: return 0U;
  }
}

uint32_t enc28j60_port_get_spi_clock_hz(void)
{
  uint32_t divider = enc28j60_port_prescaler_value();
  uint32_t pclk = HAL_RCC_GetPCLK2Freq();
  return (divider == 0U) ? 0U : (pclk / divider);
}

bool enc28j60_port_pins_are_valid(void)
{
  uint32_t pa5_mode = (GPIOA->MODER >> (5U * 2U)) & 0x03U;
  uint32_t pa6_mode = (GPIOA->MODER >> (6U * 2U)) & 0x03U;
  uint32_t pa7_mode = (GPIOA->MODER >> (7U * 2U)) & 0x03U;
  uint32_t pa5_af = (GPIOA->AFR[0] >> (5U * 4U)) & 0x0FU;
  uint32_t pa6_af = (GPIOA->AFR[0] >> (6U * 4U)) & 0x0FU;
  uint32_t pa7_af = (GPIOA->AFR[0] >> (7U * 4U)) & 0x0FU;
  uint32_t pd14_mode = (GPIOD->MODER >> (14U * 2U)) & 0x03U;
  uint32_t pf14_mode = (GPIOF->MODER >> (14U * 2U)) & 0x03U;
  uint32_t pf15_mode = (GPIOF->MODER >> (15U * 2U)) & 0x03U;
  uint32_t pf15_mask = (1UL << 15U);

  return (pa5_mode == 2U) &&
         (pa6_mode == 2U) &&
         (pa7_mode == 2U) &&
         (pa5_af == GPIO_AF5_SPI1) &&
         (pa6_af == GPIO_AF5_SPI1) &&
         (pa7_af == GPIO_AF5_SPI1) &&
         (pd14_mode == 1U) &&
         (pf14_mode == 1U) &&
         (pf15_mode == 0U) &&
         ((EXTI->FTSR1 & pf15_mask) != 0U) &&
         ((EXTI->RTSR1 & pf15_mask) == 0U);
}

enc28j60_port_status_t enc28j60_port_init(void)
{
  uint32_t spi_clock = enc28j60_port_get_spi_clock_hz();

  enc28j60_port_cs_high();
  enc28j60_port_reset_high();
  if ((hspi1.Instance != SPI1) ||
      (hspi1.Init.Mode != SPI_MODE_MASTER) ||
      (hspi1.Init.Direction != SPI_DIRECTION_2LINES) ||
      (hspi1.Init.DataSize != SPI_DATASIZE_8BIT) ||
      (hspi1.Init.CLKPolarity != SPI_POLARITY_LOW) ||
      (hspi1.Init.CLKPhase != SPI_PHASE_1EDGE) ||
      (hspi1.Init.NSS != SPI_NSS_SOFT) ||
      (hspi1.Init.FirstBit != SPI_FIRSTBIT_MSB) ||
      (hspi1.Init.CRCCalculation != SPI_CRCCALCULATION_DISABLE) ||
      (spi_clock == 0U) || (spi_clock > ENC28J60_PORT_MAX_SPI_HZ) ||
      (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) ||
      (!enc28j60_port_pins_are_valid()))
  {
    return ENC28J60_PORT_ERROR_CONFIG;
  }
  return ENC28J60_PORT_OK;
}

void enc28j60_port_cs_low(void)
{
  HAL_GPIO_WritePin(ENC_CS_GPIO_Port, ENC_CS_Pin, GPIO_PIN_RESET);
}

void enc28j60_port_cs_high(void)
{
  HAL_GPIO_WritePin(ENC_CS_GPIO_Port, ENC_CS_Pin, GPIO_PIN_SET);
}

void enc28j60_port_reset_low(void)
{
  HAL_GPIO_WritePin(ENC_RST_GPIO_Port, ENC_RST_Pin, GPIO_PIN_RESET);
}

void enc28j60_port_reset_high(void)
{
  HAL_GPIO_WritePin(ENC_RST_GPIO_Port, ENC_RST_Pin, GPIO_PIN_SET);
}

enc28j60_port_status_t enc28j60_port_transmit(const uint8_t *data,
                                               size_t length)
{
  if ((data == NULL) || (length == 0U) || (length > 0xFFFFU))
  {
    return ENC28J60_PORT_ERROR_PARAM;
  }
  return (HAL_SPI_Transmit(&hspi1, (uint8_t *)(void *)data,
                           (uint16_t)length,
                           ENC28J60_PORT_SPI_TIMEOUT_MS) == HAL_OK) ?
         ENC28J60_PORT_OK : ENC28J60_PORT_ERROR_SPI;
}

enc28j60_port_status_t enc28j60_port_receive(uint8_t *data, size_t length)
{
  if ((data == NULL) || (length == 0U) || (length > 0xFFFFU))
  {
    return ENC28J60_PORT_ERROR_PARAM;
  }
  return (HAL_SPI_Receive(&hspi1, data, (uint16_t)length,
                          ENC28J60_PORT_SPI_TIMEOUT_MS) == HAL_OK) ?
         ENC28J60_PORT_OK : ENC28J60_PORT_ERROR_SPI;
}

enc28j60_port_status_t enc28j60_port_transfer_byte(uint8_t tx, uint8_t *rx)
{
  if (rx == NULL)
  {
    return ENC28J60_PORT_ERROR_PARAM;
  }
  return (HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1U,
                                  ENC28J60_PORT_SPI_TIMEOUT_MS) == HAL_OK) ?
         ENC28J60_PORT_OK : ENC28J60_PORT_ERROR_SPI;
}

void enc28j60_port_delay_ms(uint32_t delay_ms)
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
  {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
  else
  {
    HAL_Delay(delay_ms);
  }
}

uint32_t enc28j60_port_get_tick_ms(void)
{
  return HAL_GetTick();
}

void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
  BaseType_t higher_priority_task_woken = pdFALSE;

  if (gpio_pin == ENC_INT_Pin)
  {
    ENC28J60_RecordIrqFromISR();
    (void)app_tasks_notify_netif_from_isr(&higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}
