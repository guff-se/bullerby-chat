#ifndef UI_H
#define UI_H

#include <esp_err.h>
#include <lvgl.h>

// Create the test/skeleton UI on the given LVGL display.
// Shows device status and touch feedback on the round LCD.
esp_err_t ui_init(lv_display_t *disp);

// Update the status text shown on screen.
void ui_set_status(const char *text);

// Show recording state on screen (pulsing indicator).
void ui_show_recording(bool active);

// Show playback state on screen.
void ui_show_playback(bool active);

#endif // UI_H
