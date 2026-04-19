#include "net.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "api_client.h"
#include "identity.h"
#include "model_families.h"
#include "wifi.h"
#include "wifi_portal.h"
#include "ws_client.h"

#if CONFIG_BULLERBY_ENABLE_NET
#include "ui_app.h"
#endif

static const char *TAG = "net";

#define NVS_NS_WIFI       "bullerby"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

/* Download URL + metadata is small; 512 B per event is plenty. */
#define INBOX_URL_MAX     384
#define INBOX_FAMILY_MAX  48
#define INBOX_QUEUE_DEPTH 4
#define DOWNLOAD_BUF_BYTES (128 * 1024)

typedef struct {
    char  download_url[INBOX_URL_MAX];
    char  from_family_id[INBOX_FAMILY_MAX];
    int   sample_rate_hz;
    float duration_s;
} inbox_item_t;

static QueueHandle_t  s_inbox;
static TaskHandle_t   s_worker;
static uint8_t       *s_dl_buf;
static volatile bool  s_online;
static net_play_cb_t  s_play_cb;

static void on_new_message(const ws_incoming_message_t *msg)
{
    if (!s_inbox) return;
    inbox_item_t item = {0};
    strlcpy(item.download_url, msg->download_url, sizeof(item.download_url));
    strlcpy(item.from_family_id, msg->from_family_id ? msg->from_family_id : "",
            sizeof(item.from_family_id));
    item.sample_rate_hz = msg->sample_rate_hz > 0 ? msg->sample_rate_hz : 16000;
    item.duration_s     = msg->duration_s;

    if (xQueueSend(s_inbox, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "inbox queue full — dropping message");
    }
}

static void net_worker(void *arg)
{
    (void)arg;

    const bullerby_identity_t *id = identity_get();

    /* 1) Wait for WiFi link. Retry fetch/register until they succeed once. */
    ESP_LOGI(TAG, "waiting for WiFi…");
    while (!wifi_wait_connected(20000)) {
        ESP_LOGW(TAG, "still no WiFi — will retry");
    }
    ESP_LOGI(TAG, "WiFi up. device_id=%s server=%s", id->device_id, id->server_url);

    /* 2) Register (best-effort — server just echoes family). */
    for (int i = 0; i < 5; i++) {
        if (api_register() == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /* 3) Fetch config → families + my family from server. */
    char cfg_body[3072];
    bool config_applied = false;
    for (int i = 0; i < 5; i++) {
        size_t cfg_len = 0;
        if (api_fetch_config(cfg_body, sizeof(cfg_body), &cfg_len) != ESP_OK || cfg_len == 0) {
            ESP_LOGW(TAG, "config fetch failed or empty (attempt %d/5)", i + 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        esp_err_t aerr = model_apply_server_config_json(cfg_body);
        if (aerr != ESP_OK) {
            ESP_LOGE(TAG, "config JSON apply failed: %s (attempt %d/5) — keeping pre-net family id",
                     esp_err_to_name(aerr), i + 1);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
#if CONFIG_BULLERBY_ENABLE_NET
        ui_app_rebuild_home_ring();
#endif
        config_applied = true;
        break;
    }
    if (!config_applied) {
        ESP_LOGE(TAG, "server config never applied after 5 tries — check Worker deploy vs "
                      "bullerby.json, serial logs above, and NVS family_id if UI shows wrong home");
    }

    /* 4) Open WebSocket. */
    ws_client_set_new_message_cb(on_new_message);
    esp_err_t werr = ws_client_start();
    if (werr != ESP_OK) {
        ESP_LOGE(TAG, "ws start failed: %s", esp_err_to_name(werr));
    }

    /* 5) Drain inbox forever. */
    for (;;) {
        /* Update online flag opportunistically. */
        s_online = ws_client_is_connected();

        inbox_item_t item;
        if (xQueueReceive(s_inbox, &item, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }
        if (!s_dl_buf) {
            ESP_LOGE(TAG, "download buffer not allocated — dropping");
            continue;
        }

        size_t got = 0;
        esp_err_t err = api_download_audio(item.download_url, s_dl_buf,
                                           DOWNLOAD_BUF_BYTES, &got);
        if (err != ESP_OK || got == 0) {
            ESP_LOGE(TAG, "download failed (%s, %u bytes)",
                     esp_err_to_name(err), (unsigned)got);
            continue;
        }
        if (s_play_cb) {
            s_play_cb(s_dl_buf, got, item.sample_rate_hz, item.from_family_id);
        } else {
            ESP_LOGW(TAG, "no playback cb — discarded %u bytes", (unsigned)got);
        }
    }
}

void net_set_playback_cb(net_play_cb_t cb)
{
    s_play_cb = cb;
}

bool net_is_online(void)
{
    return s_online;
}

/** NVS `bullerby` keys first; else Kconfig when SSID non-empty. */
static bool load_stored_wifi_credentials(char *ssid_out, size_t ssid_sz,
                                         char *pass_out, size_t pass_sz)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (err == ESP_OK) {
        size_t sz = ssid_sz;
        err = nvs_get_str(h, NVS_KEY_WIFI_SSID, ssid_out, &sz);
        if (err == ESP_OK && ssid_out[0] != '\0') {
            sz = pass_sz;
            esp_err_t perr = nvs_get_str(h, NVS_KEY_WIFI_PASS, pass_out, &sz);
            if (perr == ESP_ERR_NVS_NOT_FOUND) {
                pass_out[0] = '\0';
            } else if (perr != ESP_OK) {
                ESP_LOGW(TAG, "nvs_get_str(%s): %s — empty password",
                         NVS_KEY_WIFI_PASS, esp_err_to_name(perr));
                pass_out[0] = '\0';
            }
            nvs_close(h);
            ESP_LOGI(TAG, "WiFi credentials from NVS");
            return true;
        }
        nvs_close(h);
    }

#ifdef CONFIG_BULLERBY_WIFI_SSID
    const char *cfg_ssid = CONFIG_BULLERBY_WIFI_SSID;
#else
    const char *cfg_ssid = "";
#endif
#ifdef CONFIG_BULLERBY_WIFI_PASS
    const char *cfg_pass = CONFIG_BULLERBY_WIFI_PASS;
#else
    const char *cfg_pass = "";
#endif
    if (cfg_ssid && cfg_ssid[0] != '\0') {
        strlcpy(ssid_out, cfg_ssid, ssid_sz);
        strlcpy(pass_out, cfg_pass ? cfg_pass : "", pass_sz);
        ESP_LOGI(TAG, "WiFi credentials from Kconfig");
        return true;
    }
    return false;
}

esp_err_t net_start(void)
{
    if (!identity_is_configured()) {
        ESP_LOGE(TAG, "identity not configured — set CONFIG_BULLERBY_DEVICE_ID and SERVER_URL");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = wifi_init_driver();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_init_driver: %s", esp_err_to_name(err));
        return err;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    bool have_creds = load_stored_wifi_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    bool sta_ok = false;
    if (have_creds) {
        sta_ok = wifi_sta_connect(ssid, pass, 45000);
        if (!sta_ok) {
            ESP_LOGW(TAG, "stored WiFi did not connect — captive portal");
        }
    } else {
        ESP_LOGI(TAG, "no WiFi credentials — captive portal");
    }

    if (!sta_ok) {
        char ap_ssid[33];
        wifi_build_setup_ap_ssid(ap_ssid, sizeof(ap_ssid));
#if CONFIG_BULLERBY_ENABLE_NET
        ui_app_show_wifi_setup(ap_ssid);
#endif
        wifi_portal_run(ap_ssid);
        return ESP_OK;
    }

    s_dl_buf = heap_caps_malloc(DOWNLOAD_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_dl_buf) {
        ESP_LOGE(TAG, "failed to alloc %u-byte download buffer in PSRAM",
                 (unsigned)DOWNLOAD_BUF_BYTES);
        return ESP_ERR_NO_MEM;
    }

    s_inbox = xQueueCreate(INBOX_QUEUE_DEPTH, sizeof(inbox_item_t));
    if (!s_inbox) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(net_worker, "net_worker", 8192, NULL, 5, &s_worker);
    if (ok != pdPASS) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

esp_err_t net_send_pcm(const char *to_family_server_id,
                       const uint8_t *pcm, size_t pcm_len,
                       int sample_rate_hz, float duration_s)
{
    if (!s_online) {
        ESP_LOGW(TAG, "not online — skipping upload");
        return ESP_ERR_INVALID_STATE;
    }
    return api_post_message(to_family_server_id, pcm, pcm_len, sample_rate_hz, duration_s);
}
