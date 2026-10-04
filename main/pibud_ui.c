#include "pibud_ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "pibud_format.h"
#include "pibud_text_layout.h"

/* Palette (pi.dev brand tokens + dark data pages). */
#define C_BG        0x0B0F17
#define C_INK       0xDCE3EC
#define C_DIM       0x66707E
#define C_OK        0x46C46B
#define C_ERR       0xFF5C5C
#define C_INFO      0x5CA8FF
#define C_WARN      0xE7C14E
#define C_PARCH     0xF3F2F0
#define C_PARCH_INK 0x252F3D

/* pi's tri-color, darkened for text on the light HOME page: the raw brand colors
 * (coral #F09082, amber #F1BE58) are far too light to read against parchment.
 * Each of these keeps at least a 4.5:1 contrast ratio on C_PARCH. */
#define C_PARCH_CORAL 0xB04A3C
#define C_PARCH_STEEL 0x2C6A88
#define C_PARCH_AMBER 0x8A6410

#define STATUS_H   22
#define ACTION_H   22
#define RECENT_MAX 6

static lv_obj_t *s_model, *s_batt, *s_wifi, *s_dot, *s_clock, *s_action;
static lv_obj_t *s_home, *s_live, *s_stats, *s_menu;
static lv_obj_t *s_home_name, *s_home_link, *s_home_usage, *s_home_sub;
static lv_obj_t *s_home_30d, *s_home_7d;
/* The progress bar is one rounded track holding one fill per pi brand color, lit
 * in order. */
#define PIBUD_HOME_SEGS  3
#define PIBUD_HOME_BAR_W 180
#define PIBUD_HOME_SEG_W (PIBUD_HOME_BAR_W / PIBUD_HOME_SEGS)
static lv_obj_t *s_home_bar; /* rounded track; clip_corner rounds the square fills */
static lv_obj_t *s_home_fill[PIBUD_HOME_SEGS];
static bool s_home_bar_running;
static lv_obj_t *s_live_focus, *s_live_tokens, *s_live_recent[RECENT_MAX];
static lv_obj_t *s_stat_key[8], *s_stat_val[8];
static lv_obj_t *s_menu_items[PIBUD_SETTING_COUNT];
static lv_obj_t *s_appr, *s_appr_text, *s_pair, *s_pair_code, *s_confirm, *s_confirm_msg;
static char s_recent[RECENT_MAX][48];
static int s_recent_n;

static lv_obj_t *mk_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(C_INK), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    return l;
}

static void put(lv_obj_t *o, int x, int y, int w, int h, const lv_font_t *font)
{
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    if (font != NULL) {
        lv_obj_set_style_text_font(o, font, 0);
    }
}

static lv_obj_t *mk_view(lv_obj_t *root, uint32_t bg)
{
    lv_obj_t *v = lv_obj_create(root);
    lv_obj_set_size(v, 240, 320 - STATUS_H - ACTION_H);
    lv_obj_set_pos(v, 0, STATUS_H);
    lv_obj_set_style_bg_color(v, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(v, 0, 0);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    return v;
}

/* The pi.dev logo (https://pi.dev/logo-auto.svg) is a tri-color mosaic whose
 * every edge is axis-aligned, so it redraws exactly as five rectangles on a 4x4
 * cell grid instead of shipping a bitmap. */
typedef struct {
    uint8_t x, y, w, h; /* grid cells */
    uint32_t color;
} pibud_logo_rect_t;

static const pibud_logo_rect_t PIBUD_LOGO[] = {
    { 0, 0, 3, 1, 0xF09082 }, /* coral */
    { 2, 1, 1, 1, 0xF09082 },
    { 0, 1, 1, 3, 0x4D9ABF }, /* steel */
    { 1, 2, 1, 1, 0x4D9ABF },
    { 3, 2, 1, 2, 0xF1BE58 }, /* amber */
};

// Draws the mosaic at (x, y); `cell` is the grid pitch, so the mark is 4*cell px.
static void pibud_logo_create(lv_obj_t *parent, int x, int y, int cell)
{
    for (size_t i = 0; i < sizeof(PIBUD_LOGO) / sizeof(PIBUD_LOGO[0]); i++) {
        const pibud_logo_rect_t *r = &PIBUD_LOGO[i];
        lv_obj_t *part = lv_obj_create(parent);
        lv_obj_set_pos(part, x + r->x * cell, y + r->y * cell);
        lv_obj_set_size(part, r->w * cell, r->h * cell);
        lv_obj_set_style_radius(part, 0, 0);
        lv_obj_set_style_border_width(part, 0, 0);
        lv_obj_set_style_bg_color(part, lv_color_hex(r->color), 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void pibud_ui_init_views(lv_obj_t *root)
{
    s_home = mk_view(root, C_PARCH);
    pibud_logo_create(s_home, 12, 16, 10); // 40px, beside the name
    s_home_name = mk_label(s_home, "Pi");
    put(s_home_name, 64, 26, 28, 20, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(s_home_name, lv_color_hex(C_PARCH_INK), 0);
    // The agent line is an indeterminate progress bar: three fills, one per pi
    // brand color, lit left to right while pi works and empty when it is not. One
    // rounded track holds them and clip_corner rounds only the two outer ends, so
    // the colour dividers stay straight. Colors are the darkened variants — the raw
    // brand coral/amber are far too light to read as fills on parchment.
    static const uint32_t seg_color[PIBUD_HOME_SEGS] = {
        C_PARCH_CORAL, C_PARCH_STEEL, C_PARCH_AMBER,
    };
    // Align instead of set_pos: the view carries theme padding and set_pos is
    // relative to the content area, which shoves a hand-placed group right.
    s_home_bar = lv_obj_create(s_home);
    lv_obj_set_size(s_home_bar, PIBUD_HOME_BAR_W, 12);
    lv_obj_align(s_home_bar, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_set_style_radius(s_home_bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_home_bar, lv_color_hex(C_PARCH_INK), 0);
    lv_obj_set_style_bg_opa(s_home_bar, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_home_bar, 0, 0);
    lv_obj_set_style_pad_all(s_home_bar, 0, 0);
    lv_obj_set_style_clip_corner(s_home_bar, true, 0);
    lv_obj_clear_flag(s_home_bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < PIBUD_HOME_SEGS; i++) {
        lv_obj_t *fill = lv_obj_create(s_home_bar);
        lv_obj_set_pos(fill, i * PIBUD_HOME_SEG_W, 0);
        lv_obj_set_size(fill, 0, 12);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(seg_color[i]), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        s_home_fill[i] = fill;
    }
    // 30-day running total, right-aligned, on the logo's line.
    s_home_30d = mk_label(s_home, "30d: 0 tok");
    put(s_home_30d, 94, 28, 118, 16, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_30d, lv_color_hex(C_PARCH_AMBER), 0);
    lv_obj_set_style_text_align(s_home_30d, LV_TEXT_ALIGN_RIGHT, 0);
    s_home_link = mk_label(s_home, "link offline");
    put(s_home_link, 12, 152, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_link, lv_color_hex(C_PARCH_INK), 0);
    s_home_usage = mk_label(s_home, "1d: 0 tok");
    put(s_home_usage, 12, 182, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_usage, lv_color_hex(C_PARCH_CORAL), 0);
    s_home_7d = mk_label(s_home, "7d: 0 tok");
    put(s_home_7d, 12, 212, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_7d, lv_color_hex(C_PARCH_STEEL), 0);
    s_home_sub = mk_label(s_home, "0 subagents");
    put(s_home_sub, 12, 242, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_sub, lv_color_hex(C_PARCH_INK), 0);

    s_live = mk_view(root, C_BG);
    pibud_logo_create(s_live, 204, 6, 6); // 24px brand mark, top-right
    s_live_focus = mk_label(s_live, "idle");
    put(s_live_focus, 8, 8, 190, 20, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_live_focus, lv_color_hex(C_OK), 0);
    s_live_tokens = mk_label(s_live, "tok 0");
    put(s_live_tokens, 8, 32, 224, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_live_tokens, lv_color_hex(C_DIM), 0);
    for (int i = 0; i < RECENT_MAX; i++) {
        s_live_recent[i] = mk_label(s_live, "");
        put(s_live_recent[i], 8, 54 + i * 15, 224, 14, &lv_font_montserrat_14);
    }

    s_stats = mk_view(root, C_BG);
    pibud_logo_create(s_stats, 204, 6, 6);
    for (int i = 0; i < 8; i++) {
        s_stat_key[i] = mk_label(s_stats, "");
        put(s_stat_key[i], 10, 10 + i * 30, 100, 14, &lv_font_montserrat_14);
        lv_obj_set_style_text_color(s_stat_key[i], lv_color_hex(C_DIM), 0);
        s_stat_val[i] = mk_label(s_stats, "");
        put(s_stat_val[i], 10, 24 + i * 30, 210, 16, &lv_font_montserrat_14);
    }

    s_menu = mk_view(root, C_BG);
    pibud_logo_create(s_menu, 204, 6, 6);
    for (int i = 0; i < PIBUD_SETTING_COUNT; i++) {
        s_menu_items[i] = mk_label(s_menu, "");
        put(s_menu_items[i], 10, 10 + i * 24, 220, 18, &lv_font_montserrat_14);
    }

    s_appr = mk_view(root, C_BG);
    lv_obj_set_style_bg_opa(s_appr, LV_OPA_90, 0);
    s_appr_text = mk_label(s_appr, "WAITING");
    put(s_appr_text, 12, 120, 216, 40, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_appr_text, lv_color_hex(C_WARN), 0);

    s_pair = mk_view(root, C_PARCH);
    lv_obj_set_style_bg_opa(s_pair, LV_OPA_COVER, 0);
    s_pair_code = mk_label(s_pair, "000000");
    put(s_pair_code, 12, 130, 216, 60, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(s_pair_code, lv_color_hex(C_PARCH_INK), 0);

    s_confirm = mk_view(root, C_BG);
    lv_obj_set_style_bg_opa(s_confirm, LV_OPA_90, 0);
    s_confirm_msg = mk_label(s_confirm, "confirm?");
    put(s_confirm_msg, 20, 140, 200, 40, &lv_font_montserrat_20);
}

void pibud_ui_init(void)
{
    lv_obj_t *root = lv_scr_act();
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_model = mk_label(root, "-");
    put(s_model, 6, 4, 90, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_model, lv_color_hex(C_DIM), 0);
    // Local clock, refreshed by the sidecar's time line (the device has no RTC).
    s_clock = mk_label(root, "--:--");
    put(s_clock, 100, 4, 48, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(C_DIM), 0);
    // ...and this dot carries running/idle on the pages without a progress bar.
    s_dot = lv_obj_create(root);
    lv_obj_set_size(s_dot, 8, 8);
    lv_obj_set_pos(s_dot, 148, 7);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dot, 0, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(C_DIM), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
    // WiFi mark: green while the link is up, dim when it is not. The explicit
    // agent-state text is gone — the HOME progress bar shows "running", and this
    // icon covers the link, which is what the old "idle" text conveyed.
    s_wifi = mk_label(root, LV_SYMBOL_WIFI);
    put(s_wifi, 164, 4, 16, 14, &lv_font_montserrat_14);
    s_batt = mk_label(root, "--%");
    put(s_batt, 188, 4, 46, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_batt, lv_color_hex(C_DIM), 0);
    s_action = mk_label(root, "");
    put(s_action, 6, 304, 228, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_action, lv_color_hex(C_DIM), 0);

    pibud_ui_init_views(root);
}

static void push_recent(const char *line)
{
    if (s_recent_n > 0 && strcmp(s_recent[s_recent_n - 1], line) == 0) {
        return;
    }
    if (s_recent_n < RECENT_MAX) {
        memcpy(s_recent[s_recent_n], line, 47);
        s_recent[s_recent_n][47] = '\0';
        s_recent_n++;
    } else {
        for (int i = 0; i < RECENT_MAX - 1; i++) {
            memcpy(s_recent[i], s_recent[i + 1], 48);
        }
        memcpy(s_recent[RECENT_MAX - 1], line, 47);
        s_recent[RECENT_MAX - 1][47] = '\0';
    }
}

static uint32_t state_color(const pibud_ui_snapshot_t *s)
{
    if (s->hb.has_result) return s->hb.result_ok ? C_OK : C_ERR;
    if (strcmp(s->hb.state, "running") == 0) return C_INFO;
    if (s->connection == PIBUD_CONNECTION_CONNECTED) return C_OK;
    return C_DIM;
}

// The status-bar activity dot: green only while the agent is actually working.
// It carries the running/idle signal on the pages that have no progress bar.
static uint32_t activity_color(const pibud_ui_snapshot_t *s)
{
    if (s->heartbeat_stale || s->connection != PIBUD_CONNECTION_CONNECTED) {
        return C_DIM;
    }
    return strcmp(s->hb.state, "running") == 0 ? C_OK : C_DIM;
}

// One 0..3*100 sweep drives every segment, so the fill crosses the brand colors
// in order instead of filling all three at once.
static void home_bar_anim_cb(void *var, int32_t value)
{
    (void)var;
    for (int i = 0; i < PIBUD_HOME_SEGS; i++) {
        int32_t v = value - i * 100;
        if (v < 0) {
            v = 0;
        } else if (v > 100) {
            v = 100;
        }
        lv_obj_set_width(s_home_fill[i], (v * PIBUD_HOME_SEG_W) / 100);
    }
}

// Runs the sweep only while the agent is working. render() is called for every
// state change, so the flag keeps a steady state from restarting the animation.
static void home_bar_set_running(bool running)
{
    if (running == s_home_bar_running) {
        return;
    }
    s_home_bar_running = running;
    if (!running) {
        lv_anim_delete(s_home_bar, home_bar_anim_cb);
        home_bar_anim_cb(NULL, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_home_bar);
    lv_anim_set_values(&a, 0, PIBUD_HOME_SEGS * 100);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_reverse_duration(&a, 900); // sweep back, so it goes both ways
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, home_bar_anim_cb);
    lv_anim_start(&a);
}

void pibud_ui_render(const pibud_ui_snapshot_t *s)
{
    char buf[128];
    char tok[PIBUD_FORMAT_TOKENS_MAX];
    char clock[PIBUD_FORMAT_CLOCK_MAX];

    lv_label_set_text(s_model, s->hb.model[0] ? s->hb.model : "-");
    pibud_format_clock(clock, sizeof(clock), s->epoch_seconds, s->tz_offset_seconds);
    lv_label_set_text(s_clock, clock);
    lv_obj_set_style_text_color(s_wifi, lv_color_hex(
        s->connection == PIBUD_CONNECTION_CONNECTED ? C_OK : C_DIM), 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(activity_color(s)), 0);
    {
        // Icon + percent, tinted before it becomes a problem; "--%" when the
        // board reports no battery rather than a misleading 0%.
        uint32_t batt_color = C_DIM;
        if (!s->battery_available) {
            lv_label_set_text(s_batt, "--%");
        } else {
            const char *icon = LV_SYMBOL_BATTERY_EMPTY;
            if (s->battery_percent >= 75) icon = LV_SYMBOL_BATTERY_FULL;
            else if (s->battery_percent >= 50) icon = LV_SYMBOL_BATTERY_3;
            else if (s->battery_percent >= 25) icon = LV_SYMBOL_BATTERY_2;
            else if (s->battery_percent >= 10) icon = LV_SYMBOL_BATTERY_1;
            batt_color = s->battery_percent < 10 ? C_ERR
                         : s->battery_percent < 25 ? C_WARN : C_DIM;
            lv_label_set_text_fmt(s_batt, "%s %u%%", icon, s->battery_percent);
        }
        lv_obj_set_style_text_color(s_batt, lv_color_hex(batt_color), 0);
    }

    if (s->hb.tool[0]) {
        snprintf(buf, sizeof(buf), "%s %s", s->hb.tool, s->hb.arg[0] ? s->hb.arg : "");
        push_recent(buf);
    }

    lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_live, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_stats, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);

    switch (s->view) {
    case PIBUD_VIEW_HOME:
        lv_label_set_text(s_home_name, s->name[0] ? s->name : "Pi");
        home_bar_set_running(!s->screen_off && strcmp(s->hb.state, "running") == 0);
        lv_label_set_text(s_home_link, s->connection == PIBUD_CONNECTION_CONNECTED
                                       ? "link on" : "link off");
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens);
        lv_label_set_text_fmt(s_home_usage, "1d: %s tok", tok);
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens_7d);
        lv_label_set_text_fmt(s_home_7d, "7d: %s tok", tok);
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens_30d);
        lv_label_set_text_fmt(s_home_30d, "30d: %s tok", tok);
        if (s->hb.sub_available) {
            lv_label_set_text_fmt(s_home_sub, "%u subagents (%u work)",
                                  s->hb.sub_total, s->hb.sub_working);
        } else {
            /* Unknown, not zero: an empty line beats a misleading "0 subagents". */
            lv_label_set_text(s_home_sub, "");
        }
        lv_obj_clear_flag(s_home, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_LIVE:
        lv_label_set_text_fmt(s_live_focus, "%s %s",
                              s->hb.tool[0] ? s->hb.tool : "idle",
                              s->hb.arg[0] ? s->hb.arg : "");
        lv_obj_set_style_text_color(s_live_focus, lv_color_hex(state_color(s)), 0);
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens);
        lv_label_set_text_fmt(s_live_tokens, "%s tok · $%.2f", tok, s->hb.cost);
        for (int i = 0; i < RECENT_MAX; i++) {
            lv_label_set_text(s_live_recent[i], i < s_recent_n ? s_recent[i] : "");
        }
        lv_obj_clear_flag(s_live, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_STATS:
        lv_label_set_text(s_stat_key[0], "total");
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens);
        lv_label_set_text(s_stat_val[0], tok);
        lv_label_set_text(s_stat_key[1], "input");
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens_in);
        lv_label_set_text_fmt(s_stat_val[1], "%s · %u%%", tok,
                              pibud_percent(s->hb.tokens_in, s->hb.tokens));
        lv_label_set_text(s_stat_key[2], "output");
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens_out);
        lv_label_set_text_fmt(s_stat_val[2], "%s · %u%%", tok,
                              pibud_percent(s->hb.tokens_out, s->hb.tokens));
        lv_label_set_text(s_stat_key[3], "cache");
        pibud_format_tokens(tok, sizeof(tok), s->hb.tokens_cache);
        lv_label_set_text_fmt(s_stat_val[3], "%s · %u%%", tok,
                              pibud_percent(s->hb.tokens_cache, s->hb.tokens));
        lv_label_set_text(s_stat_key[4], "cost");
        lv_label_set_text_fmt(s_stat_val[4], "$%.2f", s->hb.cost);
        lv_label_set_text(s_stat_key[5], "subs");
        if (s->hb.sub_available) {
            lv_label_set_text_fmt(s_stat_val[5], "%u/%u", s->hb.sub_total, s->hb.sub_working);
        } else {
            lv_label_set_text(s_stat_val[5], "--");
        }
        lv_label_set_text(s_stat_key[6], "tool");
        lv_label_set_text(s_stat_val[6], s->hb.tool[0] ? s->hb.tool : "-");
        lv_label_set_text(s_stat_key[7], "branch");
        lv_label_set_text(s_stat_val[7], s->hb.title[0] ? s->hb.title : "-");
        lv_obj_clear_flag(s_stats, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_MENU:
    default: {
        static const char *names[PIBUD_SETTING_COUNT] = {"brightness", "factory reset"};
        for (int i = 0; i < PIBUD_SETTING_COUNT; i++) {
            const bool sel = (i == s->settings_sel);
            if (i == PIBUD_SETTING_BRIGHTNESS) {
                // Show where the value currently sits: pressing OK with no visible
                // value was the main gap in this menu.
                lv_label_set_text_fmt(s_menu_items[i], "%s%s  %u%%",
                                      sel ? "> " : "", names[i],
                                      PIBUD_BRIGHTNESS_PERCENT(s->brightness_level));
            } else {
                lv_label_set_text_fmt(s_menu_items[i], "%s%s",
                                      sel ? "> " : "", names[i]);
            }
            lv_obj_set_style_text_color(s_menu_items[i],
                                        lv_color_hex(sel ? C_OK : C_INK), 0);
        }
        lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
        break;
    }
    }

    lv_obj_add_flag(s_appr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    pibud_overlay_kind_t ov = pibud_overlay_select(s->confirmation_pending,
                                                   s->pairing_visible,
                                                   s->approval_visible, false);
    if (ov == PIBUD_OVERLAY_CONFIRMATION) {
        lv_label_set_text(s_confirm_msg, s->confirmation == PIBUD_CONFIRM_UNPAIR
                                            ? "unpair?" : "reset?");
        lv_obj_clear_flag(s_confirm, LV_OBJ_FLAG_HIDDEN);
    } else if (ov == PIBUD_OVERLAY_PAIRING) {
        char code[8];
        snprintf(code, sizeof(code), "%06u", (unsigned)s->passkey);
        lv_label_set_text(s_pair_code, code);
        lv_obj_clear_flag(s_pair, LV_OBJ_FLAG_HIDDEN);
    } else if (ov == PIBUD_OVERLAY_APPROVAL) {
        lv_label_set_text_fmt(s_appr_text, "waiting: %s",
                              s->prompt.text[0] ? s->prompt.text : "input");
        lv_obj_clear_flag(s_appr, LV_OBJ_FLAG_HIDDEN);
    }

    const char *hint;
    if (ov == PIBUD_OVERLAY_APPROVAL) hint = PIBUD_ACTION_APPROVAL;
    else if (ov == PIBUD_OVERLAY_PAIRING) hint = PIBUD_ACTION_PAIRING;
    else if (ov == PIBUD_OVERLAY_CONFIRMATION) hint = PIBUD_ACTION_CONFIRM;
    else if (s->view == PIBUD_VIEW_MENU) hint = PIBUD_ACTION_MENU;
    else if (s->view == PIBUD_VIEW_HOME) hint = "H-UP:view  H-OK:stop";
    else hint = PIBUD_ACTION_LIVE;

    // A pending or just-finished act replaces the hint for a moment, so pressing
    // OK / hold-OK is visibly acknowledged (the state drops it after ~2.5s).
    uint32_t hint_color = C_DIM;
    if (s->delivery == PIBUD_DELIVERY_SENDING) {
        hint = "sending...";
    } else if (s->delivery == PIBUD_DELIVERY_SENT) {
        hint = "sent to your mac";
        hint_color = C_OK;
    } else if (s->delivery == PIBUD_DELIVERY_FAILED) {
        hint = "not sent - check the link";
        hint_color = C_ERR;
    }
    lv_label_set_text(s_action, hint);
    lv_obj_set_style_text_color(s_action, lv_color_hex(hint_color), 0);
}
