#include "pibud_i4.h"

#include <stdlib.h>

void pibud_i4_set_pixel(uint8_t *pixels, uint16_t width, uint16_t x, uint16_t y,
                       uint8_t color_index)
{
    uint32_t offset = (uint32_t)y * ((width + 1U) / 2U) + x / 2U;
    uint8_t shift = (x & 1U) ? 0U : 4U;
    pixels[offset] = (uint8_t)((pixels[offset] & ~(0x0fU << shift)) |
                               ((color_index & 0x0fU) << shift));
}

uint8_t pibud_i4_get_pixel(const uint8_t *pixels, uint16_t width, uint16_t x, uint16_t y)
{
    uint32_t offset = (uint32_t)y * ((width + 1U) / 2U) + x / 2U;
    uint8_t shift = (x & 1U) ? 0U : 4U;
    return (uint8_t)((pixels[offset] >> shift) & 0x0fU);
}

void pibud_i4_surface_init(pibud_i4_surface_t *surface, uint8_t *pixels,
                           uint16_t width, uint16_t height, uint16_t stride)
{
    if (surface == NULL) return;
    surface->pixels = pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = stride != 0 ? stride : (uint16_t)((width + 1U) / 2U);
}

static int pibud_max(int a, int b) { return a > b ? a : b; }
static int pibud_min(int a, int b) { return a < b ? a : b; }

static int pibud_clipped(const pibud_i4_surface_t *surface, const pibud_i4_clip_t *clip,
                         int x, int y)
{
    if (surface == NULL || surface->pixels == NULL || x < 0 || y < 0 ||
        x >= surface->width || y >= surface->height) {
        return 0;
    }
    return clip == NULL || (x >= clip->x && y >= clip->y &&
                            x < clip->x + clip->w && y < clip->y + clip->h);
}

static void pibud_surface_pixel(pibud_i4_surface_t *surface, const pibud_i4_clip_t *clip,
                                int x, int y, uint8_t color_index)
{
    uint32_t offset;
    uint8_t shift;
    if (!pibud_clipped(surface, clip, x, y)) return;
    offset = (uint32_t)y * surface->stride + (unsigned)x / 2U;
    shift = (x & 1) ? 0U : 4U;
    surface->pixels[offset] =
        (uint8_t)((surface->pixels[offset] & ~(0x0fU << shift)) |
                  ((color_index & 0x0fU) << shift));
}

void pibud_i4_fill_rect(pibud_i4_surface_t *surface, const pibud_i4_clip_t *clip,
                        int x, int y, int width, int height, uint8_t color_index)
{
    int left, top, right, bottom, px, py;
    if (surface == NULL || width <= 0 || height <= 0) return;
    left = pibud_max(x, 0);
    top = pibud_max(y, 0);
    right = pibud_min(x + width, surface->width);
    bottom = pibud_min(y + height, surface->height);
    if (clip != NULL) {
        left = pibud_max(left, clip->x);
        top = pibud_max(top, clip->y);
        right = pibud_min(right, clip->x + clip->w);
        bottom = pibud_min(bottom, clip->y + clip->h);
    }
    for (py = top; py < bottom; ++py)
        for (px = left; px < right; ++px)
            pibud_surface_pixel(surface, NULL, px, py, color_index);
}

void pibud_i4_line(pibud_i4_surface_t *surface, const pibud_i4_clip_t *clip,
                   int x1, int y1, int x2, int y2, uint8_t color_index)
{
    int dx = abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        int twice;
        pibud_surface_pixel(surface, clip, x1, y1, color_index);
        if (x1 == x2 && y1 == y2) break;
        twice = error * 2;
        if (twice >= dy) { error += dy; x1 += sx; }
        if (twice <= dx) { error += dx; y1 += sy; }
    }
}
