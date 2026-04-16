#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** One family or the broadcast row (dummy data until server exists). */
typedef struct {
    uint8_t id;
    const char *name;   /**< Display name */
    const char *abbr;   /**< Short label on grid buttons */
    bool is_broadcast;  /**< "ALL" / everyone */
} family_t;

/** Placeholder: which family this device belongs to (NVS/server later). */
extern const uint8_t model_my_family_id;

size_t model_family_count(void);
const family_t *model_family_by_index(size_t index);
const family_t *model_family_by_id(uint8_t id);
