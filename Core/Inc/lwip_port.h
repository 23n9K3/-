#ifndef APP_LWIP_PORT_H
#define APP_LWIP_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool lwip_port_start(void);
void lwip_port_process(void);
bool lwip_port_request_link(bool link_up);
bool lwip_port_input_frame(const uint8_t *frame, uint16_t length);
bool lwip_port_is_ready(void);
void lwip_port_log_stats(void);

#endif /* APP_LWIP_PORT_H */
