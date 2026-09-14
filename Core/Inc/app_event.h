#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdint.h>

#include "FreeRTOS.h"

typedef enum
{
  APP_EVENT_NONE = 0,
  APP_EVENT_SYSTEM_READY,
  APP_EVENT_SYSTEM_ERROR,
  APP_EVENT_CAN_READY,
  APP_EVENT_CAN_RX,
  APP_EVENT_CAN_ERROR,
  APP_EVENT_CAN_BUS_OFF,
  APP_EVENT_CAN_RECOVERED,
  APP_EVENT_NET_ENC_READY,
  APP_EVENT_NET_LINK_UP,
  APP_EVENT_NET_LINK_DOWN,
  APP_EVENT_NET_IP_READY,
  APP_EVENT_NET_ERROR,
  APP_EVENT_TIME_SYNCED,
  APP_EVENT_TIME_TRUST_LOST,
  APP_EVENT_SECURITY_REQUEST,
  APP_EVENT_SECURITY_READY,
  APP_EVENT_SECURITY_ERROR,
  APP_EVENT_TLS_CONNECTED,
  APP_EVENT_TLS_DISCONNECTED,
  APP_EVENT_TLS_ERROR,
  APP_EVENT_TELEMETRY_READY,
  APP_EVENT_TELEMETRY_ERROR
} app_event_type_t;

typedef enum
{
  APP_EVENT_SOURCE_SYSTEM = 0,
  APP_EVENT_SOURCE_SENSOR,
  APP_EVENT_SOURCE_CAN,
  APP_EVENT_SOURCE_NETWORK,
  APP_EVENT_SOURCE_SECURITY,
  APP_EVENT_SOURCE_TELEMETRY,
  APP_EVENT_SOURCE_MONITOR,
  APP_EVENT_SOURCE_TIME
} app_event_source_t;

typedef struct
{
  uint32_t can_id;
  uint8_t length;
  uint8_t data[8];
} app_can_frame_t;

typedef struct
{
  app_event_type_t type;
  app_event_source_t source;
  TickType_t tick;
  uint32_t sequence;
  union
  {
    struct
    {
      uint32_t code;
      uint32_t detail;
    } error;
    struct
    {
      uint32_t address;
      uint32_t netmask;
      uint32_t gateway;
    } network;
    app_can_frame_t can;
    struct
    {
      uint32_t value0;
      uint32_t value1;
    } generic;
  } payload;
} app_event_t;

#endif /* APP_EVENT_H */
