#pragma once

#include "pibud_types.h"

/* All entry points require the caller to hold bsp_lvgl_lock(). */
void pibud_ui_init(void);
void pibud_ui_render(const pibud_ui_snapshot_t *snapshot);
