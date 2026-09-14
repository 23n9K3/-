#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_can.h"

typedef enum
{
  CAN_PROTOCOL_RESULT_HANDLED = 0,
  CAN_PROTOCOL_RESULT_IGNORED,
  CAN_PROTOCOL_RESULT_INVALID
} can_protocol_result_t;

bool can_protocol_start(bsp_can_mode_t mode);
can_protocol_result_t can_protocol_process_frame(
  const bsp_can_frame_t *frame);
void can_protocol_tx_completed(const bsp_can_frame_t *frame);
void can_protocol_poll(void);
uint32_t can_protocol_get_loopback_pass_count(void);
bool can_protocol_loopback_passed(void);

#endif /* CAN_PROTOCOL_H */
