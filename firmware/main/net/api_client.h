#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** `POST {server}/api/devices/register` — verifies auth + allowlist. */
esp_err_t api_register(void);

/** `GET {server}/api/devices/{id}/config` — logs families on success. */
esp_err_t api_fetch_config(void);

/**
 * `POST {server}/api/messages` (multipart/form-data).
 *
 *   to_family_server_id   NULL or "" for broadcast; else stable server id ("family-b").
 *   pcm / pcm_len         raw mono 16-bit little-endian PCM (≤ 128 KiB).
 *   sample_rate_hz        written into metadata so recipients clock correctly.
 *   duration_s            UI-only float, 0 if unknown.
 *
 * Body is built in heap (PSRAM if available); call context needs ~pcm_len of
 * free memory for the duration of the POST.
 */
esp_err_t api_post_message(const char *to_family_server_id,
                           const uint8_t *pcm, size_t pcm_len,
                           int sample_rate_hz, float duration_s);

/**
 * Download a signed audio URL (from a `new_message` event) into `buf`.
 * On success writes received byte count to `*out_len`.
 */
esp_err_t api_download_audio(const char *signed_url,
                             uint8_t *buf, size_t buf_cap, size_t *out_len);
