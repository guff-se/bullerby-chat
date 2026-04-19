#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** Build LVGL shell: home (family grid + carousel), recording stub, status bar. */
esp_err_t ui_app_init(lv_display_t *disp);

/** Full-screen prompt: join SoftAP `ap_ssid` to open the captive portal (call from main / net, not LVGL timer). */
void ui_app_show_wifi_setup(const char *ap_ssid);

/** Rebuild home ring circles after `model_apply_server_config_json()` (take LVGL lock). */
void ui_app_rebuild_home_ring(void);
