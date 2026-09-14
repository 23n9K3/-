#ifndef APP_NETWORK_STATE_H
#define APP_NETWORK_STATE_H

#include <stdbool.h>

#include "lwip/dhcp.h"
#include "lwip/netif.h"

typedef enum
{
  APP_NET_STATE_DOWN = 0,
  APP_NET_STATE_LINK_UP,
  APP_NET_STATE_STATIC_CONFIG,
  APP_NET_STATE_DHCP_START,
  APP_NET_STATE_DHCP_WAIT,
  APP_NET_STATE_IP_READY,
  APP_NET_STATE_SERVICES_READY,
  APP_NET_STATE_RECOVERING
} app_net_state_t;

void app_network_state_init(struct netif *netif, struct dhcp *dhcp);
void app_network_state_set_link(bool link_up);
void app_network_state_process(void);
app_net_state_t app_network_state_get(void);
const char *app_network_state_text(app_net_state_t state);

#endif /* APP_NETWORK_STATE_H */
