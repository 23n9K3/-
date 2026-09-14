#ifndef APP_NETWORK_CONFIG_H
#define APP_NETWORK_CONFIG_H

typedef enum
{
  APP_NET_MODE_STATIC = 0,
  APP_NET_MODE_DHCP_WITH_STATIC_FALLBACK
} app_net_mode_t;

/* DNS/SNTP needs the active network's address, gateway and DNS service.
 * DHCP remains bounded and falls back to the static values below. */
#define APP_NET_MODE                    APP_NET_MODE_DHCP_WITH_STATIC_FALLBACK

#define APP_STATIC_IP0                  192U
#define APP_STATIC_IP1                  168U
#define APP_STATIC_IP2                  1U
#define APP_STATIC_IP3                  50U
#define APP_NETMASK0                    255U
#define APP_NETMASK1                    255U
#define APP_NETMASK2                    255U
#define APP_NETMASK3                    0U
#define APP_GATEWAY0                    192U
#define APP_GATEWAY1                    168U
#define APP_GATEWAY2                    1U
#define APP_GATEWAY3                    1U
#define APP_DNS_PRIMARY0                APP_GATEWAY0
#define APP_DNS_PRIMARY1                APP_GATEWAY1
#define APP_DNS_PRIMARY2                APP_GATEWAY2
#define APP_DNS_PRIMARY3                APP_GATEWAY3
#define APP_DNS_SECONDARY0              8U
#define APP_DNS_SECONDARY1              8U
#define APP_DNS_SECONDARY2              8U
#define APP_DNS_SECONDARY3              8U

#define APP_UDP_ECHO_PORT               5000U
#define APP_TCP_ECHO_PORT               5001U
#define APP_DHCP_TIMEOUT_MS             15000U
#define APP_TCP_MAX_CLIENTS             2U
#define APP_TCP_IDLE_MS                 30000U
#define APP_TCP_ECHO_BUFFER_SIZE        2048U
#define APP_UDP_ECHO_MAX_LENGTH         1472U
#define APP_NETWORK_POLICY_POLL_MS      250U

#endif /* APP_NETWORK_CONFIG_H */
