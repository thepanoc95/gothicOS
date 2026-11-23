/*
 * Linear framebuffer drawing core for the native Linux port.
 *
 * Everything here is a pure function of the surface it is handed: no globals,
 * no syscalls, no InterfaceKit. That keeps the console and the widget engine
 * linkable into the standalone userspace ELF and testable on the host.
 */
#include <stddef.h>
#include <stdint.h>

#include "nullk_linux/fb_font.h"
#include "nullk_linux/fb_gfx.h"

/* ------------------------------------------------------------------ */
/* Pixel format helpers.                                               */
/* ------------------------------------------------------------------ */

static uint8_t channel_down(uint32_t value, uint8_t bits)
{
    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 8u) {
        return (uint8_t)value;
    }
    return (uint8_t)(value >> (8u - bits));
}

static uint8_t channel_up(uint32_t value, uint8_t bits)
{
    uint32_t max;

    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 8u) {
        return (uint8_t)value;
    }
    max = (1u << bits) - 1u;
    return (uint8_t)((value * 255u + max / 2u) / max);
}

static uint32_t channel_mask(uint8_t bits)
{
    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 32u) {
        return 0xffffffffu;
    }
    return (1u << bits) - 1u;
}

uint32_t nkfb_pack(const nkfb_surface_t *s, uint32_t rgba)
{
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (s == NULL || s->bpp < 16u) {
        return 0u;
    }
    r = channel_down((rgba >> 16) & 0xffu, s->red_size);
    g = channel_down((rgba >> 8) & 0xffu, s->green_size);
    b = channel_down(rgba & 0xffu, s->blue_size);
    return (r << s->red_shift) | (g << s->green_shift) |
           (b << s->blue_shift);
}

void nkfb_surface_init(nkfb_surface_t *s, void *base, uint32_t width,
                       uint32_t height, uint32_t pitch, uint8_t bpp,
                       uint8_t red_size, uint8_t red_shift, uint8_t green_size,
                       uint8_t green_shift, uint8_t blue_size,
                       uint8_t blue_shift)
{
    if (s == NULL) {
        return;
    }
    s->pixels = (uint8_t *)base;
    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->bpp = bpp;
    s->red_size = red_size;
    s->red_shift = red_shift;
    s->green_size = green_size;
    s->green_shift = green_shift;
    s->blue_size = blue_size;
    s->blue_shift = blue_shift;
    nkfb_clip_reset(s);
}

void nkfb_clip_reset(nkfb_surface_t *s)
{
    if (s == NULL) {
        return;
    }
    s->clip_x = 0;
    s->clip_y = 0;
    s->clip_w = (int32_t)s->width;
    s->clip_h = (int32_t)s->height;
}

void nkfb_clip_set(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                   uint32_t h)
{
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + (int32_t)w;
    int32_t y1 = y + (int32_t)h;

    if (s == NULL) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int32_t)s->width) {
        x1 = (int32_t)s->width;
    }
    if (y1 > (int32_t)s->height) {
        y1 = (int32_t)s->height;
    }
    if (x1 < x0) {
        x1 = x0;
    }
    if (y1 < y0) {
        y1 = y0;
    }
    s->clip_x = x0;
    s->clip_y = y0;
    s->clip_w = x1 - x0;
    s->clip_h = y1 - y0;
}

static int visible(const nkfb_surface_t *s, int32_t x, int32_t y)
{
    return x >= s->clip_x && y >= s->clip_y &&
           x < s->clip_x + s->clip_w && y < s->clip_y + s->clip_h;
}

static void write_packed(nkfb_surface_t *s, int32_t x, int32_t y,
                         uint32_t packed)
{
    uint8_t *dst = s->pixels + (size_t)y * s->pitch +
                   (size_t)x * (s->bpp / 8u);

    dst[0] = (uint8_t)(packed & 0xffu);
    dst[1] = (uint8_t)((packed >> 8) & 0xffu);
    dst[2] = (uint8_t)((packed >> 16) & 0xffu);
    if (s->bpp == 32u) {
        dst[3] = 0xffu;
    }
}

void nkfb_pixel(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t rgba)
{
    uint32_t alpha;
    uint32_t packed;

    if (s == NULL || s->pixels == NULL || !visible(s, x, y)) {
        return;
    }
    alpha = rgba >> 24;
    if (alpha == 0u) {
        return;
    }
    packed = nkfb_pack(s, rgba);
    if (alpha == 255u) {
        write_packed(s, x, y, packed);
        return;
    }

    {
        const uint8_t *dst = s->pixels + (size_t)y * s->pitch +
                             (size_t)x * (s->bpp / 8u);
        uint32_t old = (uint32_t)dst[0] | ((uint32_t)dst[1] << 8) |
                       ((uint32_t)dst[2] << 16);
        uint8_t r = channel_up((old >> s->red_shift) &
                               channel_mask(s->red_size), s->red_size);
        uint8_t g = channel_up((old >> s->green_shift) &
                               channel_mask(s->green_size), s->green_size);
        uint8_t b = channel_up((old >> s->blue_shift) &
                               channel_mask(s->blue_size), s->blue_size);
        uint8_t nr = (uint8_t)((((rgba >> 16) & 0xffu) * alpha +
                                (uint32_t)r * (255u - alpha)) / 255u);
        uint8_t ng = (uint8_t)((((rgba >> 8) & 0xffu) * alpha +
                                (uint32_t)g * (255u - alpha)) / 255u);
        uint8_t nb = (uint8_t)(((rgba & 0xffu) * alpha +
                                (uint32_t)b * (255u - alpha)) / 255u);

        write_packed(s, x, y,
                     (channel_down(nr, s->red_size) << s->red_shift) |
                     (channel_down(ng, s->green_size) << s->green_shift) |
                     (channel_down(nb, s->blue_size) << s->blue_shift));
    }
}

void nkfb_fill(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
               uint32_t h, uint32_t rgba)
{
    int32_t y0;
    int32_t y1;
    int32_t x0;
    int32_t x1;

    if (s == NULL || s->pixels == NULL || w == 0u || h == 0u ||
        (rgba >> 24) == 0u) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = x + (int32_t)w;
    y1 = y + (int32_t)h;
    if (x0 < s->clip_x) {
        x0 = s->clip_x;
    }
    if (y0 < s->clip_y) {
        y0 = s->clip_y;
    }
    if (x1 > s->clip_x + s->clip_w) {
        x1 = s->clip_x + s->clip_w;
    }
    if (y1 > s->clip_y + s->clip_h) {
        y1 = s->clip_y + s->clip_h;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if ((rgba >> 24) == 255u) {
        uint32_t packed = nkfb_pack(s, rgba);
        int32_t yy;

        for (yy = y0; yy < y1; yy++) {
            int32_t xx;

            for (xx = x0; xx < x1; xx++) {
                write_packed(s, xx, yy, packed);
            }
        }
        return;
    }
    {
        int32_t yy;

        for (yy = y0; yy < y1; yy++) {
            int32_t xx;

            for (xx = x0; xx < x1; xx++) {
                nkfb_pixel(s, xx, yy, rgba);
            }
        }
    }
}

static int32_t max_i32(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static int32_t min_i32(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

/* Half-open circle test on pixel centres, so corners look round rather than
 * stair-stepped by whole pixels. */
static int inside_round(int32_t lx, int32_t ly, int32_t w, int32_t h,
                        int32_t r)
{
    int32_t cx;
    int32_t cy;
    int32_t dx;
    int32_t dy;

    if (r <= 0) {
        return 1;
    }
    if (lx >= r && lx < w - r) {
        return 1;
    }
    if (ly >= r && ly < h - r) {
        return 1;
    }
    if (lx < 0 || ly < 0 || lx >= w || ly >= h) {
        return 0;
    }
    cx = (lx < r) ? r : w - r;
    cy = (ly < r) ? r : h - r;
    dx = 2 * lx + 1 - 2 * cx;
    dy = 2 * ly + 1 - 2 * cy;
    return dx * dx + dy * dy <= 4 * r * r;
}

void nkfb_fill_round(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                     uint32_t h, uint32_t radius, uint32_t rgba)
{
    int32_t r;
    int32_t yy;

    if (s == NULL || s->pixels == NULL || w == 0u || h == 0u ||
        (rgba >> 24) == 0u) {
        return;
    }
    r = (int32_t)radius;
    r = min_i32(r, min_i32((int32_t)w / 2, (int32_t)h / 2));
    if (r <= 1) {
        nkfb_fill(s, x, y, w, h, rgba);
        return;
    }
    for (yy = 0; yy < (int32_t)h; yy++) {
        int32_t xx = 0;
        int32_t py = y + yy;
        int32_t run_start;

        if (py < s->clip_y || py >= s->clip_y + s->clip_h) {
            continue;
        }
        run_start = -1;
        for (xx = 0; xx <= (int32_t)w; xx++) {
            int inside = (xx < (int32_t)w) &&
                         inside_round(xx, yy, (int32_t)w, (int32_t)h, r) &&
                         (x + xx >= s->clip_x) &&
                         (x + xx < s->clip_x + s->clip_w);

            if (inside && run_start < 0) {
                run_start = xx;
            } else if (!inside && run_start >= 0) {
                nkfb_fill(s, x + run_start, py,
                          (uint32_t)(xx - run_start), 1u, rgba);
                run_start = -1;
            }
        }
    }
}

void nkfb_stroke_round(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, uint32_t radius, uint32_t thickness,
                       uint32_t rgba)
{
    int32_t r;
    int32_t t;
    int32_t yy;

    if (s == NULL || s->pixels == NULL || w == 0u || h == 0u ||
        thickness == 0u || (rgba >> 24) == 0u) {
        return;
    }
    r = (int32_t)radius;
    r = min_i32(r, min_i32((int32_t)w / 2, (int32_t)h / 2));
    t = (int32_t)thickness;
    for (yy = 0; yy < (int32_t)h; yy++) {
        int32_t py = y + yy;
        int32_t xx;

        if (py < s->clip_y || py >= s->clip_y + s->clip_h) {
            continue;
        }
        for (xx = 0; xx < (int32_t)w; xx++) {
            int32_t ir = max_i32(0, r - t);
            int outer = inside_round(xx, yy, (int32_t)w, (int32_t)h, r);
            int inner = 0;

            if (!outer) {
                continue;
            }
            if (xx >= t && yy >= t && xx < (int32_t)w - t &&
                yy < (int32_t)h - t) {
                inner = inside_round(xx - t, yy - t, (int32_t)w - 2 * t,
                                     (int32_t)h - 2 * t, ir);
            }
            if (!inner) {
                nkfb_pixel(s, x + xx, py, rgba);
            }
        }
    }
}

void nkfb_fill_gradient(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, uint32_t top, uint32_t bottom)
{
    uint32_t row;
    uint32_t at = top >> 24;
    uint32_t ab = bottom >> 24;

    if (s == NULL || w == 0u || h == 0u) {
        return;
    }
    for (row = 0u; row < h; row++) {
        uint32_t t = (h > 1u) ? (row * 255u) / (h - 1u) : 0u;
        uint32_t inv = 255u - t;
        uint32_t color = 0u;
        uint32_t parts[4];
        uint32_t i;

        parts[0] = (((top >> 16) & 0xffu) * inv +
                    ((bottom >> 16) & 0xffu) * t) / 255u;
        parts[1] = (((top >> 8) & 0xffu) * inv +
                    ((bottom >> 8) & 0xffu) * t) / 255u;
        parts[2] = ((top & 0xffu) * inv + (bottom & 0xffu) * t) / 255u;
        parts[3] = (at * inv + ab * t) / 255u;
        for (i = 0u; i < 4u; i++) {
            if (parts[i] > 255u) {
                parts[i] = 255u;
            }
        }
        color = NKFB_RGBA(parts[0], parts[1], parts[2], parts[3]);
        nkfb_fill(s, x, y + (int32_t)row, w, 1u, color);
    }
}

void nkfb_hline(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                uint32_t rgba)
{
    nkfb_fill(s, x, y, w, 1u, rgba);
}

void nkfb_vline(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t h,
                uint32_t rgba)
{
    nkfb_fill(s, x, y, 1u, h, rgba);
}

void nkfb_glyph(nkfb_surface_t *s, uint8_t ch, int32_t x, int32_t y,
                uint32_t rgba, uint32_t scale)
{
    const uint8_t *glyph = nkfb_font_glyph(ch);
    uint32_t row;

    if (s == NULL || scale == 0u || (rgba >> 24) == 0u) {
        return;
    }
    for (row = 0u; row < NKFB_FONT_H; row++) {
        uint32_t bits = glyph[row];
        uint32_t col = 0u;

        while (col < NKFB_FONT_W) {
            uint32_t run = 0u;

            if ((bits & (1u << (NKFB_FONT_W - 1u - col))) == 0u) {
                col++;
                continue;
            }
            while (col + run < NKFB_FONT_W &&
                   (bits & (1u << (NKFB_FONT_W - 1u - (col + run)))) != 0u) {
                run++;
            }
            nkfb_fill(s, x + (int32_t)(col * scale),
                      y + (int32_t)(row * scale), run * scale, scale, rgba);
            col += run;
        }
    }
}

int32_t nkfb_text(nkfb_surface_t *s, int32_t x, int32_t y, const char *text,
                  uint32_t rgba, uint32_t scale)
{
    int32_t pen = x;

    if (s == NULL || text == NULL || scale == 0u) {
        return x;
    }
    while (*text != '\0') {
        if (*text != ' ') {
            nkfb_glyph(s, (uint8_t)*text, pen, y, rgba, scale);
        }
        pen += (int32_t)(NKFB_FONT_ADVANCE * scale);
        text++;
    }
    return pen;
}

uint32_t nkfb_text_width(const char *text, uint32_t scale)
{
    uint32_t n = 0u;

    if (text == NULL) {
        return 0u;
    }
    while (text[n] != '\0') {
        n++;
    }
    return n * NKFB_FONT_ADVANCE * scale;
}

uint32_t nkfb_text_height(uint32_t scale)
{
    return NKFB_FONT_H * scale;
}
