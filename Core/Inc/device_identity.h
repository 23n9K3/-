#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <stdbool.h>

#include "bsp_uid.h"

#define DEVICE_IDENTITY_PREFIX "IOT-L4R5-"
#define DEVICE_IDENTITY_PREFIX_LENGTH 9U
#define DEVICE_IDENTITY_DEVICE_ID_LENGTH \
  (DEVICE_IDENTITY_PREFIX_LENGTH + BSP_UID_HEX_LENGTH)

bool device_identity_init(void);
const char *device_identity_get_uid_hex(void);
const char *device_identity_get_device_id(void);

#endif /* DEVICE_IDENTITY_H */
