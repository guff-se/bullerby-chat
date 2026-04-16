#include "model_messages.h"

static message_t s_inbox[] = {
    {1, 2, "Hansson", 12500, true},
    {2, 1, "Gustafsson", 8200, true},
    {3, 5, "Karlsson", 3000, false},
};

size_t model_inbox_count(void)
{
    return sizeof(s_inbox) / sizeof(s_inbox[0]);
}

const message_t *model_inbox_get(size_t index)
{
    if (index >= model_inbox_count()) {
        return NULL;
    }
    return &s_inbox[index];
}

unsigned model_inbox_unread_count(void)
{
    unsigned n = 0;
    for (size_t i = 0; i < model_inbox_count(); i++) {
        if (s_inbox[i].unread) {
            n++;
        }
    }
    return n;
}

void model_inbox_mark_read(size_t index)
{
    if (index < model_inbox_count()) {
        s_inbox[index].unread = false;
    }
}
