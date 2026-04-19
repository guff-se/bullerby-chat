#include "model_families.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "model_families";

#define MODEL_NVS_NAMESPACE "bullerby"
#define MODEL_NVS_KEY_FAMILY "family_id"

#define MAX_RING_FAMILIES 8

/** Packs `family_t` with inline storage so pointers stay valid in one heap block. */
typedef struct {
    family_t f;
    char name_storage[48];
    char sid_storage[48];
} family_dyn_t;

uint8_t model_my_family_id = 0;

static const family_t k_families_static[] = {
    {1, "ANSUND",     false, "family-a"},
    {2, "BERGMAN",    false, "family-b"},
    {3, "AMANDA",     false, "family-c"},
    {4, "MATTIS",     false, "family-d"},
    {5, "ANNIKA",     false, "family-e"},
    {6, "NAVID",      false, "family-f"},
    {7, "LINDMARKER", false, "family-g"},
    {8, "TADAA",      false, "family-h"},
    {9, "ALLA",       true,  NULL},
};

#define STATIC_FAMILY_COUNT (sizeof(k_families_static) / sizeof(k_families_static[0]))

static family_dyn_t *s_dyn = NULL;
static size_t s_dyn_count = 0;

static void model_free_dynamic(void)
{
    if (s_dyn) {
        free(s_dyn);
        s_dyn = NULL;
        s_dyn_count = 0;
    }
}

static esp_err_t model_persist_my_family_id(uint8_t id)
{
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
    return err;
}

esp_err_t model_init(void)
{
    const uint8_t default_id = (uint8_t)CONFIG_BULLERBY_DEFAULT_FAMILY_ID;
    uint8_t id = default_id;

#if CONFIG_BULLERBY_ENABLE_NET
    /* No static dummy ring: `GET …/config` supplies families and `model_my_family_id`. */
    model_my_family_id = 0;
    ESP_LOGI(TAG, "family list pending server config (no NVS bootstrap when networking on)");
    return ESP_OK;
#endif

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
    ESP_LOGI(TAG, "This device is family id %u (static list until server config)", (unsigned)model_my_family_id);
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

size_t model_family_count(void)
{
#if CONFIG_BULLERBY_ENABLE_NET
    if (!s_dyn) {
        return 0;
    }
#endif
    if (s_dyn) {
        return s_dyn_count;
    }
    return STATIC_FAMILY_COUNT;
}

const family_t *model_family_by_index(size_t index)
{
#if CONFIG_BULLERBY_ENABLE_NET
    if (!s_dyn) {
        return NULL;
    }
#endif
    if (s_dyn) {
        if (index >= s_dyn_count) {
            return NULL;
        }
        return &s_dyn[index].f;
    }
    if (index >= STATIC_FAMILY_COUNT) {
        return NULL;
    }
    return &k_families_static[index];
}

const family_t *model_family_by_id(uint8_t id)
{
    for (size_t i = 0; i < model_family_count(); i++) {
        const family_t *f = model_family_by_index(i);
        if (f && f->id == id) {
            return f;
        }
    }
    return NULL;
}

const family_t *model_family_by_server_id(const char *server_id)
{
    if (server_id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < model_family_count(); i++) {
        const family_t *f = model_family_by_index(i);
        if (f && f->server_id != NULL && strcmp(f->server_id, server_id) == 0) {
            return f;
        }
    }
    return NULL;
}

esp_err_t model_apply_server_config_json(const char *json)
{
    if (!json || json[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "config JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *j_fam_id = cJSON_GetObjectItemCaseSensitive(root, "family_id");
    if (!cJSON_IsString(j_fam_id) || j_fam_id->valuestring == NULL || j_fam_id->valuestring[0] == '\0') {
        ESP_LOGW(TAG, "config missing family_id string");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    const char *my_server_family = j_fam_id->valuestring;

    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "families");
    if (!cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "config missing families array");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int n = cJSON_GetArraySize(arr);
    if (n <= 0 || n > MAX_RING_FAMILIES) {
        ESP_LOGW(TAG, "families count %d out of range (1..%d)", n, MAX_RING_FAMILIES);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t total = (size_t)n + 1u;
    family_dyn_t *block = (family_dyn_t *)calloc(total, sizeof(family_dyn_t));
    if (!block) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < n; i++) {
        const cJSON *item = cJSON_GetArrayItem(arr, i);
        const cJSON *jid = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *jname = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(jid) || !cJSON_IsString(jname)) {
            ESP_LOGW(TAG, "family[%d] missing id or name", i);
            free(block);
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
        strlcpy(block[i].sid_storage, jid->valuestring, sizeof(block[i].sid_storage));
        strlcpy(block[i].name_storage, jname->valuestring, sizeof(block[i].name_storage));
        block[i].f.id = (uint8_t)(i + 1);
        block[i].f.name = block[i].name_storage;
        block[i].f.server_id = block[i].sid_storage;
        block[i].f.is_broadcast = false;
    }

    family_dyn_t *alla = &block[n];
    strlcpy(alla->name_storage, "ALLA", sizeof(alla->name_storage));
    alla->f.id = (uint8_t)(n + 1);
    alla->f.name = alla->name_storage;
    alla->f.server_id = NULL;
    alla->f.is_broadcast = true;

    const family_t *mine = NULL;
    for (int i = 0; i < n; i++) {
        if (strcmp(block[i].sid_storage, my_server_family) == 0) {
            mine = &block[i].f;
            break;
        }
    }
    if (!mine) {
        ESP_LOGW(TAG, "family_id %s not in families[]", my_server_family);
        free(block);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    model_free_dynamic();
    s_dyn = block;
    s_dyn_count = total;

    model_my_family_id = mine->id;
    esp_err_t perr = model_persist_my_family_id(model_my_family_id);
    if (perr != ESP_OK) {
        ESP_LOGW(TAG, "could not persist family id: %s", esp_err_to_name(perr));
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "server families applied: count=%u my_server_family=%s my_local_id=%u",
             (unsigned)s_dyn_count, my_server_family, (unsigned)model_my_family_id);
    return ESP_OK;
}
