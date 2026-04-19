#include "ws_client.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "identity.h"

static const char *TAG = "ws_client";

#define HEARTBEAT_PERIOD_MS (30 * 1000)
#define WS_RX_BUF_SZ        (4 * 1024)

static esp_websocket_client_handle_t s_client;
static esp_timer_handle_t            s_heartbeat;
static ws_new_message_cb_t           s_new_msg_cb;
static volatile bool                 s_connected;

/** Scratch buffer for reassembling fragmented text frames. */
static char  s_rx_buf[WS_RX_BUF_SZ];
static size_t s_rx_len;

static void heartbeat_fire(void *arg)
{
    (void)arg;
    if (!s_client || !esp_websocket_client_is_connected(s_client)) {
        return;
    }
    const char *msg = "{\"type\":\"heartbeat\"}";
    int sent = esp_websocket_client_send_text(s_client, msg, strlen(msg), pdMS_TO_TICKS(1000));
    if (sent < 0) {
        ESP_LOGW(TAG, "heartbeat send failed (%d)", sent);
    }
}

static void handle_incoming_json(const char *text, size_t len)
{
    (void)len;
    cJSON *root = cJSON_Parse(text);
    if (!root) {
        ESP_LOGW(TAG, "incoming frame is not JSON: %.*s", (int)len, text);
        return;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "heartbeat_ack") == 0) {
        ESP_LOGD(TAG, "heartbeat_ack");
    } else if (strcmp(type->valuestring, "connected") == 0) {
        const cJSON *dev = cJSON_GetObjectItemCaseSensitive(root, "device_id");
        ESP_LOGI(TAG, "connected as %s",
                 cJSON_IsString(dev) ? dev->valuestring : "?");
    } else if (strcmp(type->valuestring, "new_message") == 0) {
        const cJSON *msg_id = cJSON_GetObjectItemCaseSensitive(root, "message_id");
        const cJSON *from   = cJSON_GetObjectItemCaseSensitive(root, "from_family_id");
        const cJSON *url    = cJSON_GetObjectItemCaseSensitive(root, "download_url");
        const cJSON *dur    = cJSON_GetObjectItemCaseSensitive(root, "duration_s");
        const cJSON *sr     = cJSON_GetObjectItemCaseSensitive(root, "sample_rate_hz");
        if (cJSON_IsString(msg_id) && cJSON_IsString(url)) {
            ws_incoming_message_t inc = {
                .message_id    = msg_id->valuestring,
                .from_family_id = cJSON_IsString(from) ? from->valuestring : "",
                .download_url  = url->valuestring,
                .sample_rate_hz = cJSON_IsNumber(sr) ? (int)sr->valuedouble : 16000,
                .duration_s    = cJSON_IsNumber(dur) ? (float)dur->valuedouble : 0.0f,
            };
            ESP_LOGI(TAG, "new_message id=%s from=%s sr=%d dur=%.2f",
                     inc.message_id, inc.from_family_id, inc.sample_rate_hz, inc.duration_s);
            if (s_new_msg_cb) {
                s_new_msg_cb(&inc);
            } else {
                ESP_LOGW(TAG, "no new_message handler registered — dropping");
            }
        } else {
            ESP_LOGW(TAG, "new_message missing fields");
        }
    } else {
        ESP_LOGD(TAG, "unhandled type: %s", type->valuestring);
    }
    cJSON_Delete(root);
}

static void on_ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = (esp_websocket_event_data_t *)data;
    switch ((esp_websocket_event_id_t)id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "websocket connected");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "websocket disconnected");
        break;
    case WEBSOCKET_EVENT_CLOSED:
        s_connected = false;
        ESP_LOGW(TAG, "websocket closed");
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "websocket error");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!ev) break;
        /* op_code 0x1 = text, 0x2 = binary, 0x0 = continuation, 0x8 = close, 0x9 ping, 0xA pong */
        if (ev->op_code == 0x08) {
            ESP_LOGI(TAG, "server sent close");
            break;
        }
        if (ev->op_code == 0x09 || ev->op_code == 0x0A) {
            /* ping / pong — the stack handles pings automatically */
            break;
        }

        /* Reassemble text frames that arrive fragmented. `payload_offset` is the
         * offset within the *current frame*; a fin-less chunk is continuation. */
        if (ev->payload_offset == 0) {
            s_rx_len = 0;
        }
        if (ev->data_len > 0 && ev->data_ptr) {
            size_t room = sizeof(s_rx_buf) - 1 - s_rx_len;
            size_t take = ((size_t)ev->data_len < room) ? (size_t)ev->data_len : room;
            memcpy(s_rx_buf + s_rx_len, ev->data_ptr, take);
            s_rx_len += take;
            s_rx_buf[s_rx_len] = '\0';
        }

        /* Full frame delivered when payload_offset + data_len == payload_len. */
        if ((ev->payload_offset + ev->data_len) >= ev->payload_len) {
            if (ev->op_code == 0x01 /* text */) {
                handle_incoming_json(s_rx_buf, s_rx_len);
            }
            s_rx_len = 0;
        }
        break;
    default:
        break;
    }
}

void ws_client_set_new_message_cb(ws_new_message_cb_t cb)
{
    s_new_msg_cb = cb;
}

static bool derive_ws_uri(char *out, size_t out_sz)
{
    const bullerby_identity_t *id = identity_get();
    if (!id->server_url[0]) return false;
    const char *base = id->server_url;
    const char *scheme = "wss://";
    const char *host_part = NULL;
    if (strncmp(base, "https://", 8) == 0) {
        host_part = base + 8;
        scheme = "wss://";
    } else if (strncmp(base, "http://", 7) == 0) {
        host_part = base + 7;
        scheme = "ws://";
    } else {
        host_part = base;
        scheme = "wss://";
    }
    int n = snprintf(out, out_sz, "%s%s/api/ws", scheme, host_part);
    return n > 0 && (size_t)n < out_sz;
}

esp_err_t ws_client_start(void)
{
    if (s_client) return ESP_OK;

    char uri[256];
    if (!derive_ws_uri(uri, sizeof(uri))) {
        return ESP_ERR_INVALID_ARG;
    }

    const bullerby_identity_t *id = identity_get();
    char auth_hdr[160];
    /* esp_websocket_client_config_t::headers expects raw HTTP header lines separated by \r\n. */
    int n = snprintf(auth_hdr, sizeof(auth_hdr), "X-Device-Id: %s\r\n", id->device_id);
    if (n <= 0 || (size_t)n >= sizeof(auth_hdr)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .headers = auth_hdr,
        .buffer_size = 4096,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "websocket init failed");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "websocket start: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "websocket starting → %s", uri);

    esp_timer_create_args_t tcfg = {
        .callback = heartbeat_fire,
        .name = "bullerby_ws_hb",
    };
    if (!s_heartbeat) {
        if (esp_timer_create(&tcfg, &s_heartbeat) == ESP_OK) {
            esp_timer_start_periodic(s_heartbeat, (uint64_t)HEARTBEAT_PERIOD_MS * 1000ULL);
        }
    }
    return ESP_OK;
}

bool ws_client_is_connected(void)
{
    return s_connected;
}
