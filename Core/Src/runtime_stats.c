#include "runtime_stats.h"

#include "project_config.h"
#include "tim.h"

static bool runtime_stats_ready;

bool runtime_stats_init(void)
{
  if ((htim2.Instance != TIM2) ||
      (htim2.Init.Prescaler != 11999U) ||
      (htim2.Init.Period != 0xFFFFFFFFUL))
  {
    return false;
  }

  __HAL_TIM_SET_COUNTER(&htim2, 0U);
  if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
  {
    return false;
  }

  runtime_stats_ready = true;
  return true;
}

void runtime_stats_configure_for_freertos(void)
{
  /* main() starts TIM2 before the scheduler.  Reset it here so all task
   * accounting uses the same scheduler-start epoch. */
  if (runtime_stats_ready)
  {
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
  }
}

uint32_t runtime_stats_get_counter(void)
{
  if (!runtime_stats_ready)
  {
    return 0U;
  }

  return (uint32_t)__HAL_TIM_GET_COUNTER(&htim2);
}

uint32_t runtime_stats_get_frequency_hz(void)
{
  return PROJECT_RUNTIME_STATS_FREQUENCY_HZ;
}
