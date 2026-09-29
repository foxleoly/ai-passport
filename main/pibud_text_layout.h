#pragma once

#include <stdbool.h>
#include <stddef.h>

/* 240x320 layout budget (logical px). */
#define PIBUD_UI_STATUS_Y 0
#define PIBUD_UI_STATUS_H 22
#define PIBUD_UI_MAIN_Y   22
#define PIBUD_UI_MAIN_H   276
#define PIBUD_UI_ACTION_Y 298
#define PIBUD_UI_ACTION_H 22

/* Context-sensitive action-bar hints (H- = long press). */
#define PIBUD_ACTION_LIVE     "UP/DN:scroll  H-UP:view  H-OK:stop"
#define PIBUD_ACTION_STATS    "UP/DN:scroll  H-UP:view  H-OK:stop"
#define PIBUD_ACTION_MENU     "UP/DN:sel  OK:apply  H-UP:view"
#define PIBUD_ACTION_CONFIRM  "OK:yes  DN:no"
#define PIBUD_ACTION_APPROVAL "OK:allow  DN:deny  H-OK:stop"
#define PIBUD_ACTION_PAIRING  "enter code on your mac ..."

typedef unsigned (*pibud_text_measure_fn)(const char *text, size_t length, void *context);

typedef struct {
    unsigned lines;
    bool truncated;
} pibud_text_result_t;

typedef enum {
    PIBUD_OVERLAY_NONE,
    PIBUD_OVERLAY_MENU,
    PIBUD_OVERLAY_APPROVAL,
    PIBUD_OVERLAY_PAIRING,
    PIBUD_OVERLAY_CONFIRMATION,
} pibud_overlay_kind_t;

pibud_text_result_t pibud_text_wrap(const char *input, char *output, size_t output_size,
                                    unsigned max_width, unsigned max_lines,
                                    pibud_text_measure_fn measure, void *context);
pibud_overlay_kind_t pibud_overlay_select(bool confirmation, bool pairing,
                                          bool approval, bool menu);
