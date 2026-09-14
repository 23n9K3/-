#ifndef BSP_ENC28J60_H
#define BSP_ENC28J60_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  ENC28J60_OK = 0,
  ENC28J60_ERROR_PARAM,
  ENC28J60_ERROR_CONFIG,
  ENC28J60_ERROR_SPI,
  ENC28J60_ERROR_TIMEOUT,
  ENC28J60_ERROR_REVISION,
  ENC28J60_ERROR_LINK_DOWN,
  ENC28J60_ERROR_TX,
  ENC28J60_ERROR_RX,
  ENC28J60_ERROR_FRAME_TOO_LARGE
} enc28j60_status_t;

typedef struct
{
  uint32_t spi_errors;
  uint32_t reset_errors;
  uint32_t rx_frames;
  uint32_t rx_bytes;
  uint32_t rx_dropped;
  uint32_t rx_invalid;
  uint32_t tx_frames;
  uint32_t tx_bytes;
  uint32_t tx_errors;
  uint32_t link_up_count;
  uint32_t link_down_count;
  uint32_t irq_count;
  uint32_t rx_error_interrupts;
  uint32_t tx_error_interrupts;
  uint8_t revision;
  uint8_t last_eir;
  uint8_t last_estat;
  uint8_t last_tx_status[7];
} enc28j60_stats_t;

enc28j60_status_t ENC28J60_Init(void);
enc28j60_status_t ENC28J60_SoftReset(void);
enc28j60_status_t ENC28J60_ReadRevision(uint8_t *revision);
enc28j60_status_t ENC28J60_GetLinkState(bool *link_up);
enc28j60_status_t ENC28J60_SendFrame(const uint8_t *frame,
                                     uint16_t length);
enc28j60_status_t ENC28J60_ReceiveFrame(uint8_t *frame,
                                        uint16_t capacity,
                                        uint16_t *length);
uint8_t ENC28J60_GetPendingPacketCount(void);
const enc28j60_stats_t *ENC28J60_GetStats(void);
void ENC28J60_GetMacAddress(uint8_t mac[6]);
enc28j60_status_t ENC28J60_ProcessInterruptFlags(void);
void ENC28J60_RecordIrqFromISR(void);
void ENC28J60_RecordLinkChange(bool link_up);
bool ENC28J60_IsInitialized(void);
const char *ENC28J60_StatusText(enc28j60_status_t status);

#endif /* BSP_ENC28J60_H */
