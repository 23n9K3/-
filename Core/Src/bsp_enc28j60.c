#include "bsp_enc28j60.h"

#include <stddef.h>
#include <string.h>

#include "bsp_uid.h"
#include "enc28j60_port.h"
#include "enc28j60_regs.h"

#define ENC28J60_MII_TIMEOUT_MS    20U
#define ENC28J60_TX_TIMEOUT_MS     100U

static uint8_t current_bank;
static uint16_t next_packet_pointer;
static uint8_t mac_address[6];
static enc28j60_stats_t statistics;
static volatile bool initialized;

static bool timeout_expired(uint32_t start, uint32_t timeout_ms)
{
  return ((uint32_t)(enc28j60_port_get_tick_ms() - start) >= timeout_ms);
}

static enc28j60_status_t spi_failure(void)
{
  statistics.spi_errors++;
  enc28j60_port_cs_high();
  return ENC28J60_ERROR_SPI;
}

static enc28j60_status_t transfer_command(uint8_t command,
                                           uint8_t value,
                                           uint8_t *received)
{
  uint8_t ignored;
  uint8_t result;

  enc28j60_port_cs_low();
  if (enc28j60_port_transfer_byte(command, &ignored) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  if (enc28j60_port_transfer_byte(value, &result) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  enc28j60_port_cs_high();
  if (received != NULL)
  {
    *received = result;
  }
  return ENC28J60_OK;
}

static enc28j60_status_t bit_field_set_raw(uint8_t address, uint8_t mask)
{
  return transfer_command((uint8_t)(ENC28J60_OP_BFS |
                                    (address & ENC28J60_ADDR_MASK)),
                          mask, NULL);
}

static enc28j60_status_t bit_field_clear_raw(uint8_t address, uint8_t mask)
{
  return transfer_command((uint8_t)(ENC28J60_OP_BFC |
                                    (address & ENC28J60_ADDR_MASK)),
                          mask, NULL);
}

static enc28j60_status_t select_bank(uint8_t address)
{
  uint8_t requested_bank;
  enc28j60_status_t status;

  if ((address & ENC28J60_ADDR_MASK) >= ENC28J60_EIE)
  {
    return ENC28J60_OK;
  }
  requested_bank = (uint8_t)((address & ENC28J60_BANK_MASK) >> 5U);
  if (requested_bank == current_bank)
  {
    return ENC28J60_OK;
  }
  status = bit_field_clear_raw(ENC28J60_ECON1,
                               ENC28J60_ECON1_BSEL0 |
                               ENC28J60_ECON1_BSEL1);
  if (status != ENC28J60_OK)
  {
    return status;
  }
  status = bit_field_set_raw(ENC28J60_ECON1, requested_bank);
  if (status == ENC28J60_OK)
  {
    current_bank = requested_bank;
  }
  return status;
}

static enc28j60_status_t read_register(uint8_t address, uint8_t *value)
{
  uint8_t ignored;
  uint8_t result;
  enc28j60_status_t status;

  if (value == NULL)
  {
    return ENC28J60_ERROR_PARAM;
  }
  status = select_bank(address);
  if (status != ENC28J60_OK)
  {
    return status;
  }
  enc28j60_port_cs_low();
  if (enc28j60_port_transfer_byte(
        (uint8_t)(ENC28J60_OP_RCR | (address & ENC28J60_ADDR_MASK)),
        &ignored) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  if ((address & ENC28J60_MAC_MII_FLAG) != 0U)
  {
    if (enc28j60_port_transfer_byte(0U, &ignored) != ENC28J60_PORT_OK)
    {
      return spi_failure();
    }
  }
  if (enc28j60_port_transfer_byte(0U, &result) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  enc28j60_port_cs_high();
  *value = result;
  return ENC28J60_OK;
}

static enc28j60_status_t write_register(uint8_t address, uint8_t value)
{
  enc28j60_status_t status = select_bank(address);
  if (status != ENC28J60_OK)
  {
    return status;
  }
  return transfer_command((uint8_t)(ENC28J60_OP_WCR |
                                    (address & ENC28J60_ADDR_MASK)),
                          value, NULL);
}

static enc28j60_status_t bit_field_set(uint8_t address, uint8_t mask)
{
  enc28j60_status_t status = select_bank(address);
  return (status == ENC28J60_OK) ? bit_field_set_raw(address, mask) : status;
}

static enc28j60_status_t bit_field_clear(uint8_t address, uint8_t mask)
{
  enc28j60_status_t status = select_bank(address);
  return (status == ENC28J60_OK) ? bit_field_clear_raw(address, mask) : status;
}

static enc28j60_status_t write_register16(uint8_t low_address,
                                          uint16_t value)
{
  enc28j60_status_t status = write_register(low_address,
                                             (uint8_t)value);
  if (status == ENC28J60_OK)
  {
    status = write_register((uint8_t)(low_address + 1U),
                            (uint8_t)(value >> 8U));
  }
  return status;
}

static enc28j60_status_t read_buffer(uint8_t *data, uint16_t length)
{
  uint8_t ignored;

  if ((data == NULL) || (length == 0U))
  {
    return ENC28J60_ERROR_PARAM;
  }
  enc28j60_port_cs_low();
  if (enc28j60_port_transfer_byte(ENC28J60_OP_RBM, &ignored) !=
      ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  if (enc28j60_port_receive(data, length) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  enc28j60_port_cs_high();
  return ENC28J60_OK;
}

static enc28j60_status_t write_buffer(const uint8_t *data, uint16_t length)
{
  uint8_t ignored;

  if ((data == NULL) || (length == 0U))
  {
    return ENC28J60_ERROR_PARAM;
  }
  enc28j60_port_cs_low();
  if (enc28j60_port_transfer_byte(ENC28J60_OP_WBM, &ignored) !=
      ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  if (enc28j60_port_transmit(data, length) != ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  enc28j60_port_cs_high();
  return ENC28J60_OK;
}

static enc28j60_status_t wait_register_clear(uint8_t address,
                                             uint8_t mask,
                                             uint32_t timeout_ms)
{
  uint8_t value;
  uint32_t start = enc28j60_port_get_tick_ms();
  enc28j60_status_t status;

  do
  {
    status = read_register(address, &value);
    if (status != ENC28J60_OK)
    {
      return status;
    }
    if ((value & mask) == 0U)
    {
      return ENC28J60_OK;
    }
  } while (!timeout_expired(start, timeout_ms));
  return ENC28J60_ERROR_TIMEOUT;
}

static enc28j60_status_t phy_write(uint8_t address, uint16_t value)
{
  enc28j60_status_t status;

  status = write_register(ENC28J60_MIREGADR, address);
  if (status == ENC28J60_OK)
  {
    status = write_register(ENC28J60_MIWRL, (uint8_t)value);
  }
  if (status == ENC28J60_OK)
  {
    status = write_register(ENC28J60_MIWRH, (uint8_t)(value >> 8U));
  }
  if (status == ENC28J60_OK)
  {
    status = wait_register_clear(ENC28J60_MISTAT,
                                 ENC28J60_MISTAT_BUSY,
                                 ENC28J60_MII_TIMEOUT_MS);
  }
  return status;
}

static enc28j60_status_t phy_read(uint8_t address, uint16_t *value)
{
  uint8_t low;
  uint8_t high;
  enc28j60_status_t status;

  if (value == NULL)
  {
    return ENC28J60_ERROR_PARAM;
  }
  status = write_register(ENC28J60_MIREGADR, address);
  if (status == ENC28J60_OK)
  {
    status = write_register(ENC28J60_MICMD, ENC28J60_MICMD_MIIRD);
  }
  if (status == ENC28J60_OK)
  {
    status = wait_register_clear(ENC28J60_MISTAT,
                                 ENC28J60_MISTAT_BUSY,
                                 ENC28J60_MII_TIMEOUT_MS);
  }
  (void)write_register(ENC28J60_MICMD, 0U);
  if (status == ENC28J60_OK)
  {
    status = read_register(ENC28J60_MIRDL, &low);
  }
  if (status == ENC28J60_OK)
  {
    status = read_register(ENC28J60_MIRDH, &high);
  }
  if (status == ENC28J60_OK)
  {
    *value = (uint16_t)((uint16_t)low | ((uint16_t)high << 8U));
  }
  return status;
}

static void generate_mac_address(void)
{
  uint32_t uid[BSP_UID_WORD_COUNT];
  bsp_uid_get_words(uid);
  mac_address[0] = 0x02U;
  mac_address[1] = (uint8_t)(uid[0] >> 24U);
  mac_address[2] = (uint8_t)((uid[0] >> 8U) ^ uid[1]);
  mac_address[3] = (uint8_t)((uid[1] >> 16U) ^ uid[2]);
  mac_address[4] = (uint8_t)((uid[1] >> 24U) ^ (uid[2] >> 8U));
  mac_address[5] = (uint8_t)(uid[0] ^ uid[1] ^ uid[2]);
  memset(uid, 0, sizeof(uid));
}

static enc28j60_status_t write_mac_address(void)
{
  static const uint8_t mac_registers[6] =
  {
    ENC28J60_MAADR1,
    ENC28J60_MAADR2,
    ENC28J60_MAADR3,
    ENC28J60_MAADR4,
    ENC28J60_MAADR5,
    ENC28J60_MAADR6
  };
  enc28j60_status_t status;
  uint8_t readback;
  uint32_t index;

  /* MAADR1 is Ethernet address byte 1 (bits 47:40), despite the
     non-linear physical register addresses in bank 3. */
  for (index = 0U; index < 6U; ++index)
  {
    status = write_register(mac_registers[index], mac_address[index]);
    if (status != ENC28J60_OK)
    {
      return status;
    }
  }

  /* A wrong MAC filter silently discards unicast DHCP offers. Verify the
     programmed address while the selected bank and SPI link are known-good. */
  for (index = 0U; index < 6U; ++index)
  {
    status = read_register(mac_registers[index], &readback);
    if (status != ENC28J60_OK)
    {
      return status;
    }
    if (readback != mac_address[index])
    {
      return ENC28J60_ERROR_CONFIG;
    }
  }

  return ENC28J60_OK;
}

static void reset_tx_logic(void)
{
  (void)bit_field_clear(ENC28J60_ECON1, ENC28J60_ECON1_TXRTS);
  (void)bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
  (void)bit_field_clear(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
  (void)bit_field_clear(ENC28J60_EIR,
                        ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF);
}

static void reset_rx_logic(void)
{
  (void)bit_field_clear(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
  (void)bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_RXRST);
  (void)bit_field_clear(ENC28J60_ECON1, ENC28J60_ECON1_RXRST);
  next_packet_pointer = ENC28J60_RX_START;
  (void)write_register16(ENC28J60_ERXRDPTL, ENC28J60_RX_END);
  (void)write_register16(ENC28J60_ERDPTL, ENC28J60_RX_START);
  (void)bit_field_clear(ENC28J60_EIR, ENC28J60_EIR_RXERIF);
  (void)bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
}

enc28j60_status_t ENC28J60_SoftReset(void)
{
  uint8_t ignored;

  enc28j60_port_cs_low();
  if (enc28j60_port_transfer_byte(ENC28J60_OP_SRC, &ignored) !=
      ENC28J60_PORT_OK)
  {
    return spi_failure();
  }
  enc28j60_port_cs_high();
  current_bank = 0U;
  /* Silicon errata: CLKRDY may remain set after SRC; wait at least 1 ms. */
  enc28j60_port_delay_ms(2U);
  return ENC28J60_OK;
}

enc28j60_status_t ENC28J60_ReadRevision(uint8_t *revision)
{
  return read_register(ENC28J60_EREVID, revision);
}

enc28j60_status_t ENC28J60_Init(void)
{
  uint8_t revision_a;
  uint8_t revision_b;
  uint16_t phir;
  enc28j60_status_t status;

  initialized = false;
  memset(&statistics, 0, sizeof(statistics));
  current_bank = 0U;
  next_packet_pointer = ENC28J60_RX_START;
  generate_mac_address();

  if (enc28j60_port_init() != ENC28J60_PORT_OK)
  {
    return ENC28J60_ERROR_CONFIG;
  }
  enc28j60_port_cs_high();
  enc28j60_port_reset_high();
  enc28j60_port_delay_ms(1U);
  enc28j60_port_reset_low();
  enc28j60_port_delay_ms(2U);
  enc28j60_port_reset_high();
  enc28j60_port_delay_ms(2U);

  status = ENC28J60_SoftReset();
  if (status != ENC28J60_OK)
  {
    statistics.reset_errors++;
    return status;
  }

  /*
   * ENC28J60 silicon errata: ESTAT.CLKRDY cannot be used reliably after
   * the SPI System Reset Command.  ENC28J60_SoftReset() already provides
   * the required fixed delay, so verify readiness by reading EREVID twice.
   */

  status = ENC28J60_ReadRevision(&revision_a);
  if (status == ENC28J60_OK) status = ENC28J60_ReadRevision(&revision_b);
  if (status != ENC28J60_OK)
  {
    return status;
  }
  if ((revision_a != revision_b) || (revision_a == 0U) ||
      (revision_a == 0xFFU))
  {
    statistics.revision = revision_a;
    return ENC28J60_ERROR_REVISION;
  }
  statistics.revision = revision_a;

  status = bit_field_set(ENC28J60_ECON2, ENC28J60_ECON2_AUTOINC);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ERXSTL, ENC28J60_RX_START);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ERXNDL, ENC28J60_RX_END);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ERXRDPTL, ENC28J60_RX_END);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ERDPTL, ENC28J60_RX_START);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_ERXFCON,
      ENC28J60_ERXFCON_UCEN | ENC28J60_ERXFCON_BCEN | ENC28J60_ERXFCON_CRCEN);

  /* Half duplex is selected in both MAC and PHY for broad switch compatibility. */
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MACON1,
                                                     ENC28J60_MACON1_MARXEN);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MACON3,
      ENC28J60_MACON3_PADCFG0 | ENC28J60_MACON3_TXCRCEN | ENC28J60_MACON3_FRMLNEN);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MACON4, 0x40U);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MABBIPG, 0x12U);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MAIPGL, 0x12U);
  if (status == ENC28J60_OK) status = write_register(ENC28J60_MAIPGH, 0x0CU);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_MAMXFLL,
                                                        ENC28J60_MAX_FRAME_LENGTH);
  if (status == ENC28J60_OK) status = phy_write(ENC28J60_PHCON1, 0U);
  if (status == ENC28J60_OK) status = phy_write(ENC28J60_PHCON2,
                                                ENC28J60_PHCON2_HDLDIS);
  if (status == ENC28J60_OK) status = phy_write(ENC28J60_PHIE,
      ENC28J60_PHIE_PLNKIE | ENC28J60_PHIE_PGEIE);
  if (status == ENC28J60_OK) status = write_mac_address();
  if (status != ENC28J60_OK)
  {
    return status;
  }

  (void)phy_read(ENC28J60_PHIR, &phir);
  (void)bit_field_clear(ENC28J60_EIR,
      ENC28J60_EIR_RXERIF | ENC28J60_EIR_TXERIF |
      ENC28J60_EIR_TXIF | ENC28J60_EIR_LINKIF);
  status = write_register(ENC28J60_EIE,
      ENC28J60_EIE_INTIE | ENC28J60_EIE_PKTIE |
      ENC28J60_EIE_LINKIE | ENC28J60_EIE_TXERIE |
      ENC28J60_EIE_RXERIE);
  if (status == ENC28J60_OK)
  {
    status = bit_field_set(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
  }
  if (status != ENC28J60_OK)
  {
    return status;
  }
  initialized = true;
  return ENC28J60_OK;
}

enc28j60_status_t ENC28J60_GetLinkState(bool *link_up)
{
  uint16_t phstat2;
  enc28j60_status_t status;

  if (link_up == NULL)
  {
    return ENC28J60_ERROR_PARAM;
  }
  if (!initialized)
  {
    return ENC28J60_ERROR_CONFIG;
  }
  status = phy_read(ENC28J60_PHSTAT2, &phstat2);
  if (status == ENC28J60_OK)
  {
    /* Read twice because PHY status bits can be latched. */
    status = phy_read(ENC28J60_PHSTAT2, &phstat2);
  }
  if (status == ENC28J60_OK)
  {
    *link_up = ((phstat2 & ENC28J60_PHSTAT2_LSTAT) != 0U);
  }
  return status;
}

static enc28j60_status_t release_rx_packet(uint16_t next_pointer)
{
  uint16_t read_pointer;
  enc28j60_status_t status;

  if (next_pointer > ENC28J60_RX_END)
  {
    next_pointer = ENC28J60_RX_START;
  }
  /* Errata-compatible: ERXRDPT must be odd and one byte behind next packet. */
  read_pointer = (next_pointer == ENC28J60_RX_START) ?
                 ENC28J60_RX_END : (uint16_t)(next_pointer - 1U);
  if ((read_pointer & 1U) == 0U)
  {
    read_pointer = (read_pointer == ENC28J60_RX_START) ?
                   ENC28J60_RX_END : (uint16_t)(read_pointer - 1U);
  }
  status = write_register16(ENC28J60_ERXRDPTL, read_pointer);
  if (status == ENC28J60_OK)
  {
    status = bit_field_set(ENC28J60_ECON2, ENC28J60_ECON2_PKTDEC);
  }
  next_packet_pointer = next_pointer;
  return status;
}

uint8_t ENC28J60_GetPendingPacketCount(void)
{
  uint8_t count = 0U;
  if (initialized)
  {
    (void)read_register(ENC28J60_EPKTCNT, &count);
  }
  return count;
}

enc28j60_status_t ENC28J60_ReceiveFrame(uint8_t *frame,
                                        uint16_t capacity,
                                        uint16_t *length)
{
  uint8_t header[ENC28J60_RX_HEADER_LENGTH];
  uint16_t next_pointer;
  uint16_t wire_length;
  uint16_t frame_length;
  uint16_t receive_status;
  enc28j60_status_t status;

  if ((frame == NULL) || (length == NULL) || (capacity == 0U))
  {
    return ENC28J60_ERROR_PARAM;
  }
  *length = 0U;
  if (!initialized)
  {
    return ENC28J60_ERROR_CONFIG;
  }
  if (ENC28J60_GetPendingPacketCount() == 0U)
  {
    return ENC28J60_OK;
  }
  status = write_register16(ENC28J60_ERDPTL, next_packet_pointer);
  if (status == ENC28J60_OK)
  {
    status = read_buffer(header, sizeof(header));
  }
  if (status != ENC28J60_OK)
  {
    return status;
  }
  next_pointer = (uint16_t)((uint16_t)header[0] |
                            ((uint16_t)header[1] << 8U));
  wire_length = (uint16_t)((uint16_t)header[2] |
                           ((uint16_t)header[3] << 8U));
  receive_status = (uint16_t)((uint16_t)header[4] |
                              ((uint16_t)header[5] << 8U));
  if ((next_pointer > ENC28J60_RX_END) ||
      ((next_pointer & 1U) != 0U) ||
      (wire_length < ENC28J60_ETH_CRC_LENGTH) ||
      (wire_length > (ENC28J60_MAX_FRAME_LENGTH + ENC28J60_ETH_CRC_LENGTH)) ||
      ((receive_status & ENC28J60_RX_STATUS_OK) == 0U))
  {
    statistics.rx_invalid++;
    (void)release_rx_packet(next_pointer);
    if ((next_pointer > ENC28J60_RX_END) ||
        ((next_pointer & 1U) != 0U))
    {
      reset_rx_logic();
    }
    return ENC28J60_ERROR_RX;
  }
  frame_length = (uint16_t)(wire_length - ENC28J60_ETH_CRC_LENGTH);
  if (frame_length > capacity)
  {
    statistics.rx_dropped++;
    (void)release_rx_packet(next_pointer);
    return ENC28J60_ERROR_FRAME_TOO_LARGE;
  }
  status = read_buffer(frame, frame_length);
  if (release_rx_packet(next_pointer) != ENC28J60_OK)
  {
    return ENC28J60_ERROR_SPI;
  }
  if (status != ENC28J60_OK)
  {
    return status;
  }
  statistics.rx_frames++;
  statistics.rx_bytes += frame_length;
  *length = frame_length;
  return ENC28J60_OK;
}

enc28j60_status_t ENC28J60_SendFrame(const uint8_t *frame,
                                     uint16_t length)
{
  uint8_t control = 0U;
  uint8_t econ1;
  uint8_t eir;
  uint8_t estat;
  bool link_up;
  uint32_t start;
  enc28j60_status_t status;

  if ((frame == NULL) || (length < 14U))
  {
    return ENC28J60_ERROR_PARAM;
  }
  if (length > ENC28J60_MAX_FRAME_LENGTH)
  {
    return ENC28J60_ERROR_FRAME_TOO_LARGE;
  }
  if (!initialized)
  {
    return ENC28J60_ERROR_CONFIG;
  }
  status = ENC28J60_GetLinkState(&link_up);
  if (status != ENC28J60_OK) return status;
  if (!link_up) return ENC28J60_ERROR_LINK_DOWN;

  reset_tx_logic();
  status = write_register16(ENC28J60_EWRPTL, ENC28J60_TX_START);
  if (status == ENC28J60_OK) status = write_buffer(&control, 1U);
  if (status == ENC28J60_OK) status = write_buffer(frame, length);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ETXSTL, ENC28J60_TX_START);
  if (status == ENC28J60_OK) status = write_register16(ENC28J60_ETXNDL,
      (uint16_t)(ENC28J60_TX_START + length));
  if (status == ENC28J60_OK) status = bit_field_clear(ENC28J60_EIR,
      ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF);
  if (status == ENC28J60_OK) status = bit_field_set(ENC28J60_ECON1,
                                                    ENC28J60_ECON1_TXRTS);
  if (status != ENC28J60_OK)
  {
    statistics.tx_errors++;
    return status;
  }

  start = enc28j60_port_get_tick_ms();
  do
  {
    status = read_register(ENC28J60_ECON1, &econ1);
    if (status != ENC28J60_OK) break;
    if ((econ1 & ENC28J60_ECON1_TXRTS) == 0U) break;
  } while (!timeout_expired(start, ENC28J60_TX_TIMEOUT_MS));
  if ((status != ENC28J60_OK) || ((econ1 & ENC28J60_ECON1_TXRTS) != 0U))
  {
    statistics.tx_errors++;
    reset_tx_logic();
    return (status == ENC28J60_OK) ? ENC28J60_ERROR_TIMEOUT : status;
  }

  status = write_register16(ENC28J60_ERDPTL,
      (uint16_t)(ENC28J60_TX_START + length + 1U));
  if (status == ENC28J60_OK)
  {
    status = read_buffer(statistics.last_tx_status,
                         ENC28J60_TX_STATUS_LENGTH);
  }
  if (status == ENC28J60_OK) status = read_register(ENC28J60_EIR, &eir);
  if (status == ENC28J60_OK) status = read_register(ENC28J60_ESTAT, &estat);
  if (status != ENC28J60_OK)
  {
    statistics.tx_errors++;
    return status;
  }
  statistics.last_eir = eir;
  statistics.last_estat = estat;
  if (((eir & ENC28J60_EIR_TXERIF) != 0U) ||
      ((estat & ENC28J60_ESTAT_TXABRT) != 0U))
  {
    statistics.tx_errors++;
    reset_tx_logic();
    return ENC28J60_ERROR_TX;
  }
  statistics.tx_frames++;
  statistics.tx_bytes += length;
  (void)bit_field_clear(ENC28J60_EIR, ENC28J60_EIR_TXIF);
  return ENC28J60_OK;
}

enc28j60_status_t ENC28J60_ProcessInterruptFlags(void)
{
  uint8_t eir;
  uint16_t phir;
  enc28j60_status_t status;

  if (!initialized)
  {
    return ENC28J60_ERROR_CONFIG;
  }
  status = read_register(ENC28J60_EIR, &eir);
  if (status != ENC28J60_OK) return status;
  statistics.last_eir = eir;
  if ((eir & ENC28J60_EIR_LINKIF) != 0U)
  {
    status = phy_read(ENC28J60_PHIR, &phir);
    if (status != ENC28J60_OK) return status;
    (void)phir;
  }
  if ((eir & ENC28J60_EIR_RXERIF) != 0U)
  {
    statistics.rx_error_interrupts++;
    statistics.rx_dropped++;
    reset_rx_logic();
  }
  if ((eir & ENC28J60_EIR_TXERIF) != 0U)
  {
    statistics.tx_error_interrupts++;
    statistics.tx_errors++;
    reset_tx_logic();
  }
  return ENC28J60_OK;
}

void ENC28J60_RecordIrqFromISR(void)
{
  statistics.irq_count++;
}

void ENC28J60_RecordLinkChange(bool link_up)
{
  if (link_up)
  {
    statistics.link_up_count++;
  }
  else
  {
    statistics.link_down_count++;
  }
}

const enc28j60_stats_t *ENC28J60_GetStats(void)
{
  return &statistics;
}

void ENC28J60_GetMacAddress(uint8_t mac[6])
{
  if (mac != NULL)
  {
    memcpy(mac, mac_address, sizeof(mac_address));
  }
}

bool ENC28J60_IsInitialized(void)
{
  return initialized;
}

const char *ENC28J60_StatusText(enc28j60_status_t status)
{
  switch (status)
  {
    case ENC28J60_OK: return "OK";
    case ENC28J60_ERROR_PARAM: return "PARAM";
    case ENC28J60_ERROR_CONFIG: return "CONFIG";
    case ENC28J60_ERROR_SPI: return "SPI";
    case ENC28J60_ERROR_TIMEOUT: return "TIMEOUT";
    case ENC28J60_ERROR_REVISION: return "REVISION";
    case ENC28J60_ERROR_LINK_DOWN: return "LINK_DOWN";
    case ENC28J60_ERROR_TX: return "TX";
    case ENC28J60_ERROR_RX: return "RX";
    case ENC28J60_ERROR_FRAME_TOO_LARGE: return "FRAME_TOO_LARGE";
    default: return "UNKNOWN";
  }
}
