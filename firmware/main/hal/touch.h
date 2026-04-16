#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include <esp_err.h>
#include <lvgl.h>

// Initialize the CST816D touch controller and register it as an LVGL input device.
// Must be called after display_init() so the LVGL display exists.
esp_err_t touch_init(lv_display_t *disp);

#endif // HAL_TOUCH_H
