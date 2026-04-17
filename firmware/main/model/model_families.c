#include "model_families.h"

/* Stub: pretend this device is family id 1 (replace with NVS). */
const uint8_t model_my_family_id = 1;

/* 8 regular families + 1 broadcast = 9 circles on the home ring. */
static const family_t k_families[] = {
    {1, "Gustafsson", "G",   false},
    {2, "Hansson",    "H",   false},
    {3, "Ivarsson",   "I",   false},
    {4, "Jansson",    "J",   false},
    {5, "Karlsson",   "K",   false},
    {6, "Lindberg",   "L",   false},
    {8, "Magnusson",  "M",   false},
    {9, "Nilsson",    "N",   false},
    {7, "All",        "ALL", true },
};

size_t model_family_count(void)
{
    return sizeof(k_families) / sizeof(k_families[0]);
}

const family_t *model_family_by_index(size_t index)
{
    if (index >= model_family_count()) {
        return NULL;
    }
    return &k_families[index];
}

const family_t *model_family_by_id(uint8_t id)
{
    for (size_t i = 0; i < model_family_count(); i++) {
        if (k_families[i].id == id) {
            return &k_families[i];
        }
    }
    return NULL;
}
