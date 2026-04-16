#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <esp_err.h>
#include <lvgl.h>

// Initialize SPI bus, GC9A01 panel, backlight, and LVGL display.
// Returns the LVGL display object. All LVGL calls after this must be
// wrapped in lvgl_port_lock() / lvgl_port_unlock().
esp_err_t display_init(lv_display_t **out_disp);

// Set backlight brightness (0–100%).
void display_set_brightness(uint8_t brightness_pct);

#endif // HAL_DISPLAY_H
