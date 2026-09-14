#include "app_event.h"

/* Keep queue messages compact and deterministic on the Cortex-M4 port. */
typedef char app_event_size_must_be_28_bytes[
  (sizeof(app_event_t) == 28U) ? 1 : -1];
typedef char app_can_frame_size_must_be_16_bytes[
  (sizeof(app_can_frame_t) == 16U) ? 1 : -1];
