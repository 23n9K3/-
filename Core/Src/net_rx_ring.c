#include "net_rx_ring.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32l4xx.h"
#include "net_transport_types.h"

#define RING_GUARD_VALUE 0xA55A39C3UL
#define RING_MASK        (NET_TCP_RX_RING_SIZE - 1U)
#if (NET_TCP_RX_RING_SIZE == 0U) || \
    ((NET_TCP_RX_RING_SIZE & (NET_TCP_RX_RING_SIZE - 1U)) != 0U)
#error "NET_TCP_RX_RING_SIZE must be a power of two"
#endif

typedef struct
{
  uint32_t guard_before;
  uint8_t bytes[NET_TCP_RX_RING_SIZE];
  uint32_t guard_after;
} guarded_rx_storage_t;

static guarded_rx_storage_t storage;
static volatile uint32_t head_count;
static volatile uint32_t tail_count;

static void set_counts(uint32_t head, uint32_t tail)
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
  {
    head_count = head;
    tail_count = tail;
    __DMB();
  }
  else
  {
    taskENTER_CRITICAL();
    head_count = head;
    tail_count = tail;
    __DMB();
    taskEXIT_CRITICAL();
  }
}

void net_rx_ring_init(void)
{
  storage.guard_before = RING_GUARD_VALUE;
  storage.guard_after = RING_GUARD_VALUE;
  net_rx_ring_reset();
}

uint32_t net_rx_ring_used(void)
{
  uint32_t head = head_count;
  __DMB();
  return head - tail_count;
}

uint32_t net_rx_ring_free(void)
{
  return NET_TCP_RX_RING_SIZE - net_rx_ring_used();
}

bool net_rx_ring_write(const uint8_t *data, uint32_t length)
{
  uint32_t head;
  uint32_t first;
  if ((data == NULL) || (length == 0U) ||
      (length > NET_TCP_RX_RING_SIZE) || (length > net_rx_ring_free()))
  {
    return false;
  }
  head = head_count;
  first = NET_TCP_RX_RING_SIZE - (head & RING_MASK);
  if (first > length) first = length;
  memcpy(&storage.bytes[head & RING_MASK], data, first);
  if (length > first) memcpy(storage.bytes, &data[first], length - first);
  __DMB();
  head_count = head + length;
  return true;
}

uint32_t net_rx_ring_peek(uint8_t *data, uint32_t length)
{
  uint32_t available;
  uint32_t tail;
  uint32_t first;
  if ((data == NULL) || (length == 0U)) return 0U;
  available = net_rx_ring_used();
  if (length > available) length = available;
  tail = tail_count;
  first = NET_TCP_RX_RING_SIZE - (tail & RING_MASK);
  if (first > length) first = length;
  memcpy(data, &storage.bytes[tail & RING_MASK], first);
  if (length > first) memcpy(&data[first], storage.bytes, length - first);
  return length;
}

bool net_rx_ring_discard(uint32_t length)
{
  uint32_t tail;
  if ((length == 0U) || (length > net_rx_ring_used())) return false;
  tail = tail_count;
  __DMB();
  tail_count = tail + length;
  return true;
}

uint32_t net_rx_ring_read(uint8_t *data, uint32_t length)
{
  uint32_t copied = net_rx_ring_peek(data, length);
  if (copied != 0U) (void)net_rx_ring_discard(copied);
  return copied;
}

void net_rx_ring_reset(void)
{
  set_counts(0U, 0U);
}

bool net_rx_ring_self_test(void)
{
  uint8_t input[64];
  uint8_t output[64];
  uint32_t index;
  bool passed = true;
  net_rx_ring_reset();
  for (index = 0U; index < sizeof(input); index++) input[index] = (uint8_t)index;
  if (net_rx_ring_read(output, sizeof(output)) != 0U) passed = false;
  for (index = 0U; index < (NET_TCP_RX_RING_SIZE / sizeof(input)); index++)
  {
    if (!net_rx_ring_write(input, sizeof(input))) passed = false;
  }
  if (net_rx_ring_write(input, 1U)) passed = false;
  for (index = 0U; index < (NET_TCP_RX_RING_SIZE / sizeof(output)); index++)
  {
    if ((net_rx_ring_read(output, sizeof(output)) != sizeof(output)) ||
        (memcmp(input, output, sizeof(input)) != 0)) passed = false;
  }
  if (!net_rx_ring_write(input, 47U) ||
      (net_rx_ring_read(output, 31U) != 31U) ||
      !net_rx_ring_write(&input[47U], 17U) ||
      (net_rx_ring_read(output, 33U) != 33U)) passed = false;
  net_rx_ring_reset();
  for (index = 0U; index < 257U; index++)
  {
    uint32_t length = ((index * 37U) & 63U) + 1U;
    if (!net_rx_ring_write(input, length) ||
        (net_rx_ring_peek(output, length) != length) ||
        (memcmp(input, output, length) != 0) ||
        (net_rx_ring_read(output, length) != length) ||
        (memcmp(input, output, length) != 0)) passed = false;
  }
  set_counts(0xFFFFFFF0UL, 0xFFFFFFF0UL);
  if (!net_rx_ring_write(input, sizeof(input)) ||
      (net_rx_ring_read(output, sizeof(output)) != sizeof(output)) ||
      (memcmp(input, output, sizeof(input)) != 0)) passed = false;
  net_rx_ring_reset();
  if ((net_rx_ring_used() != 0U) ||
      (storage.guard_before != RING_GUARD_VALUE) ||
      (storage.guard_after != RING_GUARD_VALUE)) passed = false;
  return passed;
}
