#ifndef BSP_UID_H
#define BSP_UID_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_UID_WORD_COUNT 3U
#define BSP_UID_HEX_LENGTH 24U

/* The UID is a public hardware identifier, not a secret or a cryptographic key. */
void bsp_uid_get_words(uint32_t uid_words[BSP_UID_WORD_COUNT]);
bool bsp_uid_get_hex(char output[BSP_UID_HEX_LENGTH + 1U]);

#endif /* BSP_UID_H */
