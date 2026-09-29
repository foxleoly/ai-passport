#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_i4.h"

int main(void)
{
    enum { W = 16, H = 16 };
    uint8_t pixels[W * H / 2 + 1];
    pibud_i4_surface_t surface;
    pibud_i4_clip_t clip;

    memset(pixels, 0, sizeof(pixels));
    pibud_i4_surface_init(&surface, pixels, W, H, 0);

    /* Pixel round-trip across even/odd columns. */
    pibud_i4_set_pixel(pixels, W, 0, 0, 0xA);
    pibud_i4_set_pixel(pixels, W, 1, 0, 0x5);
    assert(pibud_i4_get_pixel(pixels, W, 0, 0) == 0xA);
    assert(pibud_i4_get_pixel(pixels, W, 1, 0) == 0x5);
    assert(pibud_i4_get_pixel(pixels, W, 2, 0) == 0x0);

    /* Fill rect respects clip. */
    memset(pixels, 0, sizeof(pixels));
    pibud_i4_fill_rect(&surface, NULL, 0, 0, W, H, 0x1);
    assert(pibud_i4_get_pixel(pixels, W, 0, 0) == 0x1);
    assert(pibud_i4_get_pixel(pixels, W, 15, 15) == 0x1);

    memset(pixels, 0, sizeof(pixels));
    clip = (pibud_i4_clip_t){.x = 4, .y = 4, .w = 8, .h = 8};
    pibud_i4_fill_rect(&surface, &clip, 0, 0, W, H, 0x2);
    assert(pibud_i4_get_pixel(pixels, W, 0, 0) == 0x0);  /* outside clip */
    assert(pibud_i4_get_pixel(pixels, W, 7, 7) == 0x2);  /* inside clip */

    /* Diagonal line sets endpoints. */
    memset(pixels, 0, sizeof(pixels));
    pibud_i4_line(&surface, NULL, 2, 2, 9, 9, 0xF);
    assert(pibud_i4_get_pixel(pixels, W, 2, 2) == 0xF);
    assert(pibud_i4_get_pixel(pixels, W, 9, 9) == 0xF);

    return 0;
}
