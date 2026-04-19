#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * Device-side identity used by every Bullerby API call.
 *
 *   X-Device-Id: <device_id>
 *
 * Values are loaded once from NVS namespace "bullerby" (key `server_url`; optional
 * `device_id` is only used when MAC derivation is off in menuconfig). Missing keys
 * fall back to Kconfig. When **Derive device id from chip WiFi MAC** is enabled
 * (default for networked builds), `device_id` is always esp-<12 hex> from the
 * factory WiFi MAC — stable across flashes; allowlist that string on the server.
 */
typedef struct {
    const char *device_id;    /**< esp-<mac> (default) or fixed Kconfig/NVS id */
    const char *server_url;   /**< no trailing slash, https://…/http://… */
} bullerby_identity_t;

/** Call once after nvs_flash_init(). */
esp_err_t identity_init(void);

/** Snapshot pointers are stable for the lifetime of the process. */
const bullerby_identity_t *identity_get(void);

/** True when device_id / server_url are non-empty. */
bool identity_is_configured(void);
