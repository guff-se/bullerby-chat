#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** One inbox item. Storage starts empty; populated from the server on new_message. */
typedef struct {
    uint8_t id;
    uint8_t from_family_id;
    const char *from_label;
    uint16_t duration_ms;
    bool unread;
} message_t;

size_t model_inbox_count(void);
const message_t *model_inbox_get(size_t index);
unsigned model_inbox_unread_count(void);
void model_inbox_mark_read(size_t index);
