#ifndef ENC28J60_PORT_H
#define ENC28J60_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
  ENC28J60_PORT_OK = 0,
  ENC28J60_PORT_ERROR_PARAM,
  ENC28J60_PORT_ERROR_CONFIG,
  ENC28J60_PORT_ERROR_SPI
} enc28j60_port_status_t;

enc28j60_port_status_t enc28j60_port_init(void);
void enc28j60_port_cs_low(void);
void enc28j60_port_cs_high(void);
void enc28j60_port_reset_low(void);
void enc28j60_port_reset_high(void);
enc28j60_port_status_t enc28j60_port_transmit(const uint8_t *data,
                                               size_t length);
enc28j60_port_status_t enc28j60_port_receive(uint8_t *data, size_t length);
enc28j60_port_status_t enc28j60_port_transfer_byte(uint8_t tx, uint8_t *rx);
void enc28j60_port_delay_ms(uint32_t delay_ms);
uint32_t enc28j60_port_get_tick_ms(void);
uint32_t enc28j60_port_get_spi_clock_hz(void);
bool enc28j60_port_pins_are_valid(void);

#endif /* ENC28J60_PORT_H */
