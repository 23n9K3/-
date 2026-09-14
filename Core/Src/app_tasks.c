#include "app_tasks.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_main.h"
#include "app_ipc.h"
#include "app_system_state.h"
#include "app_network_state.h"
#include "bsp_crc.h"
#include "bsp_led.h"
#include "bsp_rng.h"
#include "bsp_rtc.h"
#include "can_service.h"
#include "lwip_port.h"
#include "netif_stats.h"
#include "netif_task.h"
#include "network_diag.h"
#include "net_transport.h"
#include "platform_log.h"
#include "platform_time.h"
#include "project_config.h"
#include "time_diag.h"
#include "time_sync_task.h"
#include "trusted_time.h"

typedef struct
{
  volatile uint32_t loop_count;
  volatile uint32_t last_run_tick;
  volatile uint32_t error_count;
  volatile bool alive;
} task_health_t;

typedef enum
{
  NET_STATE_DISABLED = 0,
  NET_STATE_INITIALIZING,
  NET_STATE_DISCONNECTED,
  NET_STATE_CONNECTING,
  NET_STATE_CONNECTED,
  NET_STATE_ERROR
} net_state_t;

#define APP_STATIC_ASSERT(name, condition) \
  typedef char app_static_assert_##name[(condition) ? 1 : -1]

static StaticTask_t sensor_task_tcb;
static StackType_t sensor_task_stack[PROJECT_SENSOR_TASK_STACK_WORDS];
static TaskHandle_t sensor_task_handle;

static StaticTask_t can_task_tcb;
static StackType_t can_task_stack[PROJECT_CAN_TASK_STACK_WORDS];
static TaskHandle_t can_task_handle;

static StaticTask_t net_task_tcb;
static StackType_t net_task_stack[PROJECT_NET_TASK_STACK_WORDS];
static TaskHandle_t net_task_handle;

static StaticTask_t netif_task_tcb;
static StackType_t netif_task_stack[PROJECT_NETIF_TASK_STACK_WORDS];
static TaskHandle_t netif_task_handle;

static StaticTask_t security_task_tcb;
static StackType_t security_task_stack[PROJECT_SECURITY_TASK_STACK_WORDS];
static TaskHandle_t security_task_handle;

static StaticTask_t telemetry_task_tcb;
static StackType_t telemetry_task_stack[PROJECT_TELEMETRY_TASK_STACK_WORDS];
static TaskHandle_t telemetry_task_handle;

static StaticTask_t monitor_task_tcb;
static StackType_t monitor_task_stack[PROJECT_MONITOR_TASK_STACK_WORDS];
static TaskHandle_t monitor_task_handle;

static StaticTask_t logger_task_tcb;
static StackType_t logger_task_stack[PROJECT_LOGGER_TASK_STACK_WORDS];
static TaskHandle_t logger_task_handle;

APP_STATIC_ASSERT(stack_type_is_32_bits, sizeof(StackType_t) == 4U);
APP_STATIC_ASSERT(sensor_stack_is_1_kib, sizeof(sensor_task_stack) == 1024U);
APP_STATIC_ASSERT(can_stack_is_1_kib, sizeof(can_task_stack) == 1024U);
APP_STATIC_ASSERT(net_stack_is_2_kib, sizeof(net_task_stack) == 2048U);
APP_STATIC_ASSERT(netif_stack_is_2_kib, sizeof(netif_task_stack) == 2048U);
APP_STATIC_ASSERT(security_stack_is_8_kib, sizeof(security_task_stack) == 8192U);
APP_STATIC_ASSERT(telemetry_stack_is_2_kib, sizeof(telemetry_task_stack) == 2048U);
APP_STATIC_ASSERT(monitor_stack_is_1_kib, sizeof(monitor_task_stack) == 1024U);
APP_STATIC_ASSERT(logger_stack_is_2_kib, sizeof(logger_task_stack) == 2048U);

static task_health_t sensor_health;
static task_health_t can_health;
static task_health_t net_health;
static task_health_t netif_health;
static task_health_t security_health;
static task_health_t telemetry_health;
static task_health_t monitor_health;
static task_health_t logger_health;

static volatile net_state_t net_state = NET_STATE_DISABLED;
static TaskStatus_t task_status_array[PROJECT_MAX_MONITORED_TASKS];
static uint32_t monitor_stats_error_count;
static uint32_t monitor_stats_truncated_count;
static uint8_t network_diag_log_divider;
static volatile bool can_notification_test_pending;
static volatile bool can_notification_test_passed;

#if NET_TRANSPORT_SELF_TEST
static uint8_t transport_test_tx[4097U];
static uint8_t transport_test_rx[4097U];

static bool transport_test_stream(uint32_t length)
{
  uint32_t offset = 0U;
  int32_t result = 0;
  while (offset < length)
  {
    result = net_transport_write(NET_TRANSPORT_TCP,
      &transport_test_tx[offset], length - offset, 2000U);
    if (result <= 0) return false;
    offset += (uint32_t)result;
  }
  offset = 0U;
  while (offset < length)
  {
    result = net_transport_read(NET_TRANSPORT_TCP,
      &transport_test_rx[offset], length - offset, 5000U);
    if (result <= 0) return false;
    offset += (uint32_t)result;
  }
  return memcmp(transport_test_tx, transport_test_rx, length) == 0;
}

static void transport_self_test_run(void)
{
  ip_addr_t server;
  uint32_t index;
  int32_t result = 0;
  bool tcp_pass = true;
  IP_ADDR4(&server, NET_TEST_SERVER_IP0, NET_TEST_SERVER_IP1,
           NET_TEST_SERVER_IP2, NET_TEST_SERVER_IP3);
  for (index = 0U; index < sizeof(transport_test_tx); index++)
    transport_test_tx[index] = (uint8_t)((index * 29U) ^ (index >> 3U));
  if (net_transport_connect(NET_TRANSPORT_TCP, &server,
                            NET_TEST_TCP_PORT) != NET_TRANSPORT_OK)
    tcp_pass = false;
  if ((net_transport_wait_events(NET_TRANSPORT_EVENT_CONNECTED |
       NET_TRANSPORT_EVENT_ERROR, 5000U) &
       NET_TRANSPORT_EVENT_CONNECTED) == 0U) tcp_pass = false;
  result = net_transport_read(NET_TRANSPORT_TCP, transport_test_rx, 1U, 0U);
  if (result != NET_TRANSPORT_WOULD_BLOCK) tcp_pass = false;
  if (tcp_pass && (!transport_test_stream(31U) ||
                   !transport_test_stream(1024U) ||
                   !transport_test_stream(4097U) ||
                   !transport_test_stream(4096U))) tcp_pass = false;
  if (tcp_pass)
  {
    LOG_INFO("XTEST", "TCP transport connect: PASS");
    LOG_INFO("XTEST", "TCP transport RX: PASS");
    LOG_INFO("XTEST", "TCP transport TX: PASS");
  }
  else
  {
    LOG_ERROR("XTEST", "TCP transport test: FAIL result=%ld",
              (long)result);
  }
  (void)net_transport_close(NET_TRANSPORT_TCP);

  if ((net_transport_connect(NET_TRANSPORT_UDP, &server,
                             NET_TEST_UDP_PORT) == NET_TRANSPORT_OK) &&
      ((net_transport_wait_events(NET_TRANSPORT_EVENT_CONNECTED |
        NET_TRANSPORT_EVENT_ERROR, 2000U) &
        NET_TRANSPORT_EVENT_CONNECTED) != 0U))
  {
    result = net_transport_udp_send_datagram(transport_test_tx, 1472U,
      &server, NET_TEST_UDP_PORT, 2000U);
    if (result == 1472)
      result = net_transport_udp_recv_datagram(transport_test_rx,
        sizeof(transport_test_rx), NULL, NULL, 5000U);
    if ((result == 1472) &&
        (memcmp(transport_test_tx, transport_test_rx, 1472U) == 0))
    {
      LOG_INFO("XTEST", "UDP transport RX: PASS");
      LOG_INFO("XTEST", "UDP transport TX: PASS");
    }
    else
    {
      LOG_ERROR("XTEST", "UDP transport test: FAIL result=%ld",
                (long)result);
    }
    (void)net_transport_close(NET_TRANSPORT_UDP);
  }
  else
  {
    LOG_ERROR("XTEST", "UDP transport connect: FAIL");
  }
  LOG_INFO("XTEST", "Remote close and cable removal: WAITING FOR HARDWARE TEST");
}
#endif

static const char *net_state_text(net_state_t state)
{
  switch (state)
  {
    case NET_STATE_DISABLED: return "disabled";
    case NET_STATE_INITIALIZING: return "initializing";
    case NET_STATE_DISCONNECTED: return "link-down";
    case NET_STATE_CONNECTING: return "connecting";
    case NET_STATE_CONNECTED: return "link-up";
    case NET_STATE_ERROR: return "error";
    default: return "invalid";
  }
}

static void task_health_init(task_health_t *health)
{
  health->loop_count = 0U;
  health->last_run_tick = 0U;
  health->error_count = 0U;
  health->alive = false;
}

static void task_health_mark_alive(task_health_t *health)
{
  health->last_run_tick = (uint32_t)xTaskGetTickCount();
  health->loop_count++;
  health->alive = true;
}

static const task_health_t *task_health_from_handle(TaskHandle_t handle)
{
  if (handle == sensor_task_handle)
  {
    return &sensor_health;
  }
  if (handle == can_task_handle)
  {
    return &can_health;
  }
  if (handle == net_task_handle)
  {
    return &net_health;
  }
  if (handle == netif_task_handle)
  {
    return &netif_health;
  }
  if (handle == security_task_handle)
  {
    return &security_health;
  }
  if (handle == telemetry_task_handle)
  {
    return &telemetry_health;
  }
  if (handle == monitor_task_handle)
  {
    return &monitor_health;
  }
  if (handle == logger_task_handle)
  {
    return &logger_health;
  }

  return NULL;
}

static const char *task_state_text(TaskHandle_t handle, eTaskState state)
{
  if (((handle == can_task_handle) || (handle == security_task_handle)) &&
      ((state == eBlocked) || (state == eSuspended)))
  {
    return "blocked-wait";
  }

  switch (state)
  {
    case eRunning:
      return "running";
    case eReady:
      return "ready";
    case eBlocked:
      return "blocked";
    case eSuspended:
      return "suspended";
    case eDeleted:
      return "deleted";
    default:
      return "invalid";
  }
}

static uint32_t task_cpu_percent(uint32_t task_runtime,
                                 uint32_t total_runtime)
{
  uint64_t scaled_runtime;

  if (total_runtime == 0U)
  {
    return 0U;
  }

  scaled_runtime = (uint64_t)task_runtime * 100ULL;
  return (uint32_t)(scaled_runtime / (uint64_t)total_runtime);
}

static void sensor_task(void *argument)
{
  TickType_t previous_wake_time;
  const TickType_t interval = pdMS_TO_TICKS(PROJECT_SENSOR_INTERVAL_MS);
  uint32_t led_divider = 0U;

  (void)argument;
  previous_wake_time = xTaskGetTickCount();
  LOG_INFO("TASK", "SensorTask started");

  for (;;)
  {
    (void)xTaskDelayUntil(&previous_wake_time, interval);
    task_health_mark_alive(&sensor_health);

    led_divider++;
    if (led_divider >=
        (PROJECT_LED_TOGGLE_INTERVAL_MS / PROJECT_SENSOR_INTERVAL_MS))
    {
      led_divider = 0U;
      bsp_led_toggle();
    }
  }
}

static void can_process_task(void *argument)
{
  uint32_t notification_count;
  bool can_ready;

  (void)argument;
  task_health_mark_alive(&can_health);
  LOG_INFO("TASK", "CanProcessTask started");
  can_ready = can_service_init();
  if (!can_ready)
  {
    can_health.error_count++;
    LOG_ERROR("CAN", "CAN service initialization failed");
  }

  for (;;)
  {
    notification_count = ulTaskNotifyTake(
      pdTRUE, pdMS_TO_TICKS(100U));
    if (can_ready)
    {
      can_service_process();
    }
    task_health_mark_alive(&can_health);
    if ((notification_count > 0U) && can_notification_test_pending)
    {
      can_notification_test_pending = false;
      can_notification_test_passed = true;
      LOG_INFO("IPC", "Direct notification test: PASS");
    }
  }
}

static void net_manager_task(void *argument)
{
  const TickType_t interval = pdMS_TO_TICKS(PROJECT_NET_INTERVAL_MS);
  app_event_t event;
  bool lwip_started = false;

  (void)argument;
  LOG_INFO("TASK", "NetManagerTask started, lwIP policy owner");

  for (;;)
  {
    bool received = app_event_receive(&event, interval);
    while (received)
    {
      if (!app_system_state_process_event(&event, 0U))
      {
        net_health.error_count++;
      }
      switch (event.type)
      {
        case APP_EVENT_NET_ENC_READY:
          net_state = NET_STATE_INITIALIZING;
          if ((!lwip_started) && lwip_port_start())
          {
            lwip_started = true;
          }
          else if (!lwip_started)
          {
            net_state = NET_STATE_ERROR;
            net_health.error_count++;
          }
          break;
        case APP_EVENT_NET_LINK_UP:
          net_state = NET_STATE_CONNECTING;
          if (lwip_started) (void)lwip_port_request_link(true);
          break;
        case APP_EVENT_NET_LINK_DOWN:
          net_state = NET_STATE_DISCONNECTED;
          if (lwip_started) (void)lwip_port_request_link(false);
          break;
        case APP_EVENT_NET_IP_READY:
          net_state = NET_STATE_CONNECTED;
          break;
        case APP_EVENT_NET_ERROR:
          net_state = NET_STATE_ERROR;
          break;
        default:
          break;
      }
      received = app_event_receive(&event, 0U);
    }
    if (lwip_started) lwip_port_process();
    net_transport_process();
    task_health_mark_alive(&net_health);
  }
}

static void netif_driver_task(void *argument)
{
  bool ready = false;
  TickType_t retry_tick = 0U;

  (void)argument;
  LOG_INFO("TASK", "NetIfTask started, ENC28J60 owner");
  for (;;)
  {
    uint32_t notification_count;
    TickType_t now;

    notification_count = ulTaskNotifyTake(
      pdTRUE, pdMS_TO_TICKS(PROJECT_NET_INTERVAL_MS));
    now = xTaskGetTickCount();
    if ((!ready) && ((int32_t)(now - retry_tick) >= 0))
    {
      ready = netif_task_initialize();
      if (!ready)
      {
        netif_health.error_count++;
        retry_tick = now + pdMS_TO_TICKS(PROJECT_ENC_INIT_RETRY_MS);
      }
    }
    if (ready) netif_task_process(notification_count);
    task_health_mark_alive(&netif_health);
  }
}

static void security_task(void *argument)
{
  uint32_t notification_bits;
  uint64_t current_epoch;
#if NET_TRANSPORT_SELF_TEST
  bool transport_test_done = false;
#endif

  (void)argument;
  task_health_mark_alive(&security_health);
  LOG_INFO("TASK", "SecurityTask started, waiting for notification bits");

  for (;;)
  {
    if (xTaskNotifyWait(0U,
                        SECURITY_NOTIFY_ALL,
                        &notification_bits,
                        portMAX_DELAY) == pdTRUE)
    {
      /* Bit meanings are reserved for later security request processing. */
      task_health_mark_alive(&security_health);
      if ((notification_bits & SECURITY_NOTIFY_TIME_STATE) != 0U)
      {
        (void)security_time_precheck(&current_epoch);
      }
#if NET_TRANSPORT_SELF_TEST
      if (((notification_bits & SECURITY_NOTIFY_START) != 0U) &&
          (!transport_test_done))
      {
        transport_test_done = true;
        transport_self_test_run();
      }
#endif
    }
  }
}

static void telemetry_task(void *argument)
{
  TickType_t previous_wake_time;
  const TickType_t interval = pdMS_TO_TICKS(PROJECT_TELEMETRY_INTERVAL_MS);

  (void)argument;
  previous_wake_time = xTaskGetTickCount();
  LOG_INFO("TASK", "TelemetryTask started");

  for (;;)
  {
    (void)xTaskDelayUntil(&previous_wake_time, interval);
    task_health_mark_alive(&telemetry_health);
  }
}

static void logger_task(void *argument)
{
  (void)argument;
  task_health_mark_alive(&logger_health);
  LOG_INFO("TASK", "LoggerTask running");

  for (;;)
  {
    if (platform_log_process_next(portMAX_DELAY))
    {
      task_health_mark_alive(&logger_health);
    }
    else
    {
      logger_health.error_count++;
    }
  }
}

static void monitor_log_task(const TaskStatus_t *status,
                             uint32_t total_runtime,
                             uint32_t now_tick)
{
  const task_health_t *health;
  uint32_t stack_free_bytes;
  uint32_t cpu_percent;

  health = task_health_from_handle(status->xHandle);
  stack_free_bytes = (uint32_t)status->usStackHighWaterMark *
                     (uint32_t)sizeof(StackType_t);
  cpu_percent = task_cpu_percent((uint32_t)status->ulRunTimeCounter,
                                 total_runtime);

  if (health != NULL)
  {
    LOG_INFO("MON", "%s P=%lu StackFree=%lu B CPU=%lu%% State=%s Alive=%u Loop=%lu Age=%lu ms Err=%lu",
             status->pcTaskName,
             (unsigned long)status->uxCurrentPriority,
             (unsigned long)stack_free_bytes,
             (unsigned long)cpu_percent,
             task_state_text(status->xHandle, status->eCurrentState),
             health->alive ? 1U : 0U,
             (unsigned long)health->loop_count,
             (unsigned long)(now_tick - health->last_run_tick),
             (unsigned long)health->error_count);
  }
  else
  {
    LOG_INFO("MON", "%s P=%lu StackFree=%lu B CPU=%lu%% State=%s",
             status->pcTaskName,
             (unsigned long)status->uxCurrentPriority,
             (unsigned long)stack_free_bytes,
             (unsigned long)cpu_percent,
             task_state_text(status->xHandle, status->eCurrentState));
  }
}

static void monitor_task(void *argument)
{
  TickType_t previous_wake_time;
  const TickType_t interval = pdMS_TO_TICKS(PROJECT_MONITOR_INTERVAL_MS);

  (void)argument;
  previous_wake_time = xTaskGetTickCount();
  LOG_INFO("TASK", "MonitorTask started");

  for (;;)
  {
    UBaseType_t expected_count;
    UBaseType_t snapshot_count;
    UBaseType_t index;
    uint32_t total_runtime = 0U;
    uint32_t now_tick;
    app_system_state_snapshot_t system_snapshot;
    can_service_stats_t can_stats;
    bsp_can_diagnostics_t can_diagnostics;
    netif_stats_t ethernet_stats;
    network_diag_t network_stats;
    time_diag_t time_stats;

    (void)xTaskDelayUntil(&previous_wake_time, interval);
    task_health_mark_alive(&monitor_health);

#if PROJECT_IPC_SELF_TEST_ENABLE
    if ((!can_notification_test_passed) && (!can_notification_test_pending))
    {
      can_notification_test_pending = true;
      if (xTaskNotifyGive(can_task_handle) != pdPASS)
      {
        monitor_health.error_count++;
      }
    }
#endif

    now_tick = (uint32_t)xTaskGetTickCount();
    expected_count = uxTaskGetNumberOfTasks();

    if (expected_count > PROJECT_MAX_MONITORED_TASKS)
    {
      monitor_stats_truncated_count++;
      LOG_WARN("MON", "task snapshot truncated: tasks=%lu capacity=%u",
               (unsigned long)expected_count,
               (unsigned int)PROJECT_MAX_MONITORED_TASKS);
      continue;
    }

    /* uxTaskGetSystemState() obtains the snapshot while the scheduler is
     * suspended, then returns before any UART formatting or transmission. */
    snapshot_count = uxTaskGetSystemState(task_status_array,
                                          PROJECT_MAX_MONITORED_TASKS,
                                          &total_runtime);
    if (snapshot_count == 0U)
    {
      monitor_stats_error_count++;
      LOG_WARN("MON", "task snapshot failed, errors=%lu",
               (unsigned long)monitor_stats_error_count);
      continue;
    }

    LOG_INFO("MON", "scheduler running uptime=%lu s tasks=%lu runtime=%lu ticks@10kHz",
             (unsigned long)(platform_time_get_ms() / 1000U),
             (unsigned long)snapshot_count,
             (unsigned long)total_runtime);

    for (index = 0U; index < snapshot_count; index++)
    {
      monitor_log_task(&task_status_array[index], total_runtime, now_tick);
    }

    LOG_INFO("MON", "net=%s stat_errors=%lu stat_truncated=%lu log_errors=%lu log_truncated=%lu",
             net_state_text(net_state),
             (unsigned long)monitor_stats_error_count,
             (unsigned long)monitor_stats_truncated_count,
             (unsigned long)platform_log_get_tx_error_count(),
             (unsigned long)platform_log_get_truncated_count());
    LOG_INFO("MON", "hwsec=%s rng_errors=%lu rtc_errors=%lu crc_errors=%lu",
             app_main_hardware_security_ready() ? "ready" : "failed",
             (unsigned long)bsp_rng_get_error_count(),
             (unsigned long)bsp_rtc_get_error_count(),
             (unsigned long)bsp_crc_get_error_count());
    LOG_INFO("MON", "IPC event_pub=%lu event_rx=%lu event_drop=%lu event_hwm=%lu/%u",
             (unsigned long)app_event_get_publish_count(),
             (unsigned long)app_event_get_receive_count(),
             (unsigned long)app_event_get_drop_count(),
             (unsigned long)app_event_get_high_water_mark(),
             (unsigned int)APP_EVENT_QUEUE_LENGTH);
    LOG_INFO("MON", "LOG queued=%lu sent=%lu drop=%lu trunc=%lu uart_err=%lu hwm=%lu/%u",
             (unsigned long)platform_log_get_queued_count(),
             (unsigned long)platform_log_get_sent_count(),
             (unsigned long)platform_log_get_drop_count(),
             (unsigned long)platform_log_get_truncated_count(),
             (unsigned long)platform_log_get_uart_error_count(),
             (unsigned long)platform_log_get_queue_high_water_mark(),
             (unsigned int)PROJECT_LOG_QUEUE_LENGTH);
    if (can_service_get_stats(&can_stats))
    {
      LOG_INFO("MON", "CAN state=%s rx_irq=%lu hal_rx=%lu queued=%lu processed=%lu ignored=%lu invalid=%lu drop=%lu ovrrun=%lu",
               can_service_state_text(can_stats.state),
               (unsigned long)can_stats.rx_irq_count,
               (unsigned long)can_stats.rx_hal_count,
               (unsigned long)can_stats.rx_queued_count,
               (unsigned long)can_stats.rx_processed_count,
               (unsigned long)can_stats.rx_ignored_count,
               (unsigned long)can_stats.rx_invalid_count,
               (unsigned long)can_stats.rx_ring_overflow_count,
               (unsigned long)can_stats.rx_fifo_overrun_count);
      LOG_INFO("MON", "CAN tx_req=%lu queued=%lu submit=%lu complete=%lu busy=%lu err=%lu qdrop=%lu irq_err=%lu busoff=%lu recover=%lu hwm=%lu/%lu",
               (unsigned long)can_stats.tx_request_count,
               (unsigned long)can_stats.tx_queued_count,
               (unsigned long)can_stats.tx_submit_count,
               (unsigned long)can_stats.tx_complete_count,
               (unsigned long)can_stats.tx_busy_count,
               (unsigned long)can_stats.tx_error_count,
               (unsigned long)can_stats.tx_queue_drop_count,
               (unsigned long)can_stats.error_irq_count,
               (unsigned long)can_stats.bus_off_count,
               (unsigned long)can_stats.recovery_success_count,
               (unsigned long)can_service_get_rx_ring_high_water_mark(),
               (unsigned long)can_service_get_rx_ring_capacity());
      if (bsp_can_get_diagnostics(&can_diagnostics))
      {
        LOG_INFO("MON", "CAN HW mode=%s hal_state=%lu hal_err=0x%08lX FIFO0=%lu TXfree=%lu RXpin=%lu",
                 (bsp_can_get_mode() == BSP_CAN_MODE_NORMAL) ?
                 "NORMAL" : "LOOPBACK",
                 (unsigned long)can_diagnostics.hal_state,
                 (unsigned long)can_diagnostics.hal_error,
                 (unsigned long)can_diagnostics.rx_fifo0_fill,
                 (unsigned long)can_diagnostics.tx_mailboxes_free,
                 (unsigned long)can_diagnostics.rx_pin_level);
        LOG_INFO("MON", "CAN REG ESR=%08lX MSR=%08lX TSR=%08lX RF0R=%08lX IER=%08lX TEC=%lu REC=%lu LEC=%lu",
                 (unsigned long)can_diagnostics.esr,
                 (unsigned long)can_diagnostics.msr,
                 (unsigned long)can_diagnostics.tsr,
                 (unsigned long)can_diagnostics.rf0r,
                 (unsigned long)can_diagnostics.ier,
                 (unsigned long)((can_diagnostics.esr >> 16U) & 0xFFU),
                 (unsigned long)((can_diagnostics.esr >> 24U) & 0xFFU),
                 (unsigned long)((can_diagnostics.esr >> 4U) & 0x07U));
      }
    }
    if (app_system_state_get_snapshot(&system_snapshot, 0U))
    {
      LOG_INFO("MON", "STATE events=%lu last=%u source=%u errors=%lu mutex_err=%lu bits=0x%02lX",
               (unsigned long)system_snapshot.processed_event_count,
               (unsigned int)system_snapshot.last_event,
               (unsigned int)system_snapshot.last_source,
               (unsigned long)(system_snapshot.event_error_count +
                               system_snapshot.can_error_count +
                               system_snapshot.network_error_count +
                               system_snapshot.security_error_count),
               (unsigned long)system_snapshot.mutex_error_count,
               (unsigned long)app_state_get_bits());
    }
    else
    {
      monitor_health.error_count++;
    }
    if (netif_stats_get(&ethernet_stats))
    {
      LOG_INFO("MON", "NETIF rx=%lu bytes=%lu pbuf_ok=%lu alloc_fail=%lu input_ok=%lu input_fail=%lu drop=%lu",
               (unsigned long)ethernet_stats.rx_frames,
               (unsigned long)ethernet_stats.rx_bytes,
               (unsigned long)ethernet_stats.rx_pbuf_alloc_ok,
               (unsigned long)ethernet_stats.rx_pbuf_alloc_fail,
               (unsigned long)ethernet_stats.rx_input_ok,
               (unsigned long)ethernet_stats.rx_input_fail,
               (unsigned long)ethernet_stats.rx_dropped);
      LOG_INFO("MON", "NETIF tx=%lu bytes=%lu tx_drop=%lu tx_err=%lu irq=%lu mbox_full=%lu cb_drop=%lu",
               (unsigned long)ethernet_stats.tx_frames,
               (unsigned long)ethernet_stats.tx_bytes,
               (unsigned long)ethernet_stats.tx_dropped,
               (unsigned long)ethernet_stats.tx_errors,
               (unsigned long)ethernet_stats.irq_count,
               (unsigned long)ethernet_stats.mbox_full,
               (unsigned long)ethernet_stats.callback_drop);
    }
    if (lwip_port_is_ready()) lwip_port_log_stats();
    network_diag_log_divider++;
    if ((network_diag_log_divider >= 2U) &&
        network_diag_get(&network_stats))
    {
      network_diag_log_divider = 0U;
      LOG_INFO("MON", "NET state=%s ARP rx=%lu tx=%lu ICMP req=%lu reply=%lu DHCP ok=%lu timeout=%lu fallback=%lu",
               app_network_state_text(app_network_state_get()),
               (unsigned long)network_stats.arp_rx,
               (unsigned long)network_stats.arp_tx,
               (unsigned long)network_stats.icmp_echo_rx,
               (unsigned long)network_stats.icmp_echo_tx,
               (unsigned long)network_stats.dhcp_success,
               (unsigned long)network_stats.dhcp_timeout,
               (unsigned long)network_stats.static_fallback_count);
      LOG_INFO("MON", "NET UDP rx=%lu/%luB tx=%lu/%luB err=%lu TCP accept=%lu active=%lu rx=%luB tx=%luB mem=%lu abort=%lu",
               (unsigned long)network_stats.udp_rx_packets,
               (unsigned long)network_stats.udp_rx_bytes,
               (unsigned long)network_stats.udp_tx_packets,
               (unsigned long)network_stats.udp_tx_bytes,
               (unsigned long)network_stats.udp_errors,
               (unsigned long)network_stats.tcp_accepts,
               (unsigned long)network_stats.tcp_active,
               (unsigned long)network_stats.tcp_rx_bytes,
               (unsigned long)network_stats.tcp_tx_bytes,
               (unsigned long)network_stats.tcp_write_err_mem,
               (unsigned long)network_stats.tcp_aborts);
      LOG_INFO("MON", "NET service start=%lu stop=%lu link_up=%lu link_down=%lu callback_drop=%lu",
               (unsigned long)network_stats.service_start_count,
               (unsigned long)network_stats.service_stop_count,
               (unsigned long)network_stats.link_up_count,
               (unsigned long)network_stats.link_down_count,
               (unsigned long)network_stats.callback_drop);
      net_transport_log_stats();
      if (time_diag_get(&time_stats))
      {
        LOG_INFO("MON", "TIME state=%s trusted=%u age=%lu DNS req=%lu ok=%lu fail=%lu timeout=%lu",
                 trusted_time_state_text(trusted_time_get_state()),
                 trusted_time_is_trusted() ? 1U : 0U,
                 (unsigned long)trusted_time_get_age_seconds(),
                 (unsigned long)time_stats.dns_requests,
                 (unsigned long)time_stats.dns_success,
                 (unsigned long)time_stats.dns_failures,
                 (unsigned long)time_stats.dns_timeouts);
        LOG_INFO("MON", "TIME SNTP start=%lu ok=%lu drop=%lu invalid=%lu RTC ok=%lu fail=%lu read_fail=%lu drift=%lu step=%lu back=%lu lost=%lu hold=%lu",
                 (unsigned long)time_stats.sntp_start_count,
                 (unsigned long)time_stats.sntp_sync_success,
                 (unsigned long)time_stats.sntp_sample_drop,
                 (unsigned long)time_stats.sntp_invalid_samples,
                 (unsigned long)time_stats.rtc_update_success,
                 (unsigned long)time_stats.rtc_update_fail,
                 (unsigned long)time_stats.rtc_read_fail,
                 (unsigned long)time_stats.drift_exceeded_count,
                 (unsigned long)time_stats.large_step_count,
                 (unsigned long)time_stats.backward_time_count,
                 (unsigned long)time_stats.trust_lost_count,
                 (unsigned long)time_stats.holdover_count);
      }
    }
  }
}

static bool app_task_intervals_valid(void)
{
  return (pdMS_TO_TICKS(PROJECT_SENSOR_INTERVAL_MS) != 0U) &&
         (pdMS_TO_TICKS(PROJECT_NET_INTERVAL_MS) != 0U) &&
         (pdMS_TO_TICKS(PROJECT_TELEMETRY_INTERVAL_MS) != 0U) &&
         (pdMS_TO_TICKS(PROJECT_MONITOR_INTERVAL_MS) != 0U) &&
         ((PROJECT_LED_TOGGLE_INTERVAL_MS % PROJECT_SENSOR_INTERVAL_MS) == 0U);
}

bool app_tasks_init(void)
{
  if (!app_task_intervals_valid())
  {
    return false;
  }

  task_health_init(&sensor_health);
  task_health_init(&can_health);
  task_health_init(&net_health);
  task_health_init(&netif_health);
  task_health_init(&security_health);
  task_health_init(&telemetry_health);
  task_health_init(&monitor_health);
  task_health_init(&logger_health);
  monitor_stats_error_count = 0U;
  monitor_stats_truncated_count = 0U;
  network_diag_log_divider = 0U;
  net_state = NET_STATE_DISABLED;
  can_notification_test_pending = false;
  can_notification_test_passed = false;
  bsp_led_init();

  if (net_transport_init() != NET_TRANSPORT_OK)
  {
    return false;
  }

  logger_task_handle = xTaskCreateStatic(logger_task, "Logger",
      PROJECT_LOGGER_TASK_STACK_WORDS, NULL, PROJECT_LOGGER_TASK_PRIORITY,
      logger_task_stack, &logger_task_tcb);
  if (logger_task_handle == NULL)
  {
    return false;
  }
  LOG_INFO("TASK", "LoggerTask created");

  if (!time_sync_task_create())
  {
    return false;
  }
  LOG_INFO("TASK", "TimeSyncTask created");

  sensor_task_handle = xTaskCreateStatic(sensor_task, "Sensor",
      PROJECT_SENSOR_TASK_STACK_WORDS, NULL, PROJECT_SENSOR_TASK_PRIORITY,
      sensor_task_stack, &sensor_task_tcb);
  if (sensor_task_handle == NULL)
  {
    return false;
  }

  can_task_handle = xTaskCreateStatic(can_process_task, "CanProcess",
      PROJECT_CAN_TASK_STACK_WORDS, NULL, PROJECT_CAN_TASK_PRIORITY,
      can_task_stack, &can_task_tcb);
  if (can_task_handle == NULL)
  {
    return false;
  }

  net_task_handle = xTaskCreateStatic(net_manager_task, "NetManager",
      PROJECT_NET_TASK_STACK_WORDS, NULL, PROJECT_NET_TASK_PRIORITY,
      net_task_stack, &net_task_tcb);
  if (net_task_handle == NULL)
  {
    return false;
  }

  netif_task_handle = xTaskCreateStatic(netif_driver_task, "NetIf",
      PROJECT_NETIF_TASK_STACK_WORDS, NULL, PROJECT_NETIF_TASK_PRIORITY,
      netif_task_stack, &netif_task_tcb);
  if (netif_task_handle == NULL)
  {
    return false;
  }

  security_task_handle = xTaskCreateStatic(security_task, "Security",
      PROJECT_SECURITY_TASK_STACK_WORDS, NULL, PROJECT_SECURITY_TASK_PRIORITY,
      security_task_stack, &security_task_tcb);
  if (security_task_handle == NULL)
  {
    return false;
  }

  telemetry_task_handle = xTaskCreateStatic(telemetry_task, "Telemetry",
      PROJECT_TELEMETRY_TASK_STACK_WORDS, NULL, PROJECT_TELEMETRY_TASK_PRIORITY,
      telemetry_task_stack, &telemetry_task_tcb);
  if (telemetry_task_handle == NULL)
  {
    return false;
  }

  monitor_task_handle = xTaskCreateStatic(monitor_task, "Monitor",
      PROJECT_MONITOR_TASK_STACK_WORDS, NULL, PROJECT_MONITOR_TASK_PRIORITY,
      monitor_task_stack, &monitor_task_tcb);
  if (monitor_task_handle == NULL)
  {
    return false;
  }

  return true;
}

TaskHandle_t app_tasks_get_can_task_handle(void)
{
  return can_task_handle;
}

TaskHandle_t app_tasks_get_security_task_handle(void)
{
  return security_task_handle;
}

TaskHandle_t app_tasks_get_net_task_handle(void)
{
  return net_task_handle;
}

TaskHandle_t app_tasks_get_netif_task_handle(void)
{
  return netif_task_handle;
}

bool app_tasks_notify_can(void)
{
  return (can_task_handle != NULL) &&
         (xTaskNotifyGive(can_task_handle) == pdPASS);
}

bool app_tasks_notify_can_from_isr(BaseType_t *higher_priority_task_woken)
{
  if ((can_task_handle == NULL) || (higher_priority_task_woken == NULL))
  {
    return false;
  }
  vTaskNotifyGiveFromISR(can_task_handle, higher_priority_task_woken);
  return true;
}

bool app_tasks_notify_net_from_isr(BaseType_t *higher_priority_task_woken)
{
  if ((net_task_handle == NULL) || (higher_priority_task_woken == NULL))
  {
    return false;
  }
  vTaskNotifyGiveFromISR(net_task_handle, higher_priority_task_woken);
  return true;
}

bool app_tasks_notify_netif(void)
{
  return (netif_task_handle != NULL) &&
         (xTaskNotifyGive(netif_task_handle) == pdPASS);
}

bool app_tasks_notify_netif_from_isr(BaseType_t *higher_priority_task_woken)
{
  if ((netif_task_handle == NULL) || (higher_priority_task_woken == NULL))
  {
    return false;
  }
  vTaskNotifyGiveFromISR(netif_task_handle, higher_priority_task_woken);
  return true;
}

bool app_tasks_notify_security(uint32_t notification_bits)
{
  if ((security_task_handle == NULL) ||
      ((notification_bits & SECURITY_NOTIFY_ALL) == 0U) ||
      ((notification_bits & ~SECURITY_NOTIFY_ALL) != 0U))
  {
    return false;
  }
  return xTaskNotify(security_task_handle,
                     notification_bits,
                     eSetBits) == pdPASS;
}

bool app_tasks_notify_security_from_isr(
  uint32_t notification_bits,
  BaseType_t *higher_priority_task_woken)
{
  if ((security_task_handle == NULL) ||
      (higher_priority_task_woken == NULL) ||
      ((notification_bits & SECURITY_NOTIFY_ALL) == 0U) ||
      ((notification_bits & ~SECURITY_NOTIFY_ALL) != 0U))
  {
    return false;
  }
  return xTaskNotifyFromISR(security_task_handle,
                            notification_bits,
                            eSetBits,
                            higher_priority_task_woken) == pdPASS;
}
