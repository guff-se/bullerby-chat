/*
 * Bullerby Chat — Wacky Kids UI
 *
 * Home: family circles on ring perimeter + center message bubble.
 * Family screen: full-color flood, big record/stop button, back.
 *
 * Design intent: loud neon colors, bouncy animations, joyful chaos for kids.
 */

#include "ui_app.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "hal/hal.h"
#include "model_families.h"
#include "model_messages.h"

static const char *TAG = "ui_app";

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ── Display geometry ───────────────────────────────────────────── */
#define DISC_CX            (LCD_H_RES / 2)
#define DISC_CY            (LCD_V_RES / 2)
#define RING_RADIUS        88           /* family circles: px from disc center */
#define RING_CIRCLE_DIA    44           /* each family circle diameter */
#define MSG_BUBBLE_DIA     70           /* center message bubble */
#define REC_BTN_DIA        84           /* big record / stop button */
#define BACK_BTN_DIA       38           /* back button */
#define IDLE_DOT_DIA       26           /* white dot inside idle rec button */
#define MAX_FAMILY_CIRCLES 16

/* ── Timing ─────────────────────────────────────────────────────── */
#define TICK_MS            200
#define SENT_TICKS         (5000   / TICK_MS)
#define IDLE_DELETE_TICKS  (120000 / TICK_MS)
#define RECORD_MAX_TICKS   (30000  / TICK_MS)

/* ── Colour palette ─────────────────────────────────────────────── */
#define COL_BG          0x0d0921    /* deep space purple-black */
#define COL_MSG_READY   0xffdd00    /* neon gold  — new messages */
#define COL_MSG_PLAYED  0xff7700    /* orange     — replay */
#define COL_REC_BTN     0xff1144    /* hot red    — idle record */
#define COL_STOP_BTN    0xff5500    /* lava orange — recording → stop */
#define COL_BACK        0x18102e    /* near-black — back button */
#define COL_SENT_TEXT   0x22ff88    /* neon mint  — "WHOOSH!" */
#define COL_ALL_FILL    0xffffff
#define COL_ALL_BORDER  0xff00ff    /* magenta rim on ALL circle */
#define COL_ALL_TEXT    0x110033

/* Per-family neon palette, cycled by (id-1) mod 8 */
static const uint32_t k_neon[] = {
    0xff1493,   /* hot pink      */
    0x39ff14,   /* electric lime */
    0xff6600,   /* neon orange   */
    0x0066ff,   /* electric blue */
    0xcc00ff,   /* vivid violet  */
    0x00e5ff,   /* neon cyan     */
    0xff0033,   /* vivid red     */
    0xffcc00,   /* gold          */
};

static uint32_t fam_color(const family_t *f)
{
    if (!f) return 0x666666;
    if (f->is_broadcast) return COL_ALL_FILL;
    size_t n = sizeof(k_neon) / sizeof(k_neon[0]);
    return k_neon[(f->id - 1) % n];
}

/* ── App state ──────────────────────────────────────────────────── */
typedef enum { MSG_NONE, MSG_AVAILABLE, MSG_PLAYED } msg_state_t;

static lv_obj_t *s_scr_home;
static lv_obj_t *s_scr_record;

/* Home ring */
static lv_obj_t *s_ring_circles[MAX_FAMILY_CIRCLES];
static lv_obj_t *s_ring_labels[MAX_FAMILY_CIRCLES];
static size_t    s_ring_n;

/* Center message bubble */
static lv_obj_t *s_msg_bubble;
static lv_obj_t *s_msg_icon_lbl;
static lv_obj_t *s_msg_count_lbl;

/* Record screen */
static lv_obj_t *s_rec_btn;
static lv_obj_t *s_rec_idle_dot;   /* white circle: shown when not recording */
static lv_obj_t *s_rec_stop_lbl;   /* STOP symbol: shown while recording */
static lv_obj_t *s_sent_lbl;       /* "WHOOSH!" notification */

static size_t      s_open_family_idx = 0;
static bool        s_recording       = false;
static uint32_t    s_rec_ticks       = 0;
static uint32_t    s_sent_countdown  = 0;
static msg_state_t s_msg_state       = MSG_NONE;
static uint32_t    s_new_msg_count   = 0;
static bool        s_has_played      = false;
static uint32_t    s_idle_ticks      = 0;

/* ── Helpers ────────────────────────────────────────────────────── */

static void no_scroll(lv_obj_t *o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void style_base_screen(lv_obj_t *scr)
{
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scr, 0, 0);
    no_scroll(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_EVENT_BUBBLE);
}

/* Clockwise from 12-o'clock. Returns top-left corner for RING_CIRCLE_DIA object. */
static void ring_pos(int idx, int n, lv_coord_t *px, lv_coord_t *py)
{
    float theta = 2.0f * (float)M_PI * idx / (float)n;
    int cx = DISC_CX + (int)(RING_RADIUS * sinf(theta) + 0.5f);
    int cy = DISC_CY - (int)(RING_RADIUS * cosf(theta) + 0.5f);
    *px = (lv_coord_t)(cx - RING_CIRCLE_DIA / 2);
    *py = (lv_coord_t)(cy - RING_CIRCLE_DIA / 2);
}

/* ── Pulse animation on message bubble ──────────────────────────── */

static void bubble_glow_exec(void *obj, int32_t v)
{
    lv_obj_set_style_shadow_width((lv_obj_t *)obj, (int32_t)v, 0);
}

static void start_bubble_pulse(lv_obj_t *bubble)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bubble);
    lv_anim_set_values(&a, 8, 28);
    lv_anim_set_duration(&a, 550);
    lv_anim_set_playback_duration(&a, 550);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, bubble_glow_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* ── Message state ──────────────────────────────────────────────── */

static void update_msg_state(void)
{
    if (s_new_msg_count > 0) {
        s_msg_state = MSG_AVAILABLE;
    } else if (s_has_played) {
        s_msg_state = MSG_PLAYED;
    } else {
        s_msg_state = MSG_NONE;
    }
}

static void refresh_msg_bubble(void)
{
    if (!s_msg_bubble) return;

    if (s_msg_state == MSG_NONE) {
        lv_anim_del(s_msg_bubble, bubble_glow_exec);
        lv_obj_add_flag(s_msg_bubble, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_msg_bubble, LV_OBJ_FLAG_HIDDEN);

    if (s_msg_state == MSG_AVAILABLE) {
        lv_obj_set_style_bg_color(s_msg_bubble, lv_color_hex(COL_MSG_READY), 0);
        lv_obj_set_style_shadow_color(s_msg_bubble, lv_color_hex(COL_MSG_READY), 0);
        lv_label_set_text(s_msg_icon_lbl, LV_SYMBOL_PLAY);
        if (s_msg_count_lbl) {
            char cnt[8];
            snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)s_new_msg_count);
            lv_label_set_text(s_msg_count_lbl, cnt);
            lv_obj_clear_flag(s_msg_count_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    } else { /* MSG_PLAYED */
        lv_obj_set_style_bg_color(s_msg_bubble, lv_color_hex(COL_MSG_PLAYED), 0);
        lv_obj_set_style_shadow_color(s_msg_bubble, lv_color_hex(COL_MSG_PLAYED), 0);
        lv_label_set_text(s_msg_icon_lbl, LV_SYMBOL_LOOP);
        if (s_msg_count_lbl) {
            lv_obj_add_flag(s_msg_count_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ── Stop-and-send logic (called from button or auto-timer) ─────── */

static void do_send_stop(void)
{
    s_recording  = false;
    s_rec_ticks  = 0;
    if (s_rec_btn)      lv_obj_add_flag(s_rec_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_sent_lbl)     lv_obj_clear_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
    s_sent_countdown = SENT_TICKS;
    const family_t *f = model_family_by_index(s_open_family_idx);
    ESP_LOGI(TAG, "Message sent to: %s", f ? f->name : "?");
}

/* ── Event handlers ─────────────────────────────────────────────── */

static void on_msg_bubble_tapped(lv_event_t *e)
{
    (void)e;
    s_idle_ticks = 0;

    if (s_msg_state == MSG_AVAILABLE) {
        if (s_has_played) {
            ESP_LOGI(TAG, "Playing next (discarding previous played)");
        } else {
            ESP_LOGI(TAG, "Playing message #%lu", (unsigned long)(s_new_msg_count));
        }
        if (s_new_msg_count > 0) s_new_msg_count--;
        s_has_played = true;
        model_inbox_mark_read(0);
    } else if (s_msg_state == MSG_PLAYED) {
        ESP_LOGI(TAG, "Replaying last message");
    }

    update_msg_state();
    refresh_msg_bubble();
}

static void on_ring_circle_tapped(lv_event_t *e)
{
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const family_t *f = model_family_by_index(idx);
    if (!f || !s_scr_record) return;

    s_open_family_idx = idx;
    s_idle_ticks      = 0;
    s_recording       = false;
    s_rec_ticks       = 0;
    s_sent_countdown  = 0;

    /* Flood the record screen with the family's colour. */
    uint32_t c = f->is_broadcast ? 0xdd00cc : fam_color(f);
    lv_obj_set_style_bg_color(s_scr_record, lv_color_hex(c), 0);

    /* Reset recording screen widgets. */
    if (s_rec_btn) {
        lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(COL_REC_BTN), 0);
        lv_obj_set_style_shadow_color(s_rec_btn, lv_color_hex(COL_REC_BTN), 0);
        lv_obj_clear_flag(s_rec_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_rec_idle_dot) lv_obj_clear_flag(s_rec_idle_dot, LV_OBJ_FLAG_HIDDEN);
    if (s_rec_stop_lbl) lv_obj_add_flag(s_rec_stop_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_sent_lbl)     lv_obj_add_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Opened family: %s", f->name);
    lv_screen_load_anim(s_scr_record, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, false);
}

static void on_rec_btn_tapped(lv_event_t *e)
{
    (void)e;
    s_idle_ticks = 0;

    if (!s_recording) {
        /* Start recording */
        s_recording = true;
        s_rec_ticks = 0;
        if (s_rec_idle_dot) lv_obj_add_flag(s_rec_idle_dot, LV_OBJ_FLAG_HIDDEN);
        if (s_rec_stop_lbl) lv_obj_clear_flag(s_rec_stop_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s_rec_btn) {
            lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(COL_STOP_BTN), 0);
            lv_obj_set_style_shadow_color(s_rec_btn, lv_color_hex(COL_STOP_BTN), 0);
        }
        const family_t *f = model_family_by_index(s_open_family_idx);
        ESP_LOGI(TAG, "Recording to: %s", f ? f->name : "?");
    } else {
        do_send_stop();
    }
}

static void on_back_tapped(lv_event_t *e)
{
    (void)e;
    s_recording      = false;
    s_rec_ticks      = 0;
    s_sent_countdown = 0;
    s_idle_ticks     = 0;
    if (s_sent_lbl) lv_obj_add_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_scr_home) lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, false);
}

/* ── Tick ───────────────────────────────────────────────────────── */

static void app_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!lvgl_port_lock(0)) return;

    bool skip_idle = false;

    /* Recording auto-stop */
    if (s_recording) {
        s_rec_ticks++;
        if (s_rec_ticks >= RECORD_MAX_TICKS) {
            do_send_stop();
            skip_idle = true;
        }
    }

    /* "WHOOSH!" countdown → return to home */
    if (!skip_idle && s_sent_countdown > 0) {
        s_sent_countdown--;
        if (s_sent_countdown == 0) {
            if (s_sent_lbl) lv_obj_add_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_scr_home) {
                lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
            }
        }
        skip_idle = true;
    }

    /* Idle auto-delete of played message (2 min) */
    if (!skip_idle) {
        if (s_has_played && s_new_msg_count == 0) {
            s_idle_ticks++;
            if (s_idle_ticks >= IDLE_DELETE_TICKS) {
                ESP_LOGI(TAG, "Idle 2 min — auto-deleting played message");
                s_has_played = false;
                s_idle_ticks = 0;
                update_msg_state();
                refresh_msg_bubble();
            }
        } else {
            s_idle_ticks = 0;
        }
    }

    lvgl_port_unlock();
}

/* ── Screen builders ────────────────────────────────────────────── */

static void build_home(void)
{
    s_scr_home = lv_obj_create(NULL);
    style_base_screen(s_scr_home);

    size_t n = model_family_count();
    if (n > MAX_FAMILY_CIRCLES) n = MAX_FAMILY_CIRCLES;
    s_ring_n = n;

    for (size_t i = 0; i < n; i++) {
        const family_t *f = model_family_by_index(i);
        if (!f) continue;

        lv_coord_t px, py;
        ring_pos((int)i, (int)n, &px, &py);
        uint32_t c = fam_color(f);

        lv_obj_t *circ = lv_button_create(s_scr_home);
        lv_obj_remove_style_all(circ);
        lv_obj_set_pos(circ, px, py);
        lv_obj_set_size(circ, RING_CIRCLE_DIA, RING_CIRCLE_DIA);
        lv_obj_set_style_radius(circ, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(circ, lv_color_hex(c), 0);
        lv_obj_set_style_bg_opa(circ, LV_OPA_COVER, 0);

        /* Neon glow matching the family colour */
        lv_obj_set_style_shadow_width(circ, 16, 0);
        lv_obj_set_style_shadow_ofs_y(circ, 0, 0);
        lv_obj_set_style_shadow_color(circ, lv_color_hex(c), 0);
        lv_obj_set_style_shadow_opa(circ, LV_OPA_70, 0);

        if (f->is_broadcast) {
            /* ALL: thick magenta border for extra chaos */
            lv_obj_set_style_border_width(circ, 3, 0);
            lv_obj_set_style_border_color(circ, lv_color_hex(COL_ALL_BORDER), 0);
            lv_obj_set_style_border_opa(circ, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_border_width(circ, 0, 0);
        }

        no_scroll(circ);
        lv_obj_add_flag(circ, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(circ, on_ring_circle_tapped, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        lv_obj_t *lbl = lv_label_create(circ);
        lv_label_set_text(lbl, f->abbr);
        lv_obj_set_style_text_color(lbl,
            lv_color_hex(f->is_broadcast ? COL_ALL_TEXT : 0xffffff), 0);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(lbl, 0, 0);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(lbl);

        s_ring_circles[i] = circ;
        s_ring_labels[i]  = lbl;
    }

    /* Center message bubble */
    s_msg_bubble = lv_button_create(s_scr_home);
    lv_obj_remove_style_all(s_msg_bubble);
    lv_obj_set_size(s_msg_bubble, MSG_BUBBLE_DIA, MSG_BUBBLE_DIA);
    lv_obj_set_style_radius(s_msg_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_msg_bubble, lv_color_hex(COL_MSG_READY), 0);
    lv_obj_set_style_bg_opa(s_msg_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_msg_bubble, 8, 0);
    lv_obj_set_style_shadow_ofs_y(s_msg_bubble, 0, 0);
    lv_obj_set_style_shadow_color(s_msg_bubble, lv_color_hex(COL_MSG_READY), 0);
    lv_obj_set_style_shadow_opa(s_msg_bubble, LV_OPA_70, 0);
    lv_obj_align(s_msg_bubble, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_msg_bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_msg_bubble, on_msg_bubble_tapped, LV_EVENT_CLICKED, NULL);
    no_scroll(s_msg_bubble);

    /* Play / loop icon */
    s_msg_icon_lbl = lv_label_create(s_msg_bubble);
    lv_label_set_text(s_msg_icon_lbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(s_msg_icon_lbl, lv_color_hex(0x1a1400), 0);
    lv_obj_set_style_bg_opa(s_msg_icon_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_icon_lbl, 0, 0);
    lv_obj_add_flag(s_msg_icon_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_msg_icon_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg_icon_lbl, LV_ALIGN_CENTER, 0, -7);

    /* Message count number */
    s_msg_count_lbl = lv_label_create(s_msg_bubble);
    lv_label_set_text(s_msg_count_lbl, "0");
    lv_obj_set_style_text_color(s_msg_count_lbl, lv_color_hex(0x1a1400), 0);
    lv_obj_set_style_bg_opa(s_msg_count_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_count_lbl, 0, 0);
    lv_obj_add_flag(s_msg_count_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_msg_count_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg_count_lbl, LV_ALIGN_CENTER, 0, 8);

    /* Initialise from model */
    s_new_msg_count = model_inbox_unread_count();
    update_msg_state();
    refresh_msg_bubble();
    if (s_msg_state != MSG_NONE) {
        start_bubble_pulse(s_msg_bubble);
    }
}

static void build_record(void)
{
    s_scr_record = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr_record);
    lv_obj_set_size(s_scr_record, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(s_scr_record, lv_color_hex(0xff0000), 0); /* overwritten on open */
    lv_obj_set_style_bg_opa(s_scr_record, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_scr_record, 0, 0);
    no_scroll(s_scr_record);
    lv_obj_clear_flag(s_scr_record, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Big record / stop button (centre) */
    s_rec_btn = lv_button_create(s_scr_record);
    lv_obj_remove_style_all(s_rec_btn);
    lv_obj_set_size(s_rec_btn, REC_BTN_DIA, REC_BTN_DIA);
    lv_obj_set_style_radius(s_rec_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(COL_REC_BTN), 0);
    lv_obj_set_style_bg_opa(s_rec_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_rec_btn, 22, 0);
    lv_obj_set_style_shadow_ofs_y(s_rec_btn, 0, 0);
    lv_obj_set_style_shadow_color(s_rec_btn, lv_color_hex(COL_REC_BTN), 0);
    lv_obj_set_style_shadow_opa(s_rec_btn, LV_OPA_60, 0);
    lv_obj_align(s_rec_btn, LV_ALIGN_CENTER, 0, -14);
    lv_obj_add_flag(s_rec_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rec_btn, on_rec_btn_tapped, LV_EVENT_CLICKED, NULL);
    no_scroll(s_rec_btn);

    /* White inner dot (idle state indicator — looks like a record button) */
    s_rec_idle_dot = lv_obj_create(s_rec_btn);
    lv_obj_remove_style_all(s_rec_idle_dot);
    lv_obj_set_size(s_rec_idle_dot, IDLE_DOT_DIA, IDLE_DOT_DIA);
    lv_obj_set_style_radius(s_rec_idle_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_rec_idle_dot, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_rec_idle_dot, LV_OPA_COVER, 0);
    lv_obj_align(s_rec_idle_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_rec_idle_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_rec_idle_dot, LV_OBJ_FLAG_EVENT_BUBBLE);

    /* Stop symbol (shown while recording) */
    s_rec_stop_lbl = lv_label_create(s_rec_btn);
    lv_label_set_text(s_rec_stop_lbl, LV_SYMBOL_STOP);
    lv_obj_set_style_text_color(s_rec_stop_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_rec_stop_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_rec_stop_lbl, 0, 0);
    lv_obj_align(s_rec_stop_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_rec_stop_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_rec_stop_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_rec_stop_lbl, LV_OBJ_FLAG_CLICKABLE);

    /* Back button — below center */
    lv_obj_t *back = lv_button_create(s_scr_record);
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, BACK_BTN_DIA, BACK_BTN_DIA);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_BACK), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(back, 2, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_30, 0);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, 68);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back, on_back_tapped, LV_EVENT_CLICKED, NULL);
    no_scroll(back);

    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(back_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back_lbl, 0, 0);
    lv_obj_add_flag(back_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(back_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(back_lbl);

    /* "WHOOSH!" sent notification */
    s_sent_lbl = lv_label_create(s_scr_record);
    lv_label_set_text(s_sent_lbl, "WHOOSH!");
    lv_obj_set_style_text_color(s_sent_lbl, lv_color_hex(COL_SENT_TEXT), 0);
    lv_obj_set_style_text_font(s_sent_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_opa(s_sent_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_sent_lbl, 0, 0);
    lv_obj_align(s_sent_lbl, LV_ALIGN_CENTER, 0, -14);
    lv_obj_add_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_sent_lbl, LV_OBJ_FLAG_CLICKABLE);
}

/* ── Entry point ─────────────────────────────────────────────────── */

esp_err_t ui_app_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Bullerby wacky UI — %u families", (unsigned)model_family_count());
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;

    (void)disp;
    build_home();
    build_record();
    lv_screen_load(s_scr_home);
    lv_timer_create(app_tick_cb, TICK_MS, NULL);

    lvgl_port_unlock();
    return ESP_OK;
}
