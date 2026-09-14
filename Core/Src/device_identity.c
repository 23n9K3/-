#include "device_identity.h"

#include <stddef.h>

static char uid_hex[BSP_UID_HEX_LENGTH + 1U];
static char device_id[DEVICE_IDENTITY_DEVICE_ID_LENGTH + 1U];
static bool identity_is_initialized;

typedef char device_identity_prefix_length_check[
  ((sizeof(DEVICE_IDENTITY_PREFIX) - 1U) ==
   DEVICE_IDENTITY_PREFIX_LENGTH) ? 1 : -1];

bool device_identity_init(void)
{
  static const char prefix[] = DEVICE_IDENTITY_PREFIX;
  uint32_t index;

  identity_is_initialized = false;
  if (!bsp_uid_get_hex(uid_hex))
  {
    return false;
  }

  for (index = 0U; index < DEVICE_IDENTITY_PREFIX_LENGTH; index++)
  {
    device_id[index] = prefix[index];
  }
  for (index = 0U; index < BSP_UID_HEX_LENGTH; index++)
  {
    device_id[DEVICE_IDENTITY_PREFIX_LENGTH + index] = uid_hex[index];
  }
  device_id[DEVICE_IDENTITY_DEVICE_ID_LENGTH] = '\0';
  identity_is_initialized = true;
  return true;
}

const char *device_identity_get_uid_hex(void)
{
  return identity_is_initialized ? uid_hex : NULL;
}

const char *device_identity_get_device_id(void)
{
  return identity_is_initialized ? device_id : NULL;
}
