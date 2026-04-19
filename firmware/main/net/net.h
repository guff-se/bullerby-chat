#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * Called by the net worker when an incoming message has been downloaded.
 * `pcm` lives in an internal PSRAM buffer — the callee must consume (e.g.
 * hand to a playback task) before returning.
 */
typedef void (*net_play_cb_t)(const uint8_t *pcm, size_t pcm_len,
                              int sample_rate_hz, const char *from_family_id);

/** Register the playback sink. Pass NULL to unregister. */
void net_set_playback_cb(net_play_cb_t cb);

/**
 * Bring up WiFi, register with the Worker, fetch config, open the WebSocket,
 * and spawn the worker task that drains inbound messages.
 *
 * Returns ESP_OK immediately after launching the worker; state is observable
 * via net_is_online(). Safe to call once.
 */
esp_err_t net_start(void);

/**
 * Synchronous outgoing send. Not safe to call from the LVGL task — call from
 * a dedicated audio / record task.
 *
 *   to_family_server_id  NULL or "" → broadcast ("ALLA").
 */
esp_err_t net_send_pcm(const char *to_family_server_id,
                       const uint8_t *pcm, size_t pcm_len,
                       int sample_rate_hz, float duration_s);

/** True once the WebSocket is open (ready to receive `new_message`). */
bool net_is_online(void);

/**
 * True when the home intercom UI may run: server `GET …/config` was applied and the
 * WebSocket is connected. When false, the UI shows disconnected (no dummy ring).
 */
bool net_intercom_ui_ready(void);
