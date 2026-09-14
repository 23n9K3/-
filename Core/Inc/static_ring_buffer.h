#ifndef STATIC_RING_BUFFER_H
#define STATIC_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint8_t *storage;
  size_t item_size;
  size_t capacity;
  size_t head;
  size_t tail;
  size_t count;
  size_t high_water_mark;
  uint32_t push_count;
  uint32_t pop_count;
  uint32_t drop_count;
} static_ring_buffer_t;

bool static_ring_buffer_init(static_ring_buffer_t *ring,
                             void *storage,
                             size_t item_size,
                             size_t capacity);
bool static_ring_buffer_push(static_ring_buffer_t *ring, const void *item);
bool static_ring_buffer_push_from_isr(static_ring_buffer_t *ring,
                                      const void *item);
bool static_ring_buffer_pop(static_ring_buffer_t *ring, void *item);
bool static_ring_buffer_peek(static_ring_buffer_t *ring, void *item);
size_t static_ring_buffer_count(const static_ring_buffer_t *ring);
size_t static_ring_buffer_capacity(const static_ring_buffer_t *ring);
size_t static_ring_buffer_high_water_mark(const static_ring_buffer_t *ring);
uint32_t static_ring_buffer_push_count(const static_ring_buffer_t *ring);
uint32_t static_ring_buffer_pop_count(const static_ring_buffer_t *ring);
uint32_t static_ring_buffer_drop_count(const static_ring_buffer_t *ring);

bool static_ring_buffer_self_test(uint32_t *test_drop_count);

#endif /* STATIC_RING_BUFFER_H */
