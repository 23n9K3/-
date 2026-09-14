#include "can_protocol.h"

#include <string.h>

#include "can_service.h"
#include "platform_log.h"
#include "project_config.h"
#include "task.h"

#define CAN_TEST_REQUEST_ID  0x123U
#define CAN_TEST_RESPONSE_ID 0x321U

static const uint8_t external_test_data[8] =
  {0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};

static bsp_can_mode_t protocol_mode;
static bool loopback_active;
static bool loopback_passed;
static bool external_rx_reported;
static bool external_tx_reported;
static uint32_t loopback_pass_count;
static TickType_t loopback_deadline;
#if PROJECT_CAN_BUSOFF_TEST_ENABLE
static TickType_t busoff_test_next_tick;
#endif

static void can_protocol_make_external_response(bsp_can_frame_t *frame)
{
  memset(frame, 0, sizeof(*frame));
  frame->id = CAN_TEST_RESPONSE_ID;
  frame->id_type = BSP_CAN_ID_STANDARD;
  frame->frame_type = BSP_CAN_FRAME_DATA;
  frame->dlc = sizeof(external_test_data);
  memcpy(frame->data, external_test_data, sizeof(external_test_data));
}

static void can_protocol_make_loopback_frame(uint32_t sequence,
                                             bsp_can_frame_t *frame)
{
  memset(frame, 0, sizeof(*frame));
  frame->id = CAN_TEST_REQUEST_ID;
  frame->id_type = BSP_CAN_ID_STANDARD;
  frame->frame_type = BSP_CAN_FRAME_DATA;
  frame->dlc = 8U;
  frame->data[0] = (uint8_t)sequence;
  frame->data[1] = (uint8_t)(sequence >> 8);
  frame->data[2] = (uint8_t)(sequence >> 16);
  frame->data[3] = (uint8_t)(sequence >> 24);
  frame->data[4] = 0xA5U;
  frame->data[5] = 0x5AU;
  frame->data[6] = 0xC3U;
  frame->data[7] = 0x3CU;
}

static bool can_protocol_queue_loopback_frame(uint32_t sequence)
{
  bsp_can_frame_t frame;
  can_protocol_make_loopback_frame(sequence, &frame);
  return can_service_send_async(&frame, 0U);
}

static bool can_protocol_loopback_frame_matches(
  const bsp_can_frame_t *frame,
  uint32_t sequence)
{
  bsp_can_frame_t expected;
  can_protocol_make_loopback_frame(sequence, &expected);
  return (frame->id == expected.id) &&
         (frame->id_type == expected.id_type) &&
         (frame->frame_type == expected.frame_type) &&
         (frame->dlc == expected.dlc) &&
         (memcmp(frame->data, expected.data, sizeof(expected.data)) == 0);
}

bool can_protocol_start(bsp_can_mode_t mode)
{
  protocol_mode = mode;
  loopback_active = false;
  loopback_passed = false;
  external_rx_reported = false;
  external_tx_reported = false;
  loopback_pass_count = 0U;

  if (mode != BSP_CAN_MODE_INTERNAL_LOOPBACK)
  {
#if PROJECT_CAN_BUSOFF_TEST_ENABLE
    busoff_test_next_tick = xTaskGetTickCount();
#endif
    return true;
  }

  loopback_active = true;
  loopback_deadline = xTaskGetTickCount() +
                      pdMS_TO_TICKS(PROJECT_CAN_LOOPBACK_TIMEOUT_MS);
  if (!can_protocol_queue_loopback_frame(0U))
  {
    loopback_active = false;
    LOG_ERROR("CAN", "Loopback initial frame queue failed");
    return false;
  }
  return true;
}

can_protocol_result_t can_protocol_process_frame(
  const bsp_can_frame_t *frame)
{
  bsp_can_frame_t response;

  if (!bsp_can_frame_is_valid(frame))
  {
    return CAN_PROTOCOL_RESULT_INVALID;
  }

  if (protocol_mode == BSP_CAN_MODE_INTERNAL_LOOPBACK)
  {
    if ((!loopback_active) ||
        (!can_protocol_loopback_frame_matches(frame, loopback_pass_count)))
    {
      if (!loopback_passed)
      {
        loopback_active = false;
        LOG_ERROR("CAN", "CAN loopback: FAIL at frame %lu",
                  (unsigned long)loopback_pass_count);
      }
      return CAN_PROTOCOL_RESULT_INVALID;
    }

    loopback_pass_count++;
    loopback_deadline = xTaskGetTickCount() +
                        pdMS_TO_TICKS(PROJECT_CAN_LOOPBACK_TIMEOUT_MS);
    if (loopback_pass_count >= PROJECT_CAN_LOOPBACK_FRAME_COUNT)
    {
      loopback_active = false;
      loopback_passed = true;
      LOG_INFO("CAN", "Loopback frames: %lu/%u",
               (unsigned long)loopback_pass_count,
               (unsigned int)PROJECT_CAN_LOOPBACK_FRAME_COUNT);
      LOG_INFO("CAN", "CAN loopback: PASS");
    }
    else if (!can_protocol_queue_loopback_frame(loopback_pass_count))
    {
      loopback_active = false;
      LOG_ERROR("CAN", "Loopback TX queue failed at frame %lu",
                (unsigned long)loopback_pass_count);
      return CAN_PROTOCOL_RESULT_INVALID;
    }
    return CAN_PROTOCOL_RESULT_HANDLED;
  }

  if ((frame->id_type == BSP_CAN_ID_STANDARD) &&
      (frame->frame_type == BSP_CAN_FRAME_DATA) &&
      (frame->id == CAN_TEST_REQUEST_ID) &&
      (frame->dlc == sizeof(external_test_data)) &&
      (memcmp(frame->data, external_test_data,
              sizeof(external_test_data)) == 0))
  {
    if (!external_rx_reported)
    {
      external_rx_reported = true;
      LOG_INFO("CAN", "External RX id=0x%03lX dlc=%u",
               (unsigned long)frame->id, (unsigned int)frame->dlc);
      LOG_INFO("CAN", "CAN external RX: PASS");
    }
    can_protocol_make_external_response(&response);
    if (!can_service_send_async(&response, 0U))
    {
      LOG_WARN("CAN", "Response queue full id=0x321");
      return CAN_PROTOCOL_RESULT_INVALID;
    }
    LOG_INFO("CAN", "Response queued id=0x321");
    return CAN_PROTOCOL_RESULT_HANDLED;
  }

  return CAN_PROTOCOL_RESULT_IGNORED;
}

void can_protocol_tx_completed(const bsp_can_frame_t *frame)
{
  if ((protocol_mode == BSP_CAN_MODE_NORMAL) &&
      (!external_tx_reported) && (frame != NULL) &&
      (frame->id_type == BSP_CAN_ID_STANDARD) &&
      (frame->id == CAN_TEST_RESPONSE_ID))
  {
    external_tx_reported = true;
    LOG_INFO("CAN", "CAN external TX: PASS");
  }
}

void can_protocol_poll(void)
{
  TickType_t now;
#if PROJECT_CAN_BUSOFF_TEST_ENABLE
  bsp_can_frame_t test_frame;

  if (protocol_mode == BSP_CAN_MODE_NORMAL)
  {
    now = xTaskGetTickCount();
    if ((int32_t)(now - busoff_test_next_tick) >= 0)
    {
      can_protocol_make_external_response(&test_frame);
      (void)can_service_send_async(&test_frame, 0U);
      busoff_test_next_tick = now + pdMS_TO_TICKS(100U);
    }
    return;
  }
#endif
  if (!loopback_active)
  {
    return;
  }
  now = xTaskGetTickCount();
  if ((int32_t)(now - loopback_deadline) >= 0)
  {
    loopback_active = false;
    LOG_ERROR("CAN", "CAN loopback: TIMEOUT at frame %lu",
              (unsigned long)loopback_pass_count);
  }
}

uint32_t can_protocol_get_loopback_pass_count(void)
{
  return loopback_pass_count;
}

bool can_protocol_loopback_passed(void)
{
  return loopback_passed;
}
