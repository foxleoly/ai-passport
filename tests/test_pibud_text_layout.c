#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pibud_text_layout.h"

static unsigned measure_bytes(const char *text, size_t length, void *context)
{
    (void)context;
    (void)text;
    return (unsigned)length; /* 1 unit per char */
}

int main(void)
{
    char out[64];
    pibud_text_result_t r;

    /* Simple wrap at word boundaries. */
    r = pibud_text_wrap("the quick brown fox", out, sizeof(out), 10, 10, measure_bytes, NULL);
    assert(out[0] != '\0');
    assert(strstr(out, "\n") != NULL);
    assert(r.lines >= 2);

    /* Truncation when content exceeds max_lines. */
    r = pibud_text_wrap("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", out, sizeof(out), 10, 1,
                        measure_bytes, NULL);
    assert(r.truncated);
    assert(r.lines == 1);

    /* Overlay priority: confirmation > pairing > approval > menu. */
    assert(pibud_overlay_select(true, true, true, true) == PIBUD_OVERLAY_CONFIRMATION);
    assert(pibud_overlay_select(false, true, true, true) == PIBUD_OVERLAY_PAIRING);
    assert(pibud_overlay_select(false, false, true, true) == PIBUD_OVERLAY_APPROVAL);
    assert(pibud_overlay_select(false, false, false, true) == PIBUD_OVERLAY_MENU);
    assert(pibud_overlay_select(false, false, false, false) == PIBUD_OVERLAY_NONE);

    (void)printf;
    return 0;
}
