#include "pibud_ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
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

#define STATUS_H   22
#define ACTION_H   22
#define RECENT_MAX 6

static lv_obj_t *s_model, *s_state, *s_batt, *s_action;
static lv_obj_t *s_home, *s_live, *s_stats, *s_menu;
static lv_obj_t *s_home_name, *s_home_agent, *s_home_link, *s_home_usage, *s_home_sub;
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

static void pibud_ui_init_views(lv_obj_t *root)
{
    s_home = mk_view(root, C_PARCH);
    s_home_name = mk_label(s_home, "Pi");
    put(s_home_name, 70, 20, 150, 20, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(s_home_name, lv_color_hex(C_PARCH_INK), 0);
    s_home_agent = mk_label(s_home, "agent idle");
    put(s_home_agent, 12, 96, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_agent, lv_color_hex(C_PARCH_INK), 0);
    s_home_link = mk_label(s_home, "link offline");
    put(s_home_link, 12, 144, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_link, lv_color_hex(C_PARCH_INK), 0);
    s_home_usage = mk_label(s_home, "0 tok");
    put(s_home_usage, 12, 176, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_usage, lv_color_hex(C_PARCH_INK), 0);
    s_home_sub = mk_label(s_home, "0 subagents");
    put(s_home_sub, 12, 208, 216, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_home_sub, lv_color_hex(C_PARCH_INK), 0);

    s_live = mk_view(root, C_BG);
    s_live_focus = mk_label(s_live, "idle");
    put(s_live_focus, 8, 8, 224, 20, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_live_focus, lv_color_hex(C_OK), 0);
    s_live_tokens = mk_label(s_live, "tok 0");
    put(s_live_tokens, 8, 32, 224, 14, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(s_live_tokens, lv_color_hex(C_DIM), 0);
    for (int i = 0; i < RECENT_MAX; i++) {
        s_live_recent[i] = mk_label(s_live, "");
        put(s_live_recent[i], 8, 54 + i * 15, 224, 14, &lv_font_montserrat_14);
    }

    s_stats = mk_view(root, C_BG);
    for (int i = 0; i < 8; i++) {
        s_stat_key[i] = mk_label(s_stats, "");
        put(s_stat_key[i], 10, 10 + i * 30, 100, 14, &lv_font_montserrat_14);
        lv_obj_set_style_text_color(s_stat_key[i], lv_color_hex(C_DIM), 0);
        s_stat_val[i] = mk_label(s_stats, "");
        put(s_stat_val[i], 10, 24 + i * 30, 210, 16, &lv_font_montserrat_14);
    }

    s_menu = mk_view(root, C_BG);
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
    s_state = mk_label(root, "idle");
    put(s_state, 150, 4, 50, 14, &lv_font_montserrat_14);
    s_batt = mk_label(root, "-");
    put(s_batt, 200, 4, 34, 14, &lv_font_montserrat_14);
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
    if (s->ble_connected) return C_OK;
    return C_DIM;
}

void pibud_ui_render(const pibud_ui_snapshot_t *s)
{
    char buf[128];

    lv_label_set_text(s_model, s->hb.model[0] ? s->hb.model : "-");
    lv_label_set_text(s_state, s->hb.state[0] ? s->hb.state : "idle");
    lv_obj_set_style_text_color(s_state, lv_color_hex(state_color(s)), 0);
    lv_label_set_text_fmt(s_batt, "%u%%", s->battery_percent);

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
        lv_label_set_text_fmt(s_home_agent, "agent %s",
                              s->hb.state[0] ? s->hb.state : "offline");
        lv_label_set_text_fmt(s_home_link, "link %s%s",
                              s->ble_connected ? "on" : "off",
                              s->ble_encrypted ? " (enc)" : "");
        lv_label_set_text_fmt(s_home_usage, "%" PRIu64 " tok", s->hb.tokens);
        lv_label_set_text_fmt(s_home_sub, "%u subagents (%u work)",
                              s->hb.sub_total, s->hb.sub_working);
        lv_obj_clear_flag(s_home, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_LIVE:
        lv_label_set_text_fmt(s_live_focus, "%s %s",
                              s->hb.tool[0] ? s->hb.tool : "idle",
                              s->hb.arg[0] ? s->hb.arg : "");
        lv_obj_set_style_text_color(s_live_focus, lv_color_hex(state_color(s)), 0);
        lv_label_set_text_fmt(s_live_tokens, "%" PRIu64 " tok · $%.2f",
                              s->hb.tokens, s->hb.cost);
        for (int i = 0; i < RECENT_MAX; i++) {
            lv_label_set_text(s_live_recent[i], i < s_recent_n ? s_recent[i] : "");
        }
        lv_obj_clear_flag(s_live, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_STATS:
        lv_label_set_text(s_stat_key[0], "model");
        lv_label_set_text(s_stat_val[0], s->hb.model[0] ? s->hb.model : "-");
        lv_label_set_text(s_stat_key[1], "state");
        lv_label_set_text(s_stat_val[1], s->hb.state[0] ? s->hb.state : "offline");
        lv_label_set_text(s_stat_key[2], "tokens");
        lv_label_set_text_fmt(s_stat_val[2], "%" PRIu64, s->hb.tokens);
        lv_label_set_text(s_stat_key[3], "cost");
        lv_label_set_text_fmt(s_stat_val[3], "$%.2f", s->hb.cost);
        lv_label_set_text(s_stat_key[4], "subs");
        lv_label_set_text_fmt(s_stat_val[4], "%u/%u", s->hb.sub_total, s->hb.sub_working);
        lv_label_set_text(s_stat_key[5], "tool");
        lv_label_set_text(s_stat_val[5], s->hb.tool[0] ? s->hb.tool : "-");
        lv_label_set_text(s_stat_key[6], "branch");
        lv_label_set_text(s_stat_val[6], s->hb.title[0] ? s->hb.title : "-");
        lv_label_set_text(s_stat_key[7], "stale?");
        lv_label_set_text(s_stat_val[7], s->heartbeat_stale ? "yes" : "no");
        lv_obj_clear_flag(s_stats, LV_OBJ_FLAG_HIDDEN);
        break;
    case PIBUD_VIEW_MENU:
    default: {
        static const char *names[PIBUD_SETTING_COUNT] = {"brightness", "sound", "BLE",
                                                          "transcript", "unpair",
                                                          "factory reset"};
        for (int i = 0; i < PIBUD_SETTING_COUNT; i++) {
            if (i == s->settings_sel) {
                lv_label_set_text_fmt(s_menu_items[i], "> %s", names[i]);
                lv_obj_set_style_text_color(s_menu_items[i], lv_color_hex(C_OK), 0);
            } else {
                lv_label_set_text(s_menu_items[i], names[i]);
                lv_obj_set_style_text_color(s_menu_items[i], lv_color_hex(C_INK), 0);
            }
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
    lv_label_set_text(s_action, hint);
}
