#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * Device-side credentials used by every authenticated Bullerby API call.
 *
 *   Authorization: Bearer <device_secret>
 *   X-Device-Id:   <device_id>
 *
 * Values are loaded once from NVS namespace "bullerby" (keys `device_id`,
 * `device_secret`, `server_url`); missing keys fall back to Kconfig.
 */
typedef struct {
    const char *device_id;    /**< "device-uuid-001" etc. */
    const char *device_secret;/**< shared bearer token */
    const char *server_url;   /**< no trailing slash, https://…/http://… */
} bullerby_identity_t;

/** Call once after nvs_flash_init(). */
esp_err_t identity_init(void);

/** Snapshot pointers are stable for the lifetime of the process. */
const bullerby_identity_t *identity_get(void);

/** True when device_id / server_url / device_secret are non-empty. */
bool identity_is_configured(void);
