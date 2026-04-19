#pragma once

#include <stdbool.h>

/**
 * Touch UI record/stop (see `audio_task` in main.c).
 * Safe from the LVGL task: only flips a flag; I2S runs on `audio_task`.
 */
void app_audio_set_ui_recording(bool active);
