#include "static_ring_buffer.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

static void ring_copy(uint8_t *destination,
                      const uint8_t *source,
                      size_t length)
{
  size_t index;
  for (index = 0U; index < length; index++)
  {
    destination[index] = source[index];
  }
}

static bool ring_is_valid(const static_ring_buffer_t *ring)
{
  return (ring != NULL) && (ring->storage != NULL) &&
         (ring->item_size != 0U) && (ring->capacity != 0U);
}

static bool ring_push_unlocked(static_ring_buffer_t *ring, const void *item)
{
  uint8_t *destination;

  if (ring->count == ring->capacity)
  {
    ring->drop_count++;
    return false;
  }

  destination = &ring->storage[ring->head * ring->item_size];
  ring_copy(destination, (const uint8_t *)item, ring->item_size);
  ring->head++;
  if (ring->head == ring->capacity)
  {
    ring->head = 0U;
  }
  ring->count++;
  ring->push_count++;
  if (ring->count > ring->high_water_mark)
  {
    ring->high_water_mark = ring->count;
  }
  return true;
}

static bool ring_pop_unlocked(static_ring_buffer_t *ring,
                              void *item,
                              bool remove_item)
{
  const uint8_t *source;

  if (ring->count == 0U)
  {
    return false;
  }

  source = &ring->storage[ring->tail * ring->item_size];
  ring_copy((uint8_t *)item, source, ring->item_size);
  if (remove_item)
  {
    ring->tail++;
    if (ring->tail == ring->capacity)
    {
      ring->tail = 0U;
    }
    ring->count--;
    ring->pop_count++;
  }
  return true;
}

bool static_ring_buffer_init(static_ring_buffer_t *ring,
                             void *storage,
                             size_t item_size,
                             size_t capacity)
{
  if ((ring == NULL) || (storage == NULL) ||
      (item_size == 0U) || (capacity == 0U) ||
      (capacity > (((size_t)-1) / item_size)))
  {
    return false;
  }

  ring->storage = (uint8_t *)storage;
  ring->item_size = item_size;
  ring->capacity = capacity;
  ring->head = 0U;
  ring->tail = 0U;
  ring->count = 0U;
  ring->high_water_mark = 0U;
  ring->push_count = 0U;
  ring->pop_count = 0U;
  ring->drop_count = 0U;
  return true;
}

bool static_ring_buffer_push(static_ring_buffer_t *ring, const void *item)
{
  bool result;

  if ((!ring_is_valid(ring)) || (item == NULL))
  {
    return false;
  }

  taskENTER_CRITICAL();
  result = ring_push_unlocked(ring, item);
  taskEXIT_CRITICAL();
  return result;
}

bool static_ring_buffer_push_from_isr(static_ring_buffer_t *ring,
                                      const void *item)
{
  UBaseType_t saved_interrupt_status;
  bool result;

  if ((!ring_is_valid(ring)) || (item == NULL))
  {
    return false;
  }

  saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
  result = ring_push_unlocked(ring, item);
  taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
  return result;
}

bool static_ring_buffer_pop(static_ring_buffer_t *ring, void *item)
{
  bool result;

  if ((!ring_is_valid(ring)) || (item == NULL))
  {
    return false;
  }

  taskENTER_CRITICAL();
  result = ring_pop_unlocked(ring, item, true);
  taskEXIT_CRITICAL();
  return result;
}

bool static_ring_buffer_peek(static_ring_buffer_t *ring, void *item)
{
  bool result;

  if ((!ring_is_valid(ring)) || (item == NULL))
  {
    return false;
  }

  taskENTER_CRITICAL();
  result = ring_pop_unlocked(ring, item, false);
  taskEXIT_CRITICAL();
  return result;
}

size_t static_ring_buffer_count(const static_ring_buffer_t *ring)
{
  size_t result = 0U;
  if (ring_is_valid(ring))
  {
    taskENTER_CRITICAL();
    result = ring->count;
    taskEXIT_CRITICAL();
  }
  return result;
}

size_t static_ring_buffer_capacity(const static_ring_buffer_t *ring)
{
  return ring_is_valid(ring) ? ring->capacity : 0U;
}

size_t static_ring_buffer_high_water_mark(const static_ring_buffer_t *ring)
{
  size_t result = 0U;
  if (ring_is_valid(ring))
  {
    taskENTER_CRITICAL();
    result = ring->high_water_mark;
    taskEXIT_CRITICAL();
  }
  return result;
}

uint32_t static_ring_buffer_push_count(const static_ring_buffer_t *ring)
{
  return ring_is_valid(ring) ? ring->push_count : 0U;
}

uint32_t static_ring_buffer_pop_count(const static_ring_buffer_t *ring)
{
  return ring_is_valid(ring) ? ring->pop_count : 0U;
}

uint32_t static_ring_buffer_drop_count(const static_ring_buffer_t *ring)
{
  return ring_is_valid(ring) ? ring->drop_count : 0U;
}

bool static_ring_buffer_self_test(uint32_t *test_drop_count)
{
  static static_ring_buffer_t test_ring;
  static static_ring_buffer_t one_item_ring;
  static uint32_t test_storage[3];
  static uint32_t one_item_storage[1];
  uint32_t value;
  uint32_t expected;

  if (test_drop_count == NULL)
  {
    return false;
  }
  *test_drop_count = 0U;

  if (static_ring_buffer_init(NULL, test_storage, sizeof(uint32_t), 3U) ||
      static_ring_buffer_init(&test_ring, NULL, sizeof(uint32_t), 3U) ||
      static_ring_buffer_init(&test_ring, test_storage, 0U, 3U) ||
      static_ring_buffer_init(&test_ring, test_storage, sizeof(uint32_t), 0U) ||
      (!static_ring_buffer_init(&test_ring, test_storage,
                                sizeof(uint32_t), 3U)))
  {
    return false;
  }

  if (static_ring_buffer_pop(&test_ring, &value))
  {
    return false;
  }
  for (value = 1U; value <= 3U; value++)
  {
    if (!static_ring_buffer_push(&test_ring, &value))
    {
      return false;
    }
  }
  value = 99U;
  if (static_ring_buffer_push(&test_ring, &value))
  {
    return false;
  }
  *test_drop_count = static_ring_buffer_drop_count(&test_ring);
  for (expected = 1U; expected <= 3U; expected++)
  {
    if ((!static_ring_buffer_pop(&test_ring, &value)) || (value != expected))
    {
      return false;
    }
  }

  /* Force head/tail wrap and verify FIFO order after reuse. */
  value = 4U;
  if ((!static_ring_buffer_push(&test_ring, &value)) ||
      (!static_ring_buffer_peek(&test_ring, &value)) || (value != 4U) ||
      (!static_ring_buffer_pop(&test_ring, &value)) || (value != 4U))
  {
    return false;
  }

  if (!static_ring_buffer_init(&one_item_ring, one_item_storage,
                               sizeof(uint32_t), 1U))
  {
    return false;
  }
  value = 7U;
  if ((!static_ring_buffer_push(&one_item_ring, &value)) ||
      static_ring_buffer_push(&one_item_ring, &value) ||
      (!static_ring_buffer_pop(&one_item_ring, &value)) || (value != 7U))
  {
    return false;
  }

  return (*test_drop_count == 1U);
}
