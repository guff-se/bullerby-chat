#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One family or the broadcast row. */
typedef struct {
    uint8_t id;
    const char *name;
    /** UTF-8 emoji string (e.g. "😀"); maps to a pre-rasterized asset via icon_to_asset_index(). */
    const char *icon;
    bool is_broadcast;
    /** Stable id used by the server API (device id or NULL for broadcast). */
    const char *server_id;
} family_t;

/** Which family this device belongs to; set by model_init() from NVS (flash). */
extern uint8_t model_my_family_id;

/** Load persisted family id from NVS; call once after nvs_flash_init(). */
esp_err_t model_init(void);

/** Persist this device's family id (must match a known family). For provisioning / UI later. */
esp_err_t model_set_my_family_id(uint8_t id);

size_t model_family_count(void);
const family_t *model_family_by_index(size_t index);
const family_t *model_family_by_id(uint8_t id);
/** Look up a family by its server id (e.g. `family-a`). NULL if unknown. */
const family_t *model_family_by_server_id(const char *server_id);

/**
 * Replace the in-memory family list from `GET …/config` JSON (fields `family_id`,
 * `families`[] with `id`, `name`, `icon`). Appends ALLA broadcast. Persists
 * `model_my_family_id` to NVS. On error, leaves the previous list unchanged.
 */
esp_err_t model_apply_server_config_json(const char *json);
