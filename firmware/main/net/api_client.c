#include "api_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "identity.h"

static const char *TAG = "api_client";

#define AUTH_HEADER_MAX 160
#define URL_MAX         256
#define MULTIPART_BOUNDARY "----bullerby7f3c2e9a"

/* ── Helpers ────────────────────────────────────────────────────────── */

static void apply_auth_headers(esp_http_client_handle_t c)
{
    const bullerby_identity_t *id = identity_get();
    char auth[AUTH_HEADER_MAX];
    snprintf(auth, sizeof(auth), "Bearer %s", id->device_secret);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "X-Device-Id", id->device_id);
}

/** Collect response body bytes into a caller-owned buffer while esp_http_client runs. */
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool   truncated;
} body_sink_t;

static esp_err_t http_event_collect(esp_http_client_event_t *evt)
{
    body_sink_t *sink = (body_sink_t *)evt->user_data;
    if (!sink) return ESP_OK;
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    if (evt->data_len <= 0 || !evt->data) return ESP_OK;

    size_t room = (sink->len < sink->cap) ? (sink->cap - 1 - sink->len) : 0;
    if (room == 0) {
        sink->truncated = true;
        return ESP_OK;
    }
    size_t take = (size_t)evt->data_len < room ? (size_t)evt->data_len : room;
    memcpy(sink->buf + sink->len, evt->data, take);
    sink->len += take;
    sink->buf[sink->len] = '\0';
    if (take < (size_t)evt->data_len) sink->truncated = true;
    return ESP_OK;
}

static bool build_url(char *out, size_t out_sz, const char *path)
{
    const bullerby_identity_t *id = identity_get();
    if (!id->server_url[0]) {
        ESP_LOGE(TAG, "server_url is empty");
        return false;
    }
    int n = snprintf(out, out_sz, "%s%s", id->server_url, path);
    return n > 0 && (size_t)n < out_sz;
}

/* ── Register ───────────────────────────────────────────────────────── */

esp_err_t api_register(void)
{
    char url[URL_MAX];
    if (!build_url(url, sizeof(url), "/api/devices/register")) {
        return ESP_ERR_INVALID_ARG;
    }

    char body_buf[256] = {0};
    body_sink_t sink = {.buf = body_buf, .cap = sizeof(body_buf)};

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_collect,
        .user_data = &sink,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    apply_auth_headers(client);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, "{}", 2);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register transport failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "register status %d: %s", status, sink.buf);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "register ok: %s", sink.buf);
    return ESP_OK;
}

/* ── Fetch config ───────────────────────────────────────────────────── */

esp_err_t api_fetch_config(void)
{
    const bullerby_identity_t *id = identity_get();
    char url[URL_MAX];
    int n = snprintf(url, sizeof(url), "%s/api/devices/%s/config",
                     id->server_url, id->device_id);
    if (n <= 0 || (size_t)n >= sizeof(url)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 2 KB is plenty for the small config JSON we emit today. */
    static char body_buf[2048];
    body_buf[0] = '\0';
    body_sink_t sink = {.buf = body_buf, .cap = sizeof(body_buf)};

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_collect,
        .user_data = &sink,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    apply_auth_headers(client);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config transport failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "config status %d: %s", status, sink.buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(sink.buf);
    if (!root) {
        ESP_LOGW(TAG, "config body is not JSON (%u bytes)", (unsigned)sink.len);
        return ESP_OK;
    }
    const cJSON *fams = cJSON_GetObjectItemCaseSensitive(root, "families");
    int count = cJSON_IsArray(fams) ? cJSON_GetArraySize(fams) : 0;
    const cJSON *fam_id = cJSON_GetObjectItemCaseSensitive(root, "family_id");
    ESP_LOGI(TAG, "config ok: family=%s families=%d",
             cJSON_IsString(fam_id) ? fam_id->valuestring : "?", count);
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── Post message (multipart) ───────────────────────────────────────── */

static char *build_multipart(const char *to_family_server_id,
                             const uint8_t *pcm, size_t pcm_len,
                             int sample_rate_hz, float duration_s,
                             size_t *out_body_len)
{
    char meta[160];
    int meta_len;
    if (to_family_server_id && to_family_server_id[0] != '\0') {
        meta_len = snprintf(meta, sizeof(meta),
                            "{\"to_family_id\":\"%s\",\"duration_s\":%.2f,\"sample_rate_hz\":%d}",
                            to_family_server_id, duration_s, sample_rate_hz);
    } else {
        meta_len = snprintf(meta, sizeof(meta),
                            "{\"duration_s\":%.2f,\"sample_rate_hz\":%d}",
                            duration_s, sample_rate_hz);
    }
    if (meta_len <= 0 || (size_t)meta_len >= sizeof(meta)) {
        return NULL;
    }

    const char *fmt_hdr_meta =
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"metadata\"\r\n"
        "Content-Type: application/json\r\n\r\n";
    const char *fmt_hdr_audio_prefix =
        "\r\n--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"audio.pcm\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    const char *fmt_tail = "\r\n--" MULTIPART_BOUNDARY "--\r\n";

    size_t part_a = strlen(fmt_hdr_meta) + (size_t)meta_len;
    size_t part_b = strlen(fmt_hdr_audio_prefix) + pcm_len;
    size_t part_c = strlen(fmt_tail);
    size_t total  = part_a + part_b + part_c;

    uint8_t *body = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        body = malloc(total);
    }
    if (!body) return NULL;

    size_t off = 0;
    memcpy(body + off, fmt_hdr_meta, strlen(fmt_hdr_meta));
    off += strlen(fmt_hdr_meta);
    memcpy(body + off, meta, (size_t)meta_len);
    off += (size_t)meta_len;
    memcpy(body + off, fmt_hdr_audio_prefix, strlen(fmt_hdr_audio_prefix));
    off += strlen(fmt_hdr_audio_prefix);
    memcpy(body + off, pcm, pcm_len);
    off += pcm_len;
    memcpy(body + off, fmt_tail, strlen(fmt_tail));
    off += strlen(fmt_tail);

    *out_body_len = off;
    return (char *)body;
}

esp_err_t api_post_message(const char *to_family_server_id,
                           const uint8_t *pcm, size_t pcm_len,
                           int sample_rate_hz, float duration_s)
{
    if (!pcm || pcm_len == 0) return ESP_ERR_INVALID_ARG;
    if (pcm_len > 128 * 1024) {
        ESP_LOGE(TAG, "audio too large: %u bytes (cap 128 KiB)", (unsigned)pcm_len);
        return ESP_ERR_INVALID_SIZE;
    }

    char url[URL_MAX];
    if (!build_url(url, sizeof(url), "/api/messages")) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t body_len = 0;
    char *body = build_multipart(to_family_server_id, pcm, pcm_len,
                                 sample_rate_hz, duration_s, &body_len);
    if (!body) return ESP_ERR_NO_MEM;

    char body_buf[512] = {0};
    body_sink_t sink = {.buf = body_buf, .cap = sizeof(body_buf)};

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_collect,
        .user_data = &sink,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body);
        return ESP_FAIL;
    }

    apply_auth_headers(client);
    esp_http_client_set_header(client, "Content-Type",
                               "multipart/form-data; boundary=" MULTIPART_BOUNDARY);
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "post_message transport failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "post_message status %d: %s", status, sink.buf);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "post_message ok (%u bytes pcm → %s): %s",
             (unsigned)pcm_len,
             to_family_server_id && to_family_server_id[0] ? to_family_server_id : "ALL",
             sink.buf);
    return ESP_OK;
}

/* ── Download audio ─────────────────────────────────────────────────── */

esp_err_t api_download_audio(const char *signed_url,
                             uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    if (!signed_url || !buf || !out_len) return ESP_ERR_INVALID_ARG;
    *out_len = 0;

    esp_http_client_config_t cfg = {
        .url = signed_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "download status %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    if (content_len > 0 && (size_t)content_len > buf_cap) {
        ESP_LOGE(TAG, "download too large: %lld > %u", (long long)content_len, (unsigned)buf_cap);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t total = 0;
    while (total < buf_cap) {
        int r = esp_http_client_read(client, (char *)buf + total, (int)(buf_cap - total));
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) break; /* EOF */
        total += (size_t)r;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "download read failed");
        return err;
    }
    *out_len = total;
    ESP_LOGI(TAG, "downloaded %u bytes", (unsigned)total);
    return ESP_OK;
}
