#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One family or the broadcast row (dummy data until server exists). */
typedef struct {
    uint8_t id;
    const char *name; /**< Display name (home ring uses raster emoji assets, see family_emoji_assets.h) */
    bool is_broadcast;  /**< ALLA — sänd till alla */
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
