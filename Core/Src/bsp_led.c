#include "bsp_led.h"

#include "main.h"
#include "project_config.h"

static bool led_is_on;

static GPIO_PinState bsp_led_output_state(bool on)
{
#if PROJECT_LED_ACTIVE_HIGH
  return on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#else
  return on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#endif
}

void bsp_led_init(void)
{
  /* GPIO 模式和时钟由 CubeMX 生成的 MX_GPIO_Init() 负责。 */
  bsp_led_set(false);
}

void bsp_led_on(void)
{
  bsp_led_set(true);
}

void bsp_led_off(void)
{
  bsp_led_set(false);
}

void bsp_led_toggle(void)
{
  bsp_led_set(!led_is_on);
}

void bsp_led_set(bool on)
{
  HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, bsp_led_output_state(on));
  led_is_on = on;
}

bool bsp_led_get_state(void)
{
  return led_is_on;
}
