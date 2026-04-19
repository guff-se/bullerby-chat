#include "identity.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>

static const char *TAG = "identity";

#define NVS_NS          "bullerby"
#define NVS_KEY_DEV_ID  "device_id"
#define NVS_KEY_URL     "server_url"

static char s_device_id[64];
static char s_server_url[160];

static bullerby_identity_t s_identity = {
    .device_id = s_device_id,
    .server_url = s_server_url,
};

static void load_from_nvs_or_default(nvs_handle_t h, const char *key,
                                     const char *fallback, char *out, size_t out_sz)
{
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, out, &sz);
    if (err == ESP_OK) {
        return;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "nvs_get_str(%s): %s — using Kconfig default", key, esp_err_to_name(err));
    }
    if (fallback == NULL) {
        fallback = "";
    }
    strlcpy(out, fallback, out_sz);
}

esp_err_t identity_init(void)
{
#ifdef CONFIG_BULLERBY_DEVICE_ID
    const char *cfg_id     = CONFIG_BULLERBY_DEVICE_ID;
#else
    const char *cfg_id     = "";
#endif
#ifdef CONFIG_BULLERBY_SERVER_URL
    const char *cfg_url    = CONFIG_BULLERBY_SERVER_URL;
#else
    const char *cfg_url    = "";
#endif

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_OK) {
        load_from_nvs_or_default(h, NVS_KEY_DEV_ID, cfg_id,  s_device_id,  sizeof(s_device_id));
        load_from_nvs_or_default(h, NVS_KEY_URL,    cfg_url, s_server_url, sizeof(s_server_url));
        nvs_close(h);
    } else {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "nvs_open: %s — using Kconfig defaults", esp_err_to_name(err));
        }
        strlcpy(s_device_id,  cfg_id,  sizeof(s_device_id));
        strlcpy(s_server_url, cfg_url, sizeof(s_server_url));
    }

    size_t url_len = strlen(s_server_url);
    while (url_len > 0 && s_server_url[url_len - 1] == '/') {
        s_server_url[--url_len] = '\0';
    }

    if (strstr(s_server_url, "example.workers.dev") != NULL) {
        ESP_LOGW(TAG, "server_url is the Kconfig placeholder (*.example.workers.dev) — "
                      "DNS will fail. Set Bullerby Chat → Server base URL to your deployed "
                      "Worker (e.g. https://<name>.<subdomain>.workers.dev) or NVS bullerby/server_url.");
    }

#if defined(CONFIG_BULLERBY_DEVICE_ID_FROM_MAC) && CONFIG_BULLERBY_DEVICE_ID_FROM_MAC
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_device_id, sizeof(s_device_id), "esp-%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
#endif

    ESP_LOGI(TAG, "device_id=\"%s\" server_url=\"%s\"", s_device_id, s_server_url);
    return ESP_OK;
}

const bullerby_identity_t *identity_get(void)
{
    return &s_identity;
}

bool identity_is_configured(void)
{
    return s_device_id[0]  != '\0' &&
           s_server_url[0] != '\0';
}
