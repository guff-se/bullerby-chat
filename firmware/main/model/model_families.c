#include "model_families.h"

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "model_families";

#define MODEL_NVS_NAMESPACE "bullerby"
#define MODEL_NVS_KEY_FAMILY "family_id"

uint8_t model_my_family_id = 8;

esp_err_t model_init(void)
{
    const uint8_t default_id = (uint8_t)CONFIG_BULLERBY_DEFAULT_FAMILY_ID;
    uint8_t id = default_id;

    nvs_handle_t h;
    esp_err_t err = nvs_open(MODEL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s — using default family id", esp_err_to_name(err));
        model_my_family_id = default_id;
        return ESP_OK;
    }

    err = nvs_get_u8(h, MODEL_NVS_KEY_FAMILY, &id);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        id = default_id;
        esp_err_t werr = nvs_set_u8(h, MODEL_NVS_KEY_FAMILY, id);
        if (werr == ESP_OK) {
            werr = nvs_commit(h);
        }
        if (werr != ESP_OK) {
            ESP_LOGW(TAG, "Could not persist default family_id: %s", esp_err_to_name(werr));
        }
    } else if (err == ESP_OK) {
        if (model_family_by_id(id) == NULL) {
            ESP_LOGW(TAG, "Stored family_id %u invalid, resetting to %u", (unsigned)id,
                     (unsigned)default_id);
            id = default_id;
            esp_err_t werr = nvs_set_u8(h, MODEL_NVS_KEY_FAMILY, id);
            if (werr == ESP_OK) {
                werr = nvs_commit(h);
            }
            if (werr != ESP_OK) {
                ESP_LOGW(TAG, "Could not persist corrected family_id: %s", esp_err_to_name(werr));
            }
        }
    } else {
        ESP_LOGW(TAG, "nvs_get_u8 family_id: %s", esp_err_to_name(err));
        id = default_id;
    }

    nvs_close(h);
    model_my_family_id = id;
    ESP_LOGI(TAG, "This device is family id %u", (unsigned)model_my_family_id);
    return ESP_OK;
}

esp_err_t model_set_my_family_id(uint8_t id)
{
    if (model_family_by_id(id) == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(MODEL_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, MODEL_NVS_KEY_FAMILY, id);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        model_my_family_id = id;
        ESP_LOGI(TAG, "Family id set to %u (saved to NVS)", (unsigned)id);
    }
    return err;
}

static const family_t k_families[] = {
    {1, "ANSUND",     false},
    {2, "BERGMAN",    false},
    {3, "AMANDA",     false},
    {4, "MATTIS",     false},
    {5, "ANNIKA",     false},
    {6, "NAVID",      false},
    {7, "LINDMARKER", false},
    {8, "TADAA",      false},
    {9, "ALLA",       true },
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
