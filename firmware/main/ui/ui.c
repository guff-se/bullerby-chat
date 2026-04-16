#include "ui.h"
#include "board_config.h"
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <stdio.h>

static const char *TAG = "ui";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_center_indicator = NULL;
static lv_obj_t *s_touch_label = NULL;

// Called by LVGL when a touch event hits the background screen
static void screen_touch_cb(lv_event_t *e)
{
    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);

    char buf[32];
    snprintf(buf, sizeof(buf), "Touch: %d, %d", point.x, point.y);

    lvgl_port_lock(0);
    lv_label_set_text(s_touch_label, buf);
    lvgl_port_unlock();
}

esp_err_t ui_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Creating skeleton UI");

    lvgl_port_lock(0);

    s_screen = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // Title at top (with padding for round screen)
    s_title_label = lv_label_create(s_screen);
    lv_label_set_text(s_title_label, "Bullerby");
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xe0e0ff), 0);
    lv_obj_set_style_text_font(s_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_title_label, LV_ALIGN_TOP_MID, 0, 35);

    // Center circle indicator (shows recording/playback state)
    s_center_indicator = lv_obj_create(s_screen);
    lv_obj_set_size(s_center_indicator, 60, 60);
    lv_obj_set_style_radius(s_center_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_center_indicator, lv_color_hex(0x334455), 0);
    lv_obj_set_style_bg_opa(s_center_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_center_indicator, 2, 0);
    lv_obj_set_style_border_color(s_center_indicator, lv_color_hex(0x5588aa), 0);
    lv_obj_align(s_center_indicator, LV_ALIGN_CENTER, 0, -10);

    // Status label (below center)
    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "Initializing...");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x88aacc), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 45);

    // Touch feedback label (bottom, with padding for round screen)
    s_touch_label = lv_label_create(s_screen);
    lv_label_set_text(s_touch_label, "Tap screen to test");
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0x667788), 0);
    lv_obj_set_style_text_font(s_touch_label, &lv_font_montserrat_12, 0);
    lv_obj_align(s_touch_label, LV_ALIGN_BOTTOM_MID, 0, -35);

    // Register touch callback on the screen background
    lv_obj_add_event_cb(s_screen, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI created");
    return ESP_OK;
}

void ui_set_status(const char *text)
{
    if (!s_status_label) return;
    lvgl_port_lock(0);
    lv_label_set_text(s_status_label, text);
    lvgl_port_unlock();
}

void ui_show_recording(bool active)
{
    if (!s_center_indicator || !s_status_label) return;
    lvgl_port_lock(0);
    if (active) {
        lv_obj_set_style_bg_color(s_center_indicator, lv_color_hex(0xcc3333), 0);
        lv_obj_set_style_border_color(s_center_indicator, lv_color_hex(0xff5555), 0);
        lv_label_set_text(s_status_label, "Recording...");
    } else {
        lv_obj_set_style_bg_color(s_center_indicator, lv_color_hex(0x334455), 0);
        lv_obj_set_style_border_color(s_center_indicator, lv_color_hex(0x5588aa), 0);
        lv_label_set_text(s_status_label, "Ready");
    }
    lvgl_port_unlock();
}

void ui_show_playback(bool active)
{
    if (!s_center_indicator || !s_status_label) return;
    lvgl_port_lock(0);
    if (active) {
        lv_obj_set_style_bg_color(s_center_indicator, lv_color_hex(0x33aa33), 0);
        lv_obj_set_style_border_color(s_center_indicator, lv_color_hex(0x55ff55), 0);
        lv_label_set_text(s_status_label, "Playing...");
    } else {
        lv_obj_set_style_bg_color(s_center_indicator, lv_color_hex(0x334455), 0);
        lv_obj_set_style_border_color(s_center_indicator, lv_color_hex(0x5588aa), 0);
        lv_label_set_text(s_status_label, "Ready");
    }
    lvgl_port_unlock();
}
