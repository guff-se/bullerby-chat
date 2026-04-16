#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** Build LVGL shell: home (family grid + carousel), recording stub, status bar. */
esp_err_t ui_app_init(lv_display_t *disp);
