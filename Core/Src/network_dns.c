#include "network_dns.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/tcpip.h"
#include "time_diag.h"
#include "time_sync_task.h"

#define NETWORK_DNS_CONTEXT_COUNT 4U

typedef struct
{
  bool in_use;
  uint32_t generation;
  char hostname[DNS_MAX_NAME_LENGTH];
} dns_request_context_t;

static dns_request_context_t contexts[NETWORK_DNS_CONTEXT_COUNT];
static network_dns_result_t completed_result;
static volatile bool result_available;
static volatile uint32_t active_generation;

static void publish_result(dns_request_context_t *context,
                           const ip_addr_t *address)
{
  bool current;
  taskENTER_CRITICAL();
  current = context->generation == active_generation;
  if (current)
  {
    completed_result.generation = context->generation;
    completed_result.success = address != NULL;
    if (address != NULL)
      completed_result.address = *address;
    else
      ip_addr_set_zero(&completed_result.address);
    result_available = true;
  }
  context->in_use = false;
  taskEXIT_CRITICAL();
  if (current) time_sync_task_signal(TIME_SYNC_NOTIFY_DNS_RESULT);
}

static void network_dns_found(const char *hostname,
                              const ip_addr_t *address, void *argument)
{
  dns_request_context_t *context = (dns_request_context_t *)argument;
  if ((context == NULL) || (!context->in_use)) return;
  if ((hostname == NULL) ||
      (strncmp(hostname, context->hostname, DNS_MAX_NAME_LENGTH) != 0))
  {
    publish_result(context, NULL);
    return;
  }
  publish_result(context, address);
}

static void dns_request_callback(void *argument)
{
  dns_request_context_t *context = (dns_request_context_t *)argument;
  ip_addr_t address;
  err_t result;
  if ((context == NULL) || (!context->in_use) ||
      (context->generation != active_generation))
  {
    if (context != NULL)
    {
      taskENTER_CRITICAL();
      context->in_use = false;
      taskEXIT_CRITICAL();
    }
    return;
  }
  ip_addr_set_zero(&address);
  result = dns_gethostbyname(context->hostname, &address,
                             network_dns_found, context);
  if (result == ERR_OK)
    publish_result(context, &address);
  else if (result != ERR_INPROGRESS)
    publish_result(context, NULL);
}

void network_dns_init(void)
{
  memset(contexts, 0, sizeof(contexts));
  memset(&completed_result, 0, sizeof(completed_result));
  result_available = false;
  active_generation = 0U;
}

bool network_dns_request(const char *hostname, uint32_t generation)
{
  dns_request_context_t *context = NULL;
  uint32_t index;
  size_t length;
  if (hostname == NULL) return false;
  length = strlen(hostname);
  if ((length == 0U) || (length >= DNS_MAX_NAME_LENGTH)) return false;
  taskENTER_CRITICAL();
  active_generation = generation;
  result_available = false;
  for (index = 0U; index < NETWORK_DNS_CONTEXT_COUNT; index++)
  {
    if (!contexts[index].in_use)
    {
      context = &contexts[index];
      context->in_use = true;
      context->generation = generation;
      memcpy(context->hostname, hostname, length + 1U);
      break;
    }
  }
  taskEXIT_CRITICAL();
  if (context == NULL) return false;
  if (tcpip_try_callback(dns_request_callback, context) != ERR_OK)
  {
    taskENTER_CRITICAL();
    context->in_use = false;
    taskEXIT_CRITICAL();
    return false;
  }
  time_diag_add(TIME_DIAG_DNS_REQUEST, 1U);
  return true;
}

void network_dns_cancel(uint32_t new_generation)
{
  taskENTER_CRITICAL();
  active_generation = new_generation;
  result_available = false;
  taskEXIT_CRITICAL();
}

bool network_dns_take_result(network_dns_result_t *result)
{
  bool available;
  if (result == NULL) return false;
  taskENTER_CRITICAL();
  available = result_available;
  if (available)
  {
    *result = completed_result;
    result_available = false;
  }
  taskEXIT_CRITICAL();
  return available;
}
