#ifndef NETIF_TASK_H
#define NETIF_TASK_H

#include <stdbool.h>
#include <stdint.h>

bool netif_task_initialize(void);
void netif_task_process(uint32_t notification_count);
bool netif_task_link_is_up(void);

#endif /* NETIF_TASK_H */
