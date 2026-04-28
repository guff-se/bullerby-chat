#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** Build LVGL shell: v2 ring home (family circles + center message bubble) and the recording screen. */
esp_err_t ui_app_init(lv_display_t *disp);

/** Full-screen prompt: join SoftAP `ap_ssid` to open the captive portal (call from main / net, not LVGL timer). */
void ui_app_show_wifi_setup(const char *ap_ssid);

/** Rebuild home ring circles after `model_apply_server_config_json()` (take LVGL lock). */
void ui_app_rebuild_home_ring(void);

/**
 * Notify the UI that a remote message was just received and played.
 * Lights up the home bubble (count + pulse) so the user can tap to replay.
 * Thread-safe: takes the LVGL lock internally. Call from the net / audio task,
 * NOT from an LVGL timer callback.
 *
 * `from_label` is the display name (e.g. "BERGMAN") or NULL if unknown.
 * `from_icon`  is the UTF-8 emoji string (e.g. "😊") or NULL if unknown.
 */
void ui_app_on_new_message(const char *from_label, const char *from_icon);
