#include "network_services.h"

#include "app_network_config.h"
#include "app_ipc.h"
#include "network_diag.h"
#include "platform_log.h"
#include "tcp_echo_raw.h"
#include "udp_echo_raw.h"

static bool services_running;

err_t network_services_start(void)
{
  err_t result;
  if (services_running) return ERR_OK;
  result = udp_echo_raw_start();
  if ((result != ERR_OK) && (result != ERR_ALREADY)) return result;
  result = tcp_echo_raw_start();
  if ((result != ERR_OK) && (result != ERR_ALREADY))
  {
    udp_echo_raw_stop();
    return result;
  }
  services_running = true;
  (void)app_state_set_bits(APP_STATE_BIT_SERVICES_READY);
  network_diag_add(NETWORK_DIAG_SERVICE_START, 1U);
  LOG_INFO("UDP", "UDP echo: LISTEN 0.0.0.0:%u",
           (unsigned int)APP_UDP_ECHO_PORT);
  LOG_INFO("TCP", "TCP echo: LISTEN 0.0.0.0:%u",
           (unsigned int)APP_TCP_ECHO_PORT);
  return ERR_OK;
}

void network_services_stop(bool force_abort)
{
  if ((!services_running) && (!udp_echo_raw_is_running()) &&
      (!tcp_echo_raw_is_running())) return;
  services_running = false;
  (void)app_state_clear_bits(APP_STATE_BIT_SERVICES_READY);
  tcp_echo_raw_stop(force_abort);
  udp_echo_raw_stop();
  network_diag_add(NETWORK_DIAG_SERVICE_STOP, 1U);
}

bool network_services_are_running(void)
{
  return services_running;
}
