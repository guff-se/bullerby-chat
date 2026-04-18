#include "net.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "api_client.h"
#include "identity.h"
#include "wifi.h"
#include "ws_client.h"

static const char *TAG = "net";

/* Signed URL + metadata is small; 512 B per event is plenty. */
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

    /* 3) Fetch config (logs it; future: feed into model). */
    for (int i = 0; i < 5; i++) {
        if (api_fetch_config() == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(2000));
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

esp_err_t net_start(void)
{
    if (!identity_is_configured()) {
        ESP_LOGE(TAG, "identity not configured — set CONFIG_BULLERBY_DEVICE_ID/SECRET and SERVER_URL");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_BULLERBY_WIFI_SSID
    const char *ssid = CONFIG_BULLERBY_WIFI_SSID;
#else
    const char *ssid = "";
#endif
#ifdef CONFIG_BULLERBY_WIFI_PASS
    const char *pass = CONFIG_BULLERBY_WIFI_PASS;
#else
    const char *pass = "";
#endif
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGE(TAG, "WiFi SSID empty — set CONFIG_BULLERBY_WIFI_SSID");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = wifi_init_sta(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_init_sta: %s", esp_err_to_name(err));
        return err;
    }

    s_dl_buf = heap_caps_malloc(DOWNLOAD_BUF_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_dl_buf) {
        ESP_LOGE(TAG, "failed to alloc %u-byte download buffer in PSRAM",
                 (unsigned)DOWNLOAD_BUF_BYTES);
        return ESP_ERR_NO_MEM;
    }

    s_inbox = xQueueCreate(INBOX_QUEUE_DEPTH, sizeof(inbox_item_t));
    if (!s_inbox) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(net_worker, "net_worker", 6144, NULL, 5, &s_worker);
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
