#include "raw_eth_test.h"

#include <string.h>

#include "bsp_crc.h"
#include "bsp_enc28j60.h"
#include "enc28j60_port.h"
#include "enc28j60_regs.h"
#include "platform_log.h"
#include "project_config.h"

#define ETH_HEADER_SIZE             14U
#define ETH_DEST_OFFSET             0U
#define ETH_SOURCE_OFFSET           6U
#define ETH_TYPE_OFFSET             12U
#define RAW_MAGIC_OFFSET            ETH_HEADER_SIZE
#define RAW_MAGIC_LENGTH            19U
#define RAW_SEQUENCE_OFFSET         (RAW_MAGIC_OFFSET + RAW_MAGIC_LENGTH)
#define RAW_PAYLOAD_LENGTH_OFFSET   (RAW_SEQUENCE_OFFSET + 4U)
#define RAW_FLAGS_OFFSET            (RAW_PAYLOAD_LENGTH_OFFSET + 2U)
#define RAW_DATA_OFFSET             (RAW_FLAGS_OFFSET + 1U)
#define RAW_TEST_DATA_LENGTH        16U
#define RAW_CRC_OFFSET              (RAW_DATA_OFFSET + RAW_TEST_DATA_LENGTH)
#define RAW_FRAME_LENGTH            (RAW_CRC_OFFSET + 4U)
#define RAW_FLAG_RESPONSE           0x01U

static const uint8_t raw_magic[RAW_MAGIC_LENGTH] =
  {'I','O','T','P','R','O','J','E','C','T','-','E','N','C','2','8','J','6','0'};
static const uint8_t raw_test_data[RAW_TEST_DATA_LENGTH] =
  {0x00U,0x11U,0x22U,0x33U,0x44U,0x55U,0x66U,0x77U,
   0x88U,0x99U,0xAAU,0xBBU,0xCCU,0xDDU,0xEEU,0xFFU};

static uint8_t rx_frame[PROJECT_ENC_FRAME_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t tx_frame[PROJECT_ENC_FRAME_BUFFER_SIZE] __attribute__((aligned(4)));
static raw_eth_test_stats_t test_statistics;
static bool test_initialized;
static bool stable_link_up;
static bool candidate_link_up;
static uint8_t candidate_link_count;
static uint32_t next_link_poll_tick;
static uint32_t next_tx_tick;
static uint32_t next_stats_tick;
static uint32_t tx_sequence;

typedef char raw_rx_buffer_size_check[
  (sizeof(rx_frame) >= ENC28J60_MAX_FRAME_LENGTH) ? 1 : -1];
typedef char raw_tx_buffer_size_check[
  (sizeof(tx_frame) >= ENC28J60_MAX_FRAME_LENGTH) ? 1 : -1];

static void write_be16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value >> 8U);
  data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24U);
  data[1] = (uint8_t)(value >> 16U);
  data[2] = (uint8_t)(value >> 8U);
  data[3] = (uint8_t)value;
}

static uint16_t read_be16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24U) |
         ((uint32_t)data[1] << 16U) |
         ((uint32_t)data[2] << 8U) |
         (uint32_t)data[3];
}

static bool compute_frame_crc(const uint8_t *frame, uint32_t *crc)
{
  return bsp_crc32_compute(&frame[RAW_MAGIC_OFFSET],
                           RAW_CRC_OFFSET - RAW_MAGIC_OFFSET,
                           crc) == BSP_CRC_STATUS_OK;
}

static uint16_t build_test_frame(uint32_t sequence)
{
  uint8_t mac[6];
  uint32_t crc;

  memset(tx_frame, 0, RAW_FRAME_LENGTH);
  memset(&tx_frame[ETH_DEST_OFFSET], 0xFF, 6U);
  ENC28J60_GetMacAddress(mac);
  memcpy(&tx_frame[ETH_SOURCE_OFFSET], mac, 6U);
  write_be16(&tx_frame[ETH_TYPE_OFFSET], RAW_ETH_TEST_TYPE);
  memcpy(&tx_frame[RAW_MAGIC_OFFSET], raw_magic, sizeof(raw_magic));
  write_be32(&tx_frame[RAW_SEQUENCE_OFFSET], sequence);
  write_be16(&tx_frame[RAW_PAYLOAD_LENGTH_OFFSET], RAW_TEST_DATA_LENGTH);
  tx_frame[RAW_FLAGS_OFFSET] = 0U;
  memcpy(&tx_frame[RAW_DATA_OFFSET], raw_test_data, sizeof(raw_test_data));
  if (!compute_frame_crc(tx_frame, &crc))
  {
    return 0U;
  }
  write_be32(&tx_frame[RAW_CRC_OFFSET], crc);
  memset(mac, 0, sizeof(mac));
  return RAW_FRAME_LENGTH;
}

static bool validate_test_frame(const uint8_t *frame,
                                uint16_t length,
                                uint32_t *sequence)
{
  uint32_t expected_crc;
  uint32_t received_crc;

  if ((length != RAW_FRAME_LENGTH) ||
      (read_be16(&frame[ETH_TYPE_OFFSET]) != RAW_ETH_TEST_TYPE) ||
      (memcmp(&frame[RAW_MAGIC_OFFSET], raw_magic, sizeof(raw_magic)) != 0) ||
      (read_be16(&frame[RAW_PAYLOAD_LENGTH_OFFSET]) != RAW_TEST_DATA_LENGTH) ||
      (memcmp(&frame[RAW_DATA_OFFSET], raw_test_data,
              sizeof(raw_test_data)) != 0) ||
      (!compute_frame_crc(frame, &expected_crc)))
  {
    return false;
  }
  received_crc = read_be32(&frame[RAW_CRC_OFFSET]);
  if (expected_crc != received_crc)
  {
    return false;
  }
  *sequence = read_be32(&frame[RAW_SEQUENCE_OFFSET]);
  return true;
}

static void echo_test_frame(const uint8_t *frame, uint16_t length)
{
  uint8_t mac[6];
  uint32_t crc;

  if (length > sizeof(tx_frame))
  {
    test_statistics.test_tx_fail++;
    return;
  }
  memcpy(tx_frame, frame, length);
  memcpy(&tx_frame[ETH_DEST_OFFSET], &frame[ETH_SOURCE_OFFSET], 6U);
  ENC28J60_GetMacAddress(mac);
  memcpy(&tx_frame[ETH_SOURCE_OFFSET], mac, 6U);
  tx_frame[RAW_FLAGS_OFFSET] = RAW_FLAG_RESPONSE;
  if (!compute_frame_crc(tx_frame, &crc))
  {
    test_statistics.test_tx_fail++;
    return;
  }
  write_be32(&tx_frame[RAW_CRC_OFFSET], crc);
  if (ENC28J60_SendFrame(tx_frame, length) == ENC28J60_OK)
  {
    test_statistics.echoed_frames++;
  }
  else
  {
    test_statistics.test_tx_fail++;
  }
  memset(mac, 0, sizeof(mac));
}

static void process_received_frames(void)
{
  uint32_t frame_index;

  for (frame_index = 0U;
       frame_index < PROJECT_ENC_MAX_RX_PER_RUN;
       frame_index++)
  {
    uint16_t length = 0U;
    uint32_t sequence;
    enc28j60_status_t status;

    if (ENC28J60_GetPendingPacketCount() == 0U)
    {
      break;
    }
    status = ENC28J60_ReceiveFrame(rx_frame, sizeof(rx_frame), &length);
    if ((status != ENC28J60_OK) || (length < ETH_HEADER_SIZE))
    {
      test_statistics.test_rx_fail++;
      LOG_WARN("ENC", "ENC28J60 RX %s length=%u",
               ENC28J60_StatusText(status), (unsigned int)length);
      continue;
    }
    if (read_be16(&rx_frame[ETH_TYPE_OFFSET]) != RAW_ETH_TEST_TYPE)
    {
      test_statistics.other_ethertype_frames++;
      continue;
    }
    if (!validate_test_frame(rx_frame, length, &sequence))
    {
      test_statistics.test_rx_fail++;
      LOG_WARN("ETH", "Raw frame RX validation failed");
      continue;
    }
    test_statistics.test_rx_pass++;
    test_statistics.last_rx_sequence = sequence;
    LOG_INFO("ETH", "Raw frame RX: PASS seq=%lu",
             (unsigned long)sequence);
    if ((rx_frame[RAW_FLAGS_OFFSET] & RAW_FLAG_RESPONSE) == 0U)
    {
      echo_test_frame(rx_frame, length);
    }
  }
}

static void poll_link(uint32_t now)
{
  bool link_up;
  enc28j60_status_t status;

  if ((int32_t)(now - next_link_poll_tick) < 0)
  {
    return;
  }
  next_link_poll_tick = now + PROJECT_ENC_LINK_POLL_MS;
  status = ENC28J60_GetLinkState(&link_up);
  if (status != ENC28J60_OK)
  {
    LOG_WARN("ENC", "PHY read failed: %s", ENC28J60_StatusText(status));
    return;
  }
  if (link_up != candidate_link_up)
  {
    candidate_link_up = link_up;
    candidate_link_count = 1U;
    return;
  }
  if (candidate_link_count < 2U)
  {
    candidate_link_count++;
  }
  if ((candidate_link_count >= 2U) && (stable_link_up != candidate_link_up))
  {
    stable_link_up = candidate_link_up;
    ENC28J60_RecordLinkChange(stable_link_up);
    if (stable_link_up)
    {
      next_tx_tick = now;
      LOG_INFO("ENC", "PHY Link: UP");
    }
    else
    {
      LOG_INFO("ENC", "PHY Link: DOWN");
    }
  }
}

bool raw_eth_test_init(void)
{
  uint8_t mac[6];
  uint8_t revision = 0U;
  bool link_up = false;
  enc28j60_status_t status;
  uint32_t now;

  memset(&test_statistics, 0, sizeof(test_statistics));
  test_initialized = false;
  LOG_INFO("ENC", "ENC28J60 driver start");
  LOG_INFO("ENC", "SPI1 clock: %lu Hz",
           (unsigned long)enc28j60_port_get_spi_clock_hz());
  if (!enc28j60_port_pins_are_valid())
  {
    LOG_ERROR("ENC", "CubeMX pin config invalid: require PA5 SCK and PF15 falling EXTI");
  }
  status = ENC28J60_Init();
  if (status != ENC28J60_OK)
  {
    if (status == ENC28J60_ERROR_REVISION)
    {
      LOG_ERROR("ENC", "ENC28J60 invalid revision: 0x%02X",
                (unsigned int)ENC28J60_GetStats()->revision);
    }
    else if (status == ENC28J60_ERROR_TIMEOUT)
    {
      LOG_ERROR("ENC", "ENC28J60 oscillator/MII timeout");
    }
    else if (status == ENC28J60_ERROR_SPI)
    {
      LOG_ERROR("ENC", "ENC28J60 SPI communication error");
    }
    else
    {
      LOG_ERROR("ENC", "ENC28J60 init failed: %s",
                ENC28J60_StatusText(status));
    }
    return false;
  }
  status = ENC28J60_ReadRevision(&revision);
  if (status != ENC28J60_OK)
  {
    LOG_ERROR("ENC", "ENC28J60 revision read failed: %s",
              ENC28J60_StatusText(status));
    return false;
  }
  LOG_INFO("ENC", "ENC28J60 revision: 0x%02X", (unsigned int)revision);
  LOG_INFO("ENC", "SPI test: PASS");
  ENC28J60_GetMacAddress(mac);
  LOG_INFO("ENC", "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned int)mac[0], (unsigned int)mac[1],
           (unsigned int)mac[2], (unsigned int)mac[3],
           (unsigned int)mac[4], (unsigned int)mac[5]);
  status = ENC28J60_GetLinkState(&link_up);
  if (status != ENC28J60_OK)
  {
    LOG_ERROR("ENC", "Initial PHY read failed: %s",
              ENC28J60_StatusText(status));
    return false;
  }
  stable_link_up = link_up;
  candidate_link_up = link_up;
  candidate_link_count = 2U;
  LOG_INFO("ENC", "PHY Link: %s", link_up ? "UP" : "DOWN");
  now = enc28j60_port_get_tick_ms();
  next_link_poll_tick = now + PROJECT_ENC_LINK_POLL_MS;
  next_tx_tick = now + PROJECT_RAW_ETH_TX_INTERVAL_MS;
  next_stats_tick = now + PROJECT_ENC_STATS_INTERVAL_MS;
  tx_sequence = 0U;
  test_initialized = true;
  memset(mac, 0, sizeof(mac));
  return true;
}

void raw_eth_test_process(bool interrupt_wake)
{
  uint32_t now;

  if (!test_initialized)
  {
    return;
  }
  now = enc28j60_port_get_tick_ms();
  if (interrupt_wake)
  {
    enc28j60_status_t status = ENC28J60_ProcessInterruptFlags();
    if (status != ENC28J60_OK)
    {
      LOG_WARN("ENC", "IRQ processing failed: %s",
               ENC28J60_StatusText(status));
    }
  }
  process_received_frames();
  poll_link(now);

  if (stable_link_up && ((int32_t)(now - next_tx_tick) >= 0))
  {
    uint16_t length = build_test_frame(tx_sequence);
    enc28j60_status_t status = (length == 0U) ? ENC28J60_ERROR_TX :
                                ENC28J60_SendFrame(tx_frame, length);
    if (status == ENC28J60_OK)
    {
      test_statistics.test_tx_pass++;
      test_statistics.last_tx_sequence = tx_sequence;
      LOG_INFO("ETH", "Raw frame TX: PASS seq=%lu",
               (unsigned long)tx_sequence);
      tx_sequence++;
    }
    else
    {
      test_statistics.test_tx_fail++;
      LOG_WARN("ETH", "Raw frame TX failed: %s",
               ENC28J60_StatusText(status));
    }
    next_tx_tick = now + PROJECT_RAW_ETH_TX_INTERVAL_MS;
  }

  if ((int32_t)(now - next_stats_tick) >= 0)
  {
    const enc28j60_stats_t *stats = ENC28J60_GetStats();
    LOG_INFO("ENC", "ENC stats irq=%lu rx=%lu tx=%lu drop=%lu spi_err=%lu tx_err=%lu",
             (unsigned long)stats->irq_count,
             (unsigned long)stats->rx_frames,
             (unsigned long)stats->tx_frames,
             (unsigned long)(stats->rx_dropped + stats->rx_invalid),
             (unsigned long)stats->spi_errors,
             (unsigned long)stats->tx_errors);
    next_stats_tick = now + PROJECT_ENC_STATS_INTERVAL_MS;
  }
}

const raw_eth_test_stats_t *raw_eth_test_get_stats(void)
{
  return &test_statistics;
}

bool raw_eth_test_link_is_up(void)
{
  return stable_link_up;
}
