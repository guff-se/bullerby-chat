#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** Parsed `new_message` event from the server. Strings are valid only during the callback. */
typedef struct {
    const char *message_id;
    const char *from_family_id;   /**< server id, e.g. "family-b" */
    int         sample_rate_hz;   /**< defaults to 16000 when omitted */
    float       duration_s;
    const char *download_url;     /**< unsigned public URL; relay holds the blob for ~10 min then drops */
} ws_incoming_message_t;

typedef void (*ws_new_message_cb_t)(const ws_incoming_message_t *msg);

/** Set once before ws_client_start(). */
void ws_client_set_new_message_cb(ws_new_message_cb_t cb);

/**
 * Open /api/ws over wss:// (derived from identity_get()->server_url),
 * launch a heartbeat timer (30 s), and dispatch `new_message` events.
 *
 * Safe to call once; reconnects automatically on drop.
 */
esp_err_t ws_client_start(void);

/** True while the underlying esp_websocket_client reports OPEN. */
bool ws_client_is_connected(void);
