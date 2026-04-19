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
#include "esp_random.h"

#include "app_audio.h"
#include "hal/hal.h"
#include "fonts.h"
#include "family_emoji_assets.h"
#include "model_families.h"
#include "model_messages.h"
#include "src/themes/default/lv_theme_default.h"

static const char *TAG = "ui_app";

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ── Display geometry ───────────────────────────────────────────── */
#define DISC_CX            (LCD_H_RES / 2)
#define DISC_CY            (LCD_V_RES / 2)
/** Reference ring at n≈9 (tune `RING_GEOM_MARGIN` to match). */
#define RING_REF_DIA       56
/** `family_emoji_ring` assets are square; display size scales with ring (1.5× intrinsic at ref). */
#define RING_EMOJI_ASSET_PX 32
#define RING_EMOJI_SCALE_VS_REF 1.5f
#define MSG_BUBBLE_DIA     90           /* center inbox / notification bubble */
#define REC_BTN_DIA        84           /* big record / stop button */
#define BACK_BTN_DIA       38           /* back button */
#define IDLE_DOT_DIA       26           /* white dot inside idle rec button */
#define MAX_FAMILY_CIRCLES 16

/* ── Timing ─────────────────────────────────────────────────────── */
#define TICK_MS            200
#define SENT_TICKS         (3000   / TICK_MS)
#define IDLE_DELETE_TICKS  (120000 / TICK_MS)
#define RECORD_MAX_TICKS   (30000  / TICK_MS)

/* ── Colour palette ─────────────────────────────────────────────── */
#define COL_BG          0x0d0921    /* deep space purple-black */
#define COL_MSG_READY   0xffdd00    /* neon gold  — new messages */
#define COL_MSG_PLAYED  0xff7700    /* orange     — replay */
#define COL_REC_BTN     0xff1144    /* hot red    — idle record */
#define COL_STOP_BTN    0xff5500    /* lava orange — recording → stop */
#define COL_BACK        0x18102e    /* near-black — back button */
#define COL_SENT_TEXT   0x22ff88    /* neon mint — random send toast (k_sent_toasts) */
#define COL_ALL_FILL    0xffffff
#define COL_ALL_BORDER  0xff00ff    /* magenta rim on ALLA circle */
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
static lv_obj_t *s_scr_wifi_setup;
static lv_obj_t *s_wifi_setup_lbl;

/* Home ring */
static lv_obj_t *s_ring_circles[MAX_FAMILY_CIRCLES];
static lv_obj_t *s_ring_imgs[MAX_FAMILY_CIRCLES];
static size_t    s_ring_n;

/* Center message bubble */
static lv_obj_t *s_msg_bubble;
static lv_obj_t *s_msg_icon_lbl;
static lv_obj_t *s_msg_count_lbl;

/* Record screen */
static lv_obj_t *s_rec_title_lbl;
static lv_obj_t *s_rec_btn;
static lv_obj_t *s_rec_idle_dot;   /* white circle: shown when not recording */
static lv_obj_t *s_rec_stop_lbl;   /* STOP symbol: shown while recording */
static lv_obj_t *s_sent_lbl;       /* random wacky send toast */

static size_t      s_open_family_idx = 0;
static bool        s_recording       = false;
static uint32_t    s_rec_ticks       = 0;
static uint32_t    s_sent_countdown  = 0;
static msg_state_t s_msg_state       = MSG_NONE;
static uint32_t    s_new_msg_count   = 0;
static bool        s_has_played      = false;
static uint32_t    s_idle_ticks      = 0;

/* One line each; shown at random after send (Swedish-flavour kid chaos). */
static const char *const k_sent_toasts[] = {
    "Svisch!",
    "Pang i bygget!",
    "Fläng!",
    "Kör läget!",
    "Nu åker vi!",
    "Wohoaa!",
    "Smällsäng!",
    "Bubbelkalas!",
    "Zlång!",
    "Krockosping!",
    "Flippflapp!",
    "Snurrbåt!",
    "Glitterpang!",
    "Wooshmåns!",
    "Svischmos!",
    "Krullkram!",
    "Zappakram!",
    "Plupp!",
    "Brrrting!",
    "Swischpatrull!",
    "Ljudkul!",
    "Kaskadkram!",
    "Fnissblast!",
    "Trångsnyft!",
    "Mjahaa!",
    "Pysch!",
    "Klonk i kosmos!",
    "Stjärnsmysch!",
    "Flingflong!",
    "Bärsärkbar!",
};
#define K_SENT_TOASTS_N (sizeof(k_sent_toasts) / sizeof(k_sent_toasts[0]))

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

/**
 * Ring radius and bubble diameter from visible count: fewer slots → larger bubbles,
 * staying inside the round LCD and leaving a small gap along the arc (margin).
 */
#define RING_GEOM_MARGIN   1.08f
#define RING_GEOM_PAD_PX   3

static void home_ring_geometry(int n, int *out_r, int *out_dia)
{
    if (n < 1) {
        n = 1;
    }
    float max_extent = (float)DISC_CX - (float)RING_GEOM_PAD_PX;
    if (n == 1) {
        int dia = (int)lroundf(fminf(80.0f, max_extent * 0.55f));
        if (dia < 48) {
            dia = 48;
        }
        *out_dia = dia;
        *out_r = (int)lroundf(max_extent - (float)dia * 0.5f);
        return;
    }
    float sn = 2.0f * sinf((float)M_PI / (float)n) / RING_GEOM_MARGIN;
    float denom = 1.0f + sn * 0.5f;
    float dia = sn * max_extent / denom;
    if (dia > 80.0f) {
        dia = 80.0f;
    }
    if (dia < 40.0f) {
        dia = 40.0f;
    }
    int idia = (int)lroundf(dia);
    float r = max_extent - (float)idia * 0.5f;
    if (r < 36.0f) {
        r = 36.0f;
    }
    *out_dia = idia;
    *out_r = (int)lroundf(r);
}

/** Target emoji box side in px: proportional to `ring_dia`, 50% larger than 32 px at `RING_REF_DIA`. */
static int ring_emoji_box_px(int ring_dia)
{
    float px = (float)ring_dia * (float)RING_EMOJI_ASSET_PX * RING_EMOJI_SCALE_VS_REF / (float)RING_REF_DIA;
    int n = (int)lroundf(px);
    if (n < 20) {
        n = 20;
    }
    int max = ring_dia - 6;
    if (max < 20) {
        max = 20;
    }
    if (n > max) {
        n = max;
    }
    return n;
}

/* Clockwise from 12-o'clock. Top-left for a circle of size `circle_dia`. */
static void ring_pos(int idx, int n, int ring_r, int circle_dia, lv_coord_t *px, lv_coord_t *py)
{
    float theta = 2.0f * (float)M_PI * idx / (float)n;
    int cx = DISC_CX + (int)lroundf((float)ring_r * sinf(theta));
    int cy = DISC_CY - (int)lroundf((float)ring_r * cosf(theta));
    int half = circle_dia / 2;
    *px = (lv_coord_t)(cx - half);
    *py = (lv_coord_t)(cy - half);
}

/** Index in `model_family_by_index` for emoji assets and event routing. */
static size_t model_index_for_family(const family_t *f)
{
    if (!f) {
        return 0;
    }
    for (size_t i = 0; i < model_family_count(); i++) {
        const family_t *x = model_family_by_index(i);
        if (x && x->id == f->id) {
            return i;
        }
    }
    return 0;
}

/**
 * Home ring lists: ALLA first (12 o'clock), then every other family except this
 * device's own (`model_my_family_id`). `out[]` receives pointers into model data.
 */
static size_t home_ring_families(const family_t *out[MAX_FAMILY_CIRCLES])
{
    size_t n = 0;

    for (size_t i = 0; i < model_family_count(); i++) {
        const family_t *f = model_family_by_index(i);
        if (f && f->is_broadcast) {
            if (n < MAX_FAMILY_CIRCLES) {
                out[n++] = f;
            }
            break;
        }
    }

    for (size_t i = 0; i < model_family_count(); i++) {
        const family_t *f = model_family_by_index(i);
        if (!f || f->is_broadcast) {
            continue;
        }
        if (f->id == model_my_family_id) {
            continue;
        }
        if (n >= MAX_FAMILY_CIRCLES) {
            break;
        }
        out[n++] = f;
    }

    return n;
}

/* ── Pulse animation on message bubble ──────────────────────────── */

static void bubble_glow_exec(void *obj, int32_t v)
{
    lv_obj_set_style_shadow_width((lv_obj_t *)obj, (int32_t)v, 0);
}

static void start_bubble_pulse(lv_obj_t *bubble)
{
    lv_anim_del(bubble, bubble_glow_exec);
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
        lv_obj_set_style_shadow_width(s_msg_bubble, 8, 0);
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
        /* Pulse only while unheard messages remain. */
        start_bubble_pulse(s_msg_bubble);
    } else { /* MSG_PLAYED */
        lv_obj_set_style_bg_color(s_msg_bubble, lv_color_hex(COL_MSG_PLAYED), 0);
        lv_obj_set_style_shadow_color(s_msg_bubble, lv_color_hex(COL_MSG_PLAYED), 0);
        lv_label_set_text(s_msg_icon_lbl, LV_SYMBOL_LOOP);
        if (s_msg_count_lbl) {
            lv_obj_add_flag(s_msg_count_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        /* Steady orange, no pulse — replay is available but no new info. */
        lv_anim_del(s_msg_bubble, bubble_glow_exec);
        lv_obj_set_style_shadow_width(s_msg_bubble, 12, 0);
    }
}

/* ── Stop-and-send logic (called from button or auto-timer) ─────── */

static void do_send_stop(void)
{
    app_audio_set_ui_recording(false);
    s_recording  = false;
    s_rec_ticks  = 0;
    if (s_rec_btn)      lv_obj_add_flag(s_rec_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_sent_lbl) {
        unsigned pick = (unsigned)(esp_random() % K_SENT_TOASTS_N);
        lv_label_set_text(s_sent_lbl, k_sent_toasts[pick]);
        lv_obj_clear_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
    }
    s_sent_countdown = SENT_TICKS;
    const family_t *f = model_family_by_index(s_open_family_idx);
    ESP_LOGI(TAG, "Stop/send UI: %s (see main: [UI] logs for capture + playback)", f ? f->name : "?");
}

/* ── Event handlers ─────────────────────────────────────────────── */

static void on_msg_bubble_tapped(lv_event_t *e)
{
    (void)e;
    s_idle_ticks = 0;

    if (s_msg_state == MSG_AVAILABLE) {
        ESP_LOGI(TAG, "Bubble tapped → replay (unread=%lu)",
                 (unsigned long)s_new_msg_count);
        /* One tap clears the whole unread stack. Product semantics in
         * docs/project-plan.md §3.0: short FIFO / delete after listen. */
        s_new_msg_count = 0;
        s_has_played = true;
    } else if (s_msg_state == MSG_PLAYED) {
        ESP_LOGI(TAG, "Bubble tapped → replay again");
    } else {
        return;
    }

    app_audio_request_replay();

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
    app_audio_set_ui_recording(false);
    s_recording       = false;
    s_rec_ticks       = 0;
    s_sent_countdown  = 0;

    /* Flood the record screen with the family's colour. */
    uint32_t c = f->is_broadcast ? 0xdd00cc : fam_color(f);
    lv_obj_set_style_bg_color(s_scr_record, lv_color_hex(c), 0);

    if (s_rec_title_lbl) {
        lv_label_set_text(s_rec_title_lbl, f->name);
    }

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
        app_audio_set_ui_recording(true);
        if (s_rec_idle_dot) lv_obj_add_flag(s_rec_idle_dot, LV_OBJ_FLAG_HIDDEN);
        if (s_rec_stop_lbl) lv_obj_clear_flag(s_rec_stop_lbl, LV_OBJ_FLAG_HIDDEN);
        if (s_rec_btn) {
            lv_obj_set_style_bg_color(s_rec_btn, lv_color_hex(COL_STOP_BTN), 0);
            lv_obj_set_style_shadow_color(s_rec_btn, lv_color_hex(COL_STOP_BTN), 0);
        }
        const family_t *f = model_family_by_index(s_open_family_idx);
        ESP_LOGI(TAG, "Record pressed — %s (main task will log [UI] when mic is live)", f ? f->name : "?");
    } else {
        do_send_stop();
    }
}

static void on_back_tapped(lv_event_t *e)
{
    (void)e;
    app_audio_set_ui_recording(false);
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

    /* Send-toast countdown → return to home */
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

static void home_ring_destroy(void)
{
    for (size_t i = 0; i < s_ring_n; i++) {
        if (s_ring_circles[i]) {
            lv_obj_delete(s_ring_circles[i]);
            s_ring_circles[i] = NULL;
            s_ring_imgs[i] = NULL;
        }
    }
    s_ring_n = 0;
}

/** Build ring circles as children of `scr` (home screen). */
static void home_ring_build_on_screen(lv_obj_t *scr)
{
    const family_t *ring[MAX_FAMILY_CIRCLES];
    size_t n = home_ring_families(ring);
    if (n == 0) {
        ESP_LOGW(TAG, "home ring: no families to show");
    }
    s_ring_n = n;

    int ring_r = 90;
    int ring_dia = RING_REF_DIA;
    if (n > 0) {
        home_ring_geometry((int)n, &ring_r, &ring_dia);
        ESP_LOGD(TAG, "home ring geometry: n=%u r=%d dia=%d", (unsigned)n, ring_r, ring_dia);
    }

    for (size_t i = 0; i < n; i++) {
        const family_t *f = ring[i];
        if (!f) {
            continue;
        }

        size_t model_idx = model_index_for_family(f);

        lv_coord_t px, py;
        ring_pos((int)i, (int)n, ring_r, ring_dia, &px, &py);
        uint32_t c = fam_color(f);

        lv_obj_t *circ = lv_button_create(scr);
        lv_obj_remove_style_all(circ);
        lv_obj_set_pos(circ, px, py);
        lv_obj_set_size(circ, ring_dia, ring_dia);
        lv_obj_set_style_radius(circ, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(circ, lv_color_hex(c), 0);
        lv_obj_set_style_bg_opa(circ, LV_OPA_COVER, 0);

        {
            int sw = (int)lroundf(16.0f * (float)ring_dia / (float)RING_REF_DIA);
            if (sw < 12) {
                sw = 12;
            }
            if (sw > 24) {
                sw = 24;
            }
            lv_obj_set_style_shadow_width(circ, sw, 0);
        }
        lv_obj_set_style_shadow_ofs_y(circ, 0, 0);
        lv_obj_set_style_shadow_color(circ, lv_color_hex(c), 0);
        lv_obj_set_style_shadow_opa(circ, LV_OPA_70, 0);

        if (f->is_broadcast) {
            lv_obj_set_style_border_width(circ, 3, 0);
            lv_obj_set_style_border_color(circ, lv_color_hex(COL_ALL_BORDER), 0);
            lv_obj_set_style_border_opa(circ, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_border_width(circ, 0, 0);
        }

        no_scroll(circ);
        lv_obj_add_flag(circ, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(circ, on_ring_circle_tapped, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)model_idx);

        lv_obj_t *em = lv_image_create(circ);
        if (model_idx < FAMILY_EMOJI_RING_COUNT) {
            lv_image_set_src(em, family_emoji_ring[model_idx]);
        }
        {
            int em_sz = ring_emoji_box_px(ring_dia);
            uint32_t z = (uint32_t)lroundf(256.0f * (float)em_sz / (float)RING_EMOJI_ASSET_PX);
            lv_image_set_scale(em, z);
            lv_image_set_antialias(em, true);
        }
        lv_obj_add_flag(em, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(em, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(em);

        s_ring_circles[i] = circ;
        s_ring_imgs[i]    = em;
    }
}

void ui_app_rebuild_home_ring(void)
{
    if (!s_scr_home) {
        return;
    }
    if (!lvgl_port_lock(5000)) {
        ESP_LOGW(TAG, "rebuild ring: lvgl lock timeout");
        return;
    }
    home_ring_destroy();
    home_ring_build_on_screen(s_scr_home);
    if (s_msg_bubble) {
        lv_obj_move_foreground(s_msg_bubble);
    }
    lvgl_port_unlock();
}

static void build_home(void)
{
    s_scr_home = lv_obj_create(NULL);
    style_base_screen(s_scr_home);

    home_ring_build_on_screen(s_scr_home);

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

    /* Play / loop icon + count (large; default theme font is 14 px) */
    s_msg_icon_lbl = lv_label_create(s_msg_bubble);
    lv_label_set_text(s_msg_icon_lbl, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_msg_icon_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_msg_icon_lbl, lv_color_hex(0x1a1400), 0);
    lv_obj_set_style_bg_opa(s_msg_icon_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_icon_lbl, 0, 0);
    lv_obj_add_flag(s_msg_icon_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_msg_icon_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg_icon_lbl, LV_ALIGN_CENTER, 0, -12);

    /* Message count number */
    s_msg_count_lbl = lv_label_create(s_msg_bubble);
    lv_label_set_text(s_msg_count_lbl, "0");
    lv_obj_set_style_text_font(s_msg_count_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_msg_count_lbl, lv_color_hex(0x1a1400), 0);
    lv_obj_set_style_bg_opa(s_msg_count_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_msg_count_lbl, 0, 0);
    lv_obj_add_flag(s_msg_count_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_msg_count_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_msg_count_lbl, LV_ALIGN_CENTER, 0, 14);

    /* Initialise from model */
    s_new_msg_count = model_inbox_unread_count();
    update_msg_state();
    refresh_msg_bubble();
    if (s_msg_state != MSG_NONE) {
        start_bubble_pulse(s_msg_bubble);
    }

    lv_obj_move_foreground(s_msg_bubble);
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

    lv_obj_t *title_bar = lv_obj_create(s_scr_record);
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, LCD_H_RES, 52);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_40, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_CLICKABLE);
    no_scroll(title_bar);

    s_rec_title_lbl = lv_label_create(title_bar);
    lv_label_set_text(s_rec_title_lbl, "");
    lv_obj_set_style_text_font(s_rec_title_lbl, &lv_font_montserrat_20_latin1, 0);
    lv_obj_set_style_text_color(s_rec_title_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(s_rec_title_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_width(s_rec_title_lbl, LCD_H_RES - 16);
    lv_label_set_long_mode(s_rec_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_rec_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_rec_title_lbl, LV_ALIGN_CENTER, 0, 0);

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

    /* Sent notification — text set in do_send_stop() */
    s_sent_lbl = lv_label_create(s_scr_record);
    lv_label_set_text(s_sent_lbl, k_sent_toasts[0]);
    lv_obj_set_style_text_color(s_sent_lbl, lv_color_hex(COL_SENT_TEXT), 0);
    lv_obj_set_style_text_font(s_sent_lbl, &lv_font_montserrat_20_latin1, 0);
    lv_obj_set_style_bg_opa(s_sent_lbl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_sent_lbl, 0, 0);
    lv_obj_align(s_sent_lbl, LV_ALIGN_CENTER, 0, -14);
    lv_obj_add_flag(s_sent_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_sent_lbl, LV_OBJ_FLAG_CLICKABLE);
}

void ui_app_on_new_message(const char *from_label)
{
    if (!lvgl_port_lock(5000)) {
        ESP_LOGW(TAG, "on_new_message: lvgl lock timeout — bubble stale");
        return;
    }

    s_new_msg_count++;
    s_has_played   = false;
    s_idle_ticks   = 0;
    update_msg_state();
    refresh_msg_bubble();

    ESP_LOGI(TAG, "new message from %s (unread=%lu)",
             from_label ? from_label : "?",
             (unsigned long)s_new_msg_count);

    lvgl_port_unlock();
}

void ui_app_show_wifi_setup(const char *ap_ssid)
{
    if (!ap_ssid) {
        ap_ssid = "?";
    }
    if (!lvgl_port_lock(5000)) {
        ESP_LOGW(TAG, "wifi setup UI: lvgl lock timeout");
        return;
    }

    if (!s_scr_wifi_setup) {
        s_scr_wifi_setup = lv_obj_create(NULL);
        style_base_screen(s_scr_wifi_setup);
        s_wifi_setup_lbl = lv_label_create(s_scr_wifi_setup);
        lv_obj_set_style_text_color(s_wifi_setup_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(s_wifi_setup_lbl, &lv_font_montserrat_14_latin1, 0);
        lv_label_set_long_mode(s_wifi_setup_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_wifi_setup_lbl, LCD_H_RES - 24);
        lv_obj_set_style_text_align(s_wifi_setup_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_wifi_setup_lbl, LV_ALIGN_CENTER, 0, 0);
        no_scroll(s_wifi_setup_lbl);
    }

    char buf[96];
    snprintf(buf, sizeof(buf), "Connect to\n%s\nto set up WiFi", ap_ssid);
    lv_label_set_text(s_wifi_setup_lbl, buf);
    lv_screen_load(s_scr_wifi_setup);

    lvgl_port_unlock();
}

/* ── Entry point ─────────────────────────────────────────────────── */

esp_err_t ui_app_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Bullerby wacky UI — %u families", (unsigned)model_family_count());
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;

    lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                          LV_THEME_DEFAULT_DARK, &lv_font_montserrat_14_latin1);
    lv_display_set_theme(disp, lv_theme_default_get());

    build_home();
    build_record();
    lv_screen_load(s_scr_home);
    lv_timer_create(app_tick_cb, TICK_MS, NULL);

    lvgl_port_unlock();
    return ESP_OK;
}
