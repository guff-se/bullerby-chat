/*
 * Home: ui-spec §2 — horizontal family strip, focal scale, cyclic index, round safe area.
 * Recording / Inbox: single column flex inside the same disc so controls do not overlap.
 */

#include "ui_app.h"

#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "hal/hal.h"
#include "model_families.h"
#include "model_messages.h"

static const char *TAG = "ui_app";

#define RECORD_UI_MAX_MS 30000
#define INBOX_ROWS_MAX   8

/**
 * Main panel fills the framebuffer (240×240). The physical display is round; do not
 * use LV_RADIUS_CIRCLE on this layer — it only fills an inscribed circle and leaves
 * the square’s corners transparent (black screen bg), which reads as huge black arcs
 * on the glass. Inner padding keeps ui-spec “safe” inset; the glass masks corners.
 */
#define UI_PANEL_PAD     10

/** Status row height (battery, inbox) — used with STRIP_ROW_H to center the strip vertically. */
#define HOME_STATUS_H    30

/**
 * Strip band height (must fit largest bubble + shadow). Larger = bigger carousel on glass.
 * HOME_STRIP_TOP_SPACER is chosen so the strip is vertically centered in the 240×240 disc
 * (status + flex padding above vs grow_bot below).
 */
#define STRIP_ROW_H      136
#define HOME_STRIP_TOP_SPACER \
    LV_MAX(0, (int)(LCD_V_RES / 2 - STRIP_ROW_H / 2 - UI_PANEL_PAD - HOME_STATUS_H - 6 - 6))

/** Five slots: −2 … +1 … +2 from focal (center slot = primary tap). */
#define STRIP_NUM_SLOTS  5
#define STRIP_FOCAL_IDX  2
/** ~spacing between slot centers — used for scroll wrap and drag quantization. */
#define STRIP_PX_PER_STEP 44
/** Ignore bubble tap if finger moved more than this (screen px). */
#define STRIP_TAP_DRAG_MAX 22

/** Focal family zoom: main bubble (leave room for three rim icon buttons on the disc). */
#define ZOOM_BUBBLE_SIZE 208
/** transform_scale initial value (≈ focal bubble size / zoom size * 256) to feel like a continuing grow. */
#define ZOOM_SCALE_FROM 120
#define ZOOM_SCALE_TO   256  /* LV_SCALE_NONE */

typedef enum {
    SCREEN_HOME = 0,
    SCREEN_RECORDING,
    SCREEN_INBOX,
} screen_id_t;

static lv_obj_t *s_scr_home;
static lv_obj_t *s_home_disc;
static lv_obj_t *s_scr_record;
static lv_obj_t *s_scr_inbox;

/** Dedicated zoom screen (not a sys_layer overlay — screens reliably stack above each other). */
static lv_obj_t *s_scr_zoom;
static lv_obj_t *s_zoom_bubble;
static lv_obj_t *s_zoom_lbl;
static lv_obj_t *s_zoom_btn_play;
static lv_obj_t *s_zoom_btn_stop;
static lv_obj_t *s_zoom_btn_back;
static size_t s_zoom_family_index = 0;

static lv_obj_t *s_lbl_batt;
static lv_obj_t *s_lbl_inbox;

static lv_obj_t *s_strip_container;
static lv_obj_t *s_strip_bubbles[STRIP_NUM_SLOTS];
static lv_obj_t *s_strip_labels[STRIP_NUM_SLOTS];

static lv_obj_t *s_lbl_record_title;
static lv_obj_t *s_lbl_record_elapsed;
static lv_obj_t *s_lbl_record_status;
static lv_obj_t *s_btn_record;
static lv_obj_t *s_btn_stop;

static lv_obj_t *s_inbox_btns[INBOX_ROWS_MAX];
static lv_obj_t *s_inbox_lbls[INBOX_ROWS_MAX];

static size_t s_carousel_idx;
static uint8_t s_record_target_id;
static bool s_recording_ui_active;
static uint32_t s_recording_elapsed_ms;

/** Horizontal pan offset (px); strip_wrap_scroll() folds into s_carousel_idx past ±STEP/2. */
static int32_t s_strip_scroll_px;
static int32_t s_drag_last_x;
static uint32_t s_drag_abs_sum;

static uint32_t family_bubble_color(const family_t *f)
{
    if (f->is_broadcast) {
        return 0xff3399;
    }
    static const uint32_t palette[] = {
        0x4466ff, 0xff6644, 0x33cc99, 0xcc55dd, 0xffcc22, 0x22ccee,
    };
    return palette[(f->id - 1) % (sizeof(palette) / sizeof(palette[0]))];
}

static void format_mmss(char *buf, size_t len, uint32_t ms)
{
    uint32_t total_s = ms / 1000;
    unsigned m = (unsigned)(total_s / 60);
    unsigned s = (unsigned)(total_s % 60);
    snprintf(buf, len, "%u:%02u", m, s);
}

/** Ring index for carousel: (logical + n) % n with negative logical handled. */
static size_t carousel_index_at_rel(int rel)
{
    size_t n = model_family_count();
    if (n == 0) {
        return 0;
    }
    int idx = (int)s_carousel_idx + rel;
    while (idx < 0) {
        idx += (int)n;
    }
    while (idx >= (int)n) {
        idx -= (int)n;
    }
    return (size_t)idx;
}

static void strip_wrap_scroll(void)
{
    size_t n = model_family_count();
    if (n == 0) {
        return;
    }
    const int32_t half = STRIP_PX_PER_STEP / 2;
    while (s_strip_scroll_px >= half) {
        s_carousel_idx = (s_carousel_idx + n - 1) % n;
        s_strip_scroll_px -= STRIP_PX_PER_STEP;
    }
    while (s_strip_scroll_px <= -half) {
        s_carousel_idx = (s_carousel_idx + 1) % n;
        s_strip_scroll_px += STRIP_PX_PER_STEP;
    }
}

/**
 * Size at fractional distance |d| from focal (d in units of slots).
 * Piecewise linear key points: (0,98), (1,68), (2,50); clamped to 50 beyond.
 * Returns diameter in pixels.
 */
static int strip_size_at(int d_tenths_abs)
{
    if (d_tenths_abs >= 20) {
        return 50;
    }
    if (d_tenths_abs >= 10) {
        /* 68 → 50 as d_tenths goes 10 → 20 */
        return 68 - (18 * (d_tenths_abs - 10)) / 10;
    }
    /* 98 → 68 as d_tenths goes 0 → 10 */
    return 98 - (30 * d_tenths_abs) / 10;
}

static void strip_refresh(void)
{
    size_t n = model_family_count();
    if (n == 0) {
        return;
    }

    const int16_t focal_cx = 110;  /* center of focal slot inside strip_container */
    const int16_t cy = STRIP_ROW_H / 2;

    /* Each slot's content shifts with s_strip_scroll_px so bubbles flow through positions;
     * size interpolates continuously so focal always reads as the biggest on glass. */
    int d_tenths_abs[STRIP_NUM_SLOTS];

    for (int slot = 0; slot < STRIP_NUM_SLOTS; slot++) {
        lv_obj_t *b = s_strip_bubbles[slot];
        lv_obj_t *lb = s_strip_labels[slot];
        if (!b || !lb || lv_obj_get_parent(b) != s_strip_container) {
            d_tenths_abs[slot] = 99;
            continue;
        }

        int rel = slot - STRIP_FOCAL_IDX;
        size_t fi = carousel_index_at_rel(rel);

        const family_t *f = model_family_by_index(fi);
        if (!f) {
            d_tenths_abs[slot] = 99;
            continue;
        }

        /* d in tenths-of-slot (integer math): d = rel + scroll_px/STEP. */
        int d_t = rel * 10 + (s_strip_scroll_px * 10) / STRIP_PX_PER_STEP;
        int d_abs = d_t < 0 ? -d_t : d_t;
        d_tenths_abs[slot] = d_abs;

        int dia = strip_size_at(d_abs);
        lv_obj_set_size(b, dia, dia);
        lv_coord_t bx = (lv_coord_t)(focal_cx + rel * STRIP_PX_PER_STEP + s_strip_scroll_px - dia / 2);
        lv_obj_set_pos(b, bx, (lv_coord_t)(cy - dia / 2));

        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(family_bubble_color(f)), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, f->is_broadcast ? 4 : 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_opa(b, f->is_broadcast ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_shadow_width(b, d_abs < 5 ? 12 : 4, 0);
        lv_obj_set_style_shadow_ofs_y(b, 3, 0);
        lv_obj_set_style_shadow_color(b, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(b, LV_OPA_30, 0);

        lv_label_set_text(lb, f->abbr);
        lv_obj_set_style_text_color(lb, lv_color_hex(0xffffff), 0);
        lv_obj_center(lb);
    }

    /* Z-order: smallest |d| on top so whatever is visually closest to focal draws frontmost. */
    int order[STRIP_NUM_SLOTS];
    for (int i = 0; i < STRIP_NUM_SLOTS; i++) {
        order[i] = i;
    }
    for (int i = 0; i < STRIP_NUM_SLOTS - 1; i++) {
        for (int j = i + 1; j < STRIP_NUM_SLOTS; j++) {
            if (d_tenths_abs[order[j]] > d_tenths_abs[order[i]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    for (int k = 0; k < STRIP_NUM_SLOTS; k++) {
        if (d_tenths_abs[order[k]] >= 99) {
            continue;
        }
        lv_obj_move_foreground(s_strip_bubbles[order[k]]);
    }
}

/** Kill LVGL scroll offsets on home ancestors so indev never “slides” the flex column. */
static void home_reset_scroll_offsets(void)
{
    if (s_scr_home) {
        lv_obj_scroll_to(s_scr_home, 0, 0, LV_ANIM_OFF);
    }
    if (s_home_disc) {
        lv_obj_scroll_to(s_home_disc, 0, 0, LV_ANIM_OFF);
    }
    if (s_strip_container) {
        lv_obj_scroll_to(s_strip_container, 0, 0, LV_ANIM_OFF);
    }
}

/** Animation exec: update scroll_px (without wrapping) and re-render — used for snap-back / commit. */
static void strip_snap_anim_exec(void *var, int32_t v)
{
    (void)var;
    s_strip_scroll_px = v;
    strip_refresh();
}

/**
 * On finger release, decide whether the partial drag should commit to the next slot or spring back,
 * then animate scroll_px → 0 so items glide into their resting positions instead of snapping.
 */
static void strip_snap_release(void)
{
    size_t n = model_family_count();
    int32_t from = s_strip_scroll_px;
    const int32_t commit_threshold = STRIP_PX_PER_STEP / 3;

    if (n > 0 && from > commit_threshold) {
        /* Finger moved right past the commit threshold → show previous family as focal. */
        s_carousel_idx = (s_carousel_idx + n - 1) % n;
        from -= STRIP_PX_PER_STEP;
    } else if (n > 0 && from < -commit_threshold) {
        s_carousel_idx = (s_carousel_idx + 1) % n;
        from += STRIP_PX_PER_STEP;
    }
    s_strip_scroll_px = from;

    lv_anim_del(NULL, strip_snap_anim_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_strip_container);
    lv_anim_set_values(&a, from, 0);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_exec_cb(&a, strip_snap_anim_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void strip_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        return;
    }
    if (lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
        return;
    }

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        /* Kill any in-flight snap so new drag starts from current visual position. */
        lv_anim_del(NULL, strip_snap_anim_exec);
        home_reset_scroll_offsets();
        s_drag_last_x = pt.x;
        s_drag_abs_sum = 0;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        home_reset_scroll_offsets();
        int32_t dx = pt.x - s_drag_last_x;
        s_drag_last_x = pt.x;
        if (dx != 0) {
            uint32_t ad = (dx < 0) ? (uint32_t)(-dx) : (uint32_t)dx;
            s_drag_abs_sum += ad;
        }
        s_strip_scroll_px += dx;
        strip_wrap_scroll();
        strip_refresh();
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        strip_wrap_scroll();
        strip_snap_release();
    }
}

static void record_ui_sync_buttons(void)
{
    if (!s_btn_record || !s_btn_stop) {
        return;
    }
    if (s_recording_ui_active) {
        lv_obj_add_state(s_btn_record, LV_STATE_DISABLED);
        lv_obj_clear_state(s_btn_stop, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_btn_record, LV_STATE_DISABLED);
        lv_obj_add_state(s_btn_stop, LV_STATE_DISABLED);
    }
}

static void record_ui_reset(void)
{
    s_recording_ui_active = false;
    s_recording_elapsed_ms = 0;
    if (s_lbl_record_elapsed) {
        lv_label_set_text(s_lbl_record_elapsed, "0:00");
    }
    if (s_lbl_record_status) {
        lv_label_set_text(s_lbl_record_status, "BOOT = mic/speaker test");
    }
    record_ui_sync_buttons();
}

static void record_ui_stop(void)
{
    s_recording_ui_active = false;
    record_ui_sync_buttons();
    if (s_lbl_record_status) {
        lv_label_set_text(s_lbl_record_status, "Stopped. BOOT = audio test.");
    }
}

static void family_bubble_apply_family(const family_t *f, lv_obj_t *bubble, lv_obj_t *lbl)
{
    if (!f || !bubble || !lbl) {
        return;
    }
    lv_obj_set_style_bg_color(bubble, lv_color_hex(family_bubble_color(f)), 0);
    lv_obj_set_style_border_width(bubble, f->is_broadcast ? 5 : 3, 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(bubble, f->is_broadcast ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_label_set_text(lbl, f->abbr);
}

/** transform_scale animation exec used when the zoom screen first appears. */
static void zoom_scale_anim_exec(void *var, int32_t v)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)var, (int32_t)v, 0);
}

static void on_zoom_screen_loaded(lv_event_t *e)
{
    (void)e;
    if (!s_zoom_bubble) {
        return;
    }
    lv_anim_del(s_zoom_bubble, zoom_scale_anim_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_zoom_bubble);
    lv_anim_set_values(&a, ZOOM_SCALE_FROM, ZOOM_SCALE_TO);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_exec_cb(&a, zoom_scale_anim_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void zoom_close(void)
{
    if (!s_scr_home) {
        return;
    }
    lv_screen_load_anim(s_scr_home, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, false);
}

static void zoom_open(size_t fi)
{
    size_t n = model_family_count();
    if (n == 0 || !s_scr_zoom || !s_zoom_bubble || !s_zoom_lbl) {
        return;
    }
    if (fi >= n) {
        fi = 0;
    }
    const family_t *f = model_family_by_index(fi);
    if (!f) {
        return;
    }

    s_zoom_family_index = fi;
    family_bubble_apply_family(f, s_zoom_bubble, s_zoom_lbl);
    lv_obj_center(s_zoom_lbl);

    /* Preset small so the on-screen-loaded callback animates it growing in. */
    lv_obj_set_style_transform_scale(s_zoom_bubble, ZOOM_SCALE_FROM, 0);

    lv_screen_load_anim(s_scr_zoom, LV_SCR_LOAD_ANIM_FADE_ON, 180, 0, false);
}

static void show_screen(screen_id_t id)
{
    if (id == SCREEN_HOME) {
        lv_screen_load(s_scr_home);
    } else if (id == SCREEN_INBOX) {
        lv_screen_load(s_scr_inbox);
    } else {
        lv_screen_load(s_scr_record);
    }
}

static void on_zoom_back_clicked(lv_event_t *e)
{
    (void)e;
    zoom_close();
}

static void on_zoom_play_clicked(lv_event_t *e)
{
    (void)e;
    const family_t *f = model_family_by_index(s_zoom_family_index);
    if (!f) {
        return;
    }
    s_record_target_id = f->id;
    ESP_LOGI(TAG, "Record to family id %u (from zoom)", (unsigned)f->id);
    if (s_lbl_record_title) {
        lv_label_set_text_fmt(s_lbl_record_title, "To: %s", f->name);
    }
    record_ui_reset();
    show_screen(SCREEN_RECORDING);
}

static void on_zoom_stop_clicked(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Family zoom stop (stub — no playback yet)");
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    if (lv_screen_active() == s_scr_record && s_recording_ui_active) {
        record_ui_stop();
    }
    show_screen(SCREEN_HOME);
}

static void on_inbox_back_clicked(lv_event_t *e)
{
    (void)e;
    show_screen(SCREEN_HOME);
}

static void inbox_refresh_rows(void)
{
    char dur[16];
    for (size_t i = 0; i < model_inbox_count() && i < INBOX_ROWS_MAX; i++) {
        const message_t *m = model_inbox_get(i);
        if (!m || !s_inbox_lbls[i]) {
            continue;
        }
        format_mmss(dur, sizeof(dur), m->duration_ms);
        lv_label_set_text_fmt(s_inbox_lbls[i], "%s  %s  %s", m->unread ? "●" : "·", m->from_label, dur);
    }
}

static void on_inbox_row(lv_event_t *e)
{
    size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const message_t *m = model_inbox_get(idx);
    if (!m) {
        return;
    }
    model_inbox_mark_read(idx);
    ESP_LOGI(TAG, "Inbox playback stub: msg %u from \"%s\" (%u ms)", (unsigned)m->id,
             m->from_label, (unsigned)m->duration_ms);
    inbox_refresh_rows();
}

static void on_inbox_open(lv_event_t *e)
{
    (void)e;
    inbox_refresh_rows();
    show_screen(SCREEN_INBOX);
}

static void on_strip_bubble_clicked(lv_event_t *e)
{
    if (s_drag_abs_sum > STRIP_TAP_DRAG_MAX) {
        return;
    }
    unsigned slot = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    size_t n = model_family_count();
    if (n == 0 || slot >= STRIP_NUM_SLOTS) {
        return;
    }

    int rel = (int)slot - STRIP_FOCAL_IDX;
    size_t fi = carousel_index_at_rel(rel);
    const family_t *f = model_family_by_index(fi);
    if (!f) {
        return;
    }

    if (slot == STRIP_FOCAL_IDX) {
        zoom_open(fi);
    } else {
        s_carousel_idx = fi;
        s_strip_scroll_px = 0;
        strip_refresh();
    }
}

static void on_record_start(lv_event_t *e)
{
    (void)e;
    if (s_recording_ui_active) {
        return;
    }
    s_recording_ui_active = true;
    s_recording_elapsed_ms = 0;
    if (s_lbl_record_elapsed) {
        lv_label_set_text(s_lbl_record_elapsed, "0:00");
    }
    if (s_lbl_record_status) {
        lv_label_set_text(s_lbl_record_status, "Recording… (UI timer — BOOT = real audio)");
    }
    record_ui_sync_buttons();
    ESP_LOGI(TAG, "UI record start → family id %u", (unsigned)s_record_target_id);
}

static void on_record_stop(lv_event_t *e)
{
    (void)e;
    if (!s_recording_ui_active) {
        return;
    }
    record_ui_stop();
    ESP_LOGI(TAG, "UI record stop (%lu ms)", (unsigned long)s_recording_elapsed_ms);
}

static void app_tick_cb(lv_timer_t *t)
{
    (void)t;

    if (!lvgl_port_lock(0)) {
        return;
    }

    int pct = hal_battery_percent();
    bool charging = hal_battery_charging();
    if (pct >= 0 && s_lbl_batt) {
        lv_label_set_text_fmt(s_lbl_batt, "%s%d%%", charging ? LV_SYMBOL_CHARGE " " : "", pct);
        if (pct < BATTERY_PCT_LOW_WARN && !charging) {
            lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(0xff6666), 0);
        } else {
            lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(0xffee88), 0);
        }
    }

    if (s_lbl_inbox) {
        lv_label_set_text_fmt(s_lbl_inbox, LV_SYMBOL_LIST " %u", model_inbox_unread_count());
    }

    if (s_recording_ui_active && s_lbl_record_elapsed && lv_screen_active() == s_scr_record) {
        s_recording_elapsed_ms += 200;
        char buf[16];
        format_mmss(buf, sizeof(buf), s_recording_elapsed_ms);
        lv_label_set_text(s_lbl_record_elapsed, buf);
        if (s_recording_elapsed_ms >= RECORD_UI_MAX_MS) {
            ESP_LOGW(TAG, "UI record max duration (%d ms)", RECORD_UI_MAX_MS);
            record_ui_stop();
            if (s_lbl_record_status) {
                lv_label_set_text(s_lbl_record_status, "Max length. BOOT = audio test.");
            }
        }
    }

    lvgl_port_unlock();
}

/** Strip all scroll / gesture propagation flags (theme may re-enable after remove_style_all). */
static void style_no_scroll_chrome(lv_obj_t *o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

/**
 * LVGL sets SCROLL_CHAIN_* on every new object by default. lv_indev_find_scroll_obj() walks up
 * from the touched widget; if any ancestor still has horizontal chain enabled, the flex
 * column (disc) can become the scroll target — so strip-only drag must clear chain on the
 * entire home subtree, not only a few containers.
 */
static void scroll_lock_obj_deep(lv_obj_t *o)
{
    if (!o) {
        return;
    }
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(o, LV_DIR_NONE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLL_WITH_ARROW);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void scroll_lock_tree(lv_obj_t *root)
{
    if (!root) {
        return;
    }
    scroll_lock_obj_deep(root);
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        scroll_lock_tree(lv_obj_get_child(root, i));
    }
}

static void style_disc(lv_obj_t *obj)
{
    /* Drop LVGL default “card” style (light theme = white panel). */
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x1e1a3a), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    /* Square fill — round glass crops corners; do not use LV_RADIUS_CIRCLE here. */
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    style_no_scroll_chrome(obj);
    /* Do not bubble pointer events to the screen (avoids parent “scroll” of whole column). */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void style_round_screen(lv_obj_t *scr)
{
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    /* Same hue as panel so no visible seam if anything peeks past the child. */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1e1a3a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    style_no_scroll_chrome(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void style_label_clear(lv_obj_t *lb)
{
    lv_obj_set_style_bg_opa(lb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lb, 0, 0);
}

/** Plain containers: no theme “card” fill (see lv_theme_default.c → styles.card). */
static void style_panel_transparent(lv_obj_t *o)
{
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    style_no_scroll_chrome(o);
}

static void style_shell_button(lv_obj_t *btn, uint32_t bg_hex)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

/** Round icon-only control (FontAwesome symbols via default font). */
static void style_icon_round_button(lv_obj_t *btn, uint32_t bg_hex)
{
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
}

/** Full-screen flex column helper: children stack top→bottom without overlap. */
static lv_obj_t *disc_column(lv_obj_t *parent)
{
    lv_obj_t *disc = lv_obj_create(parent);
    style_disc(disc);
    lv_obj_set_size(disc, LCD_H_RES, LCD_V_RES);
    lv_obj_align(disc, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_flex_flow(disc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(disc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(disc, UI_PANEL_PAD, 0);
    lv_obj_set_style_pad_right(disc, UI_PANEL_PAD, 0);
    lv_obj_set_style_pad_top(disc, UI_PANEL_PAD, 0);
    lv_obj_set_style_pad_bottom(disc, 10, 0);
    lv_obj_set_style_pad_row(disc, 6, 0);
    return disc;
}

/** Re-apply after lv_screen_load — theme can reattach scroll/gesture flags to the active screen. */
static void apply_home_scroll_lock(void)
{
    if (!s_scr_home) {
        return;
    }
    scroll_lock_tree(s_scr_home);
    if (s_home_disc) {
        lv_obj_clear_flag(s_home_disc, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_scroll_to(s_home_disc, 0, 0, LV_ANIM_OFF);
    }
    if (s_strip_container) {
        lv_obj_clear_flag(s_strip_container, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_scroll_to(s_strip_container, 0, 0, LV_ANIM_OFF);
    }
    lv_obj_scroll_to(s_scr_home, 0, 0, LV_ANIM_OFF);
}

static void on_home_screen_loaded(lv_event_t *e)
{
    (void)e;
    apply_home_scroll_lock();
}

static void build_home(void)
{
    s_scr_home = lv_obj_create(NULL);
    style_round_screen(s_scr_home);
    lv_obj_add_event_cb(s_scr_home, on_home_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    lv_obj_t *disc = disc_column(s_scr_home);

    /* Status row — inset from rim (ui-spec §2.5). */
    lv_obj_t *status = lv_obj_create(disc);
    style_panel_transparent(status);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, HOME_STATUS_H);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_batt = lv_label_create(status);
    lv_label_set_text(s_lbl_batt, "---%");
    style_label_clear(s_lbl_batt);
    lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(0xffee88), 0);

    s_lbl_inbox = lv_label_create(status);
    lv_label_set_text(s_lbl_inbox, LV_SYMBOL_LIST " 0");
    style_label_clear(s_lbl_inbox);
    lv_obj_set_style_text_color(s_lbl_inbox, lv_color_hex(0xaaddff), 0);
    lv_obj_add_flag(s_lbl_inbox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_lbl_inbox, on_inbox_open, LV_EVENT_CLICKED, NULL);

    lv_obj_t *grow_top = lv_obj_create(disc);
    style_panel_transparent(grow_top);
    lv_obj_set_width(grow_top, LV_PCT(100));
    lv_obj_set_flex_grow(grow_top, 0);
    lv_obj_set_height(grow_top, (lv_coord_t)HOME_STRIP_TOP_SPACER);

    s_strip_container = lv_obj_create(disc);
    style_panel_transparent(s_strip_container);
    lv_obj_set_width(s_strip_container, LV_PCT(100));
    lv_obj_set_height(s_strip_container, STRIP_ROW_H);
    lv_obj_set_style_clip_corner(s_strip_container, true, 0);

    for (int i = 0; i < STRIP_NUM_SLOTS; i++) {
        s_strip_bubbles[i] = lv_button_create(s_strip_container);
        lv_obj_remove_style_all(s_strip_bubbles[i]);
        lv_obj_add_flag(s_strip_bubbles[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_strip_bubbles[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_pad_all(s_strip_bubbles[i], 0, 0);
        lv_obj_add_event_cb(s_strip_bubbles[i], on_strip_bubble_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(unsigned)i);

        s_strip_labels[i] = lv_label_create(s_strip_bubbles[i]);
        lv_label_set_text(s_strip_labels[i], "?");
        lv_obj_add_flag(s_strip_labels[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    lv_obj_add_event_cb(s_strip_container, strip_drag_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_strip_container, strip_drag_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_strip_container, strip_drag_cb, LV_EVENT_RELEASED, NULL);
    /* Strip must not bubble drag/scroll to the flex disc (whole “menu” sliding). */
    lv_obj_clear_flag(s_strip_container, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *grow_bot = lv_obj_create(disc);
    style_panel_transparent(grow_bot);
    lv_obj_set_width(grow_bot, LV_PCT(100));
    lv_obj_set_flex_grow(grow_bot, 1);

    s_carousel_idx = 0;
    s_strip_scroll_px = 0;
    strip_refresh();

    s_home_disc = disc;
    /* Theme may attach scroll/gesture flags after children exist — lock again. */
    apply_home_scroll_lock();
}

/**
 * Dedicated zoom screen: standard lv_screen_load flow guarantees it draws above home without
 * relying on sys_layer compositing quirks. Bubble is a fresh object (no reparenting).
 */
static void build_zoom_screen(void)
{
    s_scr_zoom = lv_obj_create(NULL);
    style_round_screen(s_scr_zoom);
    lv_obj_add_event_cb(s_scr_zoom, on_zoom_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    s_zoom_bubble = lv_obj_create(s_scr_zoom);
    lv_obj_remove_style_all(s_zoom_bubble);
    lv_obj_set_size(s_zoom_bubble, ZOOM_BUBBLE_SIZE, ZOOM_BUBBLE_SIZE);
    lv_obj_set_style_radius(s_zoom_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_zoom_bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_zoom_bubble, 16, 0);
    lv_obj_set_style_shadow_ofs_y(s_zoom_bubble, 5, 0);
    lv_obj_set_style_shadow_color(s_zoom_bubble, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_zoom_bubble, LV_OPA_30, 0);
    /* Scale from the bubble's own center so the grow-in looks like a zoom, not a slide. */
    lv_obj_set_style_transform_pivot_x(s_zoom_bubble, ZOOM_BUBBLE_SIZE / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_zoom_bubble, ZOOM_BUBBLE_SIZE / 2, 0);
    lv_obj_set_style_transform_scale(s_zoom_bubble, ZOOM_SCALE_FROM, 0);
    lv_obj_align(s_zoom_bubble, LV_ALIGN_CENTER, 0, -18);
    lv_obj_clear_flag(s_zoom_bubble, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_zoom_bubble, LV_OBJ_FLAG_SCROLLABLE);

    s_zoom_lbl = lv_label_create(s_zoom_bubble);
    lv_label_set_text(s_zoom_lbl, "?");
    style_label_clear(s_zoom_lbl);
    lv_obj_set_style_text_color(s_zoom_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_zoom_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(s_zoom_lbl);

    s_zoom_btn_back = lv_button_create(s_scr_zoom);
    style_icon_round_button(s_zoom_btn_back, 0x2a2840);
    lv_obj_t *zlb = lv_label_create(s_zoom_btn_back);
    lv_label_set_text(zlb, LV_SYMBOL_LEFT);
    style_label_clear(zlb);
    lv_obj_set_style_text_color(zlb, lv_color_hex(0xffffff), 0);
    lv_obj_center(zlb);
    lv_obj_align(s_zoom_btn_back, LV_ALIGN_CENTER, -76, 44);

    s_zoom_btn_play = lv_button_create(s_scr_zoom);
    style_icon_round_button(s_zoom_btn_play, 0x2d6a3d);
    lv_obj_t *zlp = lv_label_create(s_zoom_btn_play);
    lv_label_set_text(zlp, LV_SYMBOL_PLAY);
    style_label_clear(zlp);
    lv_obj_set_style_text_color(zlp, lv_color_hex(0xffffff), 0);
    lv_obj_center(zlp);
    lv_obj_align(s_zoom_btn_play, LV_ALIGN_CENTER, 0, 76);

    s_zoom_btn_stop = lv_button_create(s_scr_zoom);
    style_icon_round_button(s_zoom_btn_stop, 0x6a2d2d);
    lv_obj_t *zls = lv_label_create(s_zoom_btn_stop);
    lv_label_set_text(zls, LV_SYMBOL_STOP);
    style_label_clear(zls);
    lv_obj_set_style_text_color(zls, lv_color_hex(0xffffff), 0);
    lv_obj_center(zls);
    lv_obj_align(s_zoom_btn_stop, LV_ALIGN_CENTER, 76, 44);

    lv_obj_add_event_cb(s_zoom_btn_back, on_zoom_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_zoom_btn_play, on_zoom_play_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_zoom_btn_stop, on_zoom_stop_clicked, LV_EVENT_CLICKED, NULL);
}

static void build_recording(void)
{
    s_scr_record = lv_obj_create(NULL);
    style_round_screen(s_scr_record);

    lv_obj_t *disc = disc_column(s_scr_record);

    lv_obj_t *title_row = lv_label_create(disc);
    s_lbl_record_title = title_row;
    lv_label_set_text(s_lbl_record_title, "To: …");
    lv_label_set_long_mode(s_lbl_record_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_record_title, LV_PCT(100));
    style_label_clear(s_lbl_record_title);
    lv_obj_set_style_text_align(s_lbl_record_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lbl_record_title, lv_color_hex(0xccddee), 0);

    s_lbl_record_elapsed = lv_label_create(disc);
    lv_label_set_text(s_lbl_record_elapsed, "0:00");
    style_label_clear(s_lbl_record_elapsed);
    lv_obj_set_style_text_color(s_lbl_record_elapsed, lv_color_hex(0xffee44), 0);
    lv_obj_set_style_text_align(s_lbl_record_elapsed, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_lbl_record_elapsed, LV_PCT(100));
    lv_obj_set_style_text_letter_space(s_lbl_record_elapsed, 2, 0);

    s_lbl_record_status = lv_label_create(disc);
    lv_label_set_text(s_lbl_record_status, "BOOT = mic/speaker test");
    lv_label_set_long_mode(s_lbl_record_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_record_status, LV_PCT(100));
    style_label_clear(s_lbl_record_status);
    lv_obj_set_style_text_align(s_lbl_record_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lbl_record_status, lv_color_hex(0xb0b8c8), 0);

    lv_obj_t *grow = lv_obj_create(disc);
    style_panel_transparent(grow);
    lv_obj_set_width(grow, LV_PCT(100));
    lv_obj_set_flex_grow(grow, 1);

    lv_obj_t *row = lv_obj_create(disc);
    style_panel_transparent(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    s_btn_record = lv_button_create(row);
    lv_obj_set_flex_grow(s_btn_record, 1);
    lv_obj_set_height(s_btn_record, 48);
    style_shell_button(s_btn_record, 0x3d5a8c);
    lv_obj_t *lr = lv_label_create(s_btn_record);
    lv_label_set_text(lr, "Record");
    style_label_clear(lr);
    lv_obj_center(lr);
    lv_obj_add_event_cb(s_btn_record, on_record_start, LV_EVENT_CLICKED, NULL);

    s_btn_stop = lv_button_create(row);
    lv_obj_set_flex_grow(s_btn_stop, 1);
    lv_obj_set_height(s_btn_stop, 48);
    style_shell_button(s_btn_stop, 0x8c3d4a);
    lv_obj_t *ls = lv_label_create(s_btn_stop);
    lv_label_set_text(ls, "Stop");
    style_label_clear(ls);
    lv_obj_center(ls);
    lv_obj_add_event_cb(s_btn_stop, on_record_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(s_btn_stop, LV_STATE_DISABLED);

    lv_obj_t *back = lv_button_create(disc);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    style_shell_button(back, 0x2a2840);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    style_label_clear(bl);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
}

static void build_inbox(void)
{
    s_scr_inbox = lv_obj_create(NULL);
    style_round_screen(s_scr_inbox);

    lv_obj_t *disc = disc_column(s_scr_inbox);

    lv_obj_t *hdr = lv_label_create(disc);
    lv_label_set_text(hdr, "Inbox");
    style_label_clear(hdr);
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xe8e0ff), 0);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_style_text_align(hdr, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *list = lv_obj_create(disc);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (size_t i = 0; i < model_inbox_count() && i < INBOX_ROWS_MAX; i++) {
        s_inbox_btns[i] = lv_button_create(list);
        lv_obj_set_width(s_inbox_btns[i], LV_PCT(100));
        lv_obj_set_height(s_inbox_btns[i], 46);
        style_shell_button(s_inbox_btns[i], 0x3a3558);
        lv_obj_set_style_radius(s_inbox_btns[i], 10, 0);
        s_inbox_lbls[i] = lv_label_create(s_inbox_btns[i]);
        lv_label_set_long_mode(s_inbox_lbls[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_inbox_lbls[i], LV_PCT(100));
        style_label_clear(s_inbox_lbls[i]);
        lv_obj_set_style_text_color(s_inbox_lbls[i], lv_color_hex(0xf0f0ff), 0);
        lv_obj_add_event_cb(s_inbox_btns[i], on_inbox_row, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    inbox_refresh_rows();

    lv_obj_t *back = lv_button_create(disc);
    lv_obj_set_width(back, LV_PCT(100));
    lv_obj_set_height(back, 44);
    style_shell_button(back, 0x2a2840);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "Back");
    style_label_clear(bl);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, on_inbox_back_clicked, LV_EVENT_CLICKED, NULL);
}

esp_err_t ui_app_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Building UI (%u families)", (unsigned)model_family_count());
    /* Changes every compile — grep binary or serial log to prove this .elf was flashed. */
    ESP_LOGI(TAG, "ui_app build stamp: %s %s (zoom screen, interpolated strip)", __DATE__, __TIME__);

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    (void)disp;
    build_home();
    build_zoom_screen();
    build_recording();
    build_inbox();
    lv_screen_load(s_scr_home);

    lv_timer_create(app_tick_cb, 200, NULL);

    lvgl_port_unlock();

    return ESP_OK;
}
