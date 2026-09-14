#ifndef BSP_DEBUG_UART_H
#define BSP_DEBUG_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool bsp_debug_uart_init(void);

bool bsp_debug_uart_write(const uint8_t *data,
                          size_t length,
                          uint32_t timeout_ms);

bool bsp_debug_uart_write_string(const char *text,
                                 uint32_t timeout_ms);

uint32_t bsp_debug_uart_get_transmitted_byte_count(void);
uint32_t bsp_debug_uart_get_error_count(void);

#endif /* BSP_DEBUG_UART_H */
