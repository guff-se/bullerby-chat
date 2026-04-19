#include "model_messages.h"

static message_t *s_inbox = NULL;
static size_t s_inbox_count = 0;

size_t model_inbox_count(void)
{
    return s_inbox_count;
}

const message_t *model_inbox_get(size_t index)
{
    if (index >= s_inbox_count) {
        return NULL;
    }
    return &s_inbox[index];
}

unsigned model_inbox_unread_count(void)
{
    unsigned n = 0;
    for (size_t i = 0; i < s_inbox_count; i++) {
        if (s_inbox[i].unread) {
            n++;
        }
    }
    return n;
}

void model_inbox_mark_read(size_t index)
{
    if (index < s_inbox_count) {
        s_inbox[index].unread = false;
    }
}
