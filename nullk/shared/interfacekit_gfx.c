/*
 * INTERFACEKIT pure drawing helpers: RGBA surfaces, Limine-mask framebuffer packing,
 * a tiny 3x5 bitmap font and a mouse cursor mask. No syscalls, no globals
 * besides read-only tables, so the same object links into init.elf and the
 * hosted test binary.
 */
#include <stdint.h>

#include "nullk/interfacekit.h"

/*
 * 3x5 font, ASCII 0x20..0x5f. Each glyph is three column bytes; bit n is row
 * n from the top. Lowercase characters are rendered from the uppercase glyph.
 */
static const uint8_t g_font[64u][3u] = {
    {0x00,0x00,0x00}, /* space */
    {0x00,0x17,0x00}, /* ! */
    {0x03,0x00,0x03}, /* " */
    {0x1f,0x0a,0x1f}, /* # */
    {0x16,0x1b,0x1a}, /* $ */
    {0x11,0x04,0x19}, /* % */
    {0x0f,0x15,0x1a}, /* & */
    {0x00,0x03,0x00}, /* ' */
    {0x10,0x0e,0x11}, /* ( */
    {0x11,0x0e,0x10}, /* ) */
    {0x0a,0x04,0x0a}, /* * */
    {0x04,0x0e,0x04}, /* + */
    {0x10,0x08,0x00}, /* , */
    {0x04,0x04,0x04}, /* - */
    {0x00,0x10,0x00}, /* . */
    {0x18,0x04,0x03}, /* / */
    {0x1f,0x11,0x1f}, /* 0 */
    {0x12,0x1f,0x10}, /* 1 */
    {0x1d,0x15,0x17}, /* 2 */
    {0x11,0x15,0x1f}, /* 3 */
    {0x07,0x04,0x1f}, /* 4 */
    {0x17,0x15,0x15}, /* 5 */
    {0x1f,0x1d,0x15}, /* 6 */
    {0x01,0x1d,0x01}, /* 7 */
    {0x1f,0x15,0x1f}, /* 8 */
    {0x17,0x15,0x1f}, /* 9 */
    {0x00,0x0a,0x00}, /* : */
    {0x10,0x0a,0x00}, /* ; */
    {0x04,0x0a,0x11}, /* < */
    {0x0a,0x0a,0x0a}, /* = */
    {0x11,0x0a,0x04}, /* > */
    {0x05,0x15,0x01}, /* ? */
    {0x17,0x15,0x1f}, /* @ */
    {0x1e,0x05,0x1e}, /* A */
    {0x15,0x15,0x0a}, /* B */
    {0x0e,0x11,0x11}, /* C */
    {0x11,0x1f,0x0e}, /* D */
    {0x1f,0x15,0x11}, /* E */
    {0x1f,0x05,0x01}, /* F */
    {0x12,0x11,0x1d}, /* G */
    {0x1f,0x04,0x1f}, /* H */
    {0x11,0x1f,0x11}, /* I */
    {0x18,0x10,0x1f}, /* J */
    {0x1f,0x04,0x1b}, /* K */
    {0x1f,0x10,0x10}, /* L */
    {0x1f,0x06,0x1f}, /* M */
    {0x1f,0x0e,0x1f}, /* N */
    {0x0e,0x11,0x0e}, /* O */
    {0x07,0x05,0x02}, /* P */
    {0x0e,0x11,0x1e}, /* Q */
    {0x07,0x05,0x1a}, /* R */
    {0x11,0x15,0x0e}, /* S */
    {0x01,0x1f,0x01}, /* T */
    {0x1f,0x0f,0x1f}, /* U */
    {0x07,0x18,0x07}, /* V */
    {0x1f,0x0c,0x1f}, /* W */
    {0x1b,0x04,0x1b}, /* X */
    {0x03,0x1c,0x03}, /* Y */
    {0x11,0x15,0x1b}, /* Z */
    {0x11,0x1f,0x00}, /* [ */
    {0x03,0x04,0x18}, /* \ */
    {0x1f,0x11,0x00}, /* ] */
    {0x02,0x01,0x02}, /* ^ */
    {0x10,0x10,0x10}  /* _ */
};

/* ' ' transparent, 'o' outline, 'X' fill. */
static const char *const g_cursor[] = {
    "o.......",
    "oo......",
    "oXo.....",
    "oXXo....",
    "oXXXo...",
    "oXXXXo..",
    "oXXXXXo.",
    "oXXXXXXo",
    "oXXXXo..",
    "oXoXo...",
    "oo.Xo...",
    "...oXo..",
    "...oXo..",
    "....oo.."
};

static uint8_t scale_up(uint8_t value, uint8_t bits)
{
    uint32_t max;

    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 8u) {
        return value;
    }
    max = (1u << bits) - 1u;
    return (uint8_t)((value * 255u + max / 2u) / max);
}

static uint8_t scale_down(uint8_t value, uint8_t bits)
{
    if (bits == 0u) {
        return 0u;
    }
    if (bits >= 8u) {
        return value;
    }
    return (uint8_t)(value >> (8u - bits));
}

interfacekit_rgba_t interfacekit_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t interfacekit_pixel_pack(const interfacekit_framebuffer_t *fb, interfacekit_rgba_t color)
{
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (fb == 0 || fb->bpp < 16u) {
        return 0u;
    }
    r = scale_down((uint8_t)(color >> 16), fb->red_size);
    g = scale_down((uint8_t)(color >> 8), fb->green_size);
    b = scale_down((uint8_t)color, fb->blue_size);
    return (r << fb->red_shift) | (g << fb->green_shift) |
           (b << fb->blue_shift);
}

void interfacekit_pixmap_init(interfacekit_pixmap_t *pm, void *memory, uint32_t width,
                    uint32_t height, uint32_t stride)
{
    if (pm == 0) {
        return;
    }
    pm->pixels = (uint32_t *)memory;
    pm->width = width;
    pm->height = height;
    pm->stride = stride;
}

void interfacekit_pixmap_clear(interfacekit_pixmap_t *pm)
{
    uint32_t i;
    uint32_t total;

    if (pm == 0 || pm->pixels == 0) {
        return;
    }
    total = pm->stride * pm->height;
    for (i = 0u; i < total; i++) {
        pm->pixels[i] = 0u;
    }
}

int interfacekit_pixmap_contains(const interfacekit_pixmap_t *pm, int32_t x, int32_t y)
{
    if (pm == 0 || pm->pixels == 0) {
        return 0;
    }
    return (x >= 0 && y >= 0 && (uint32_t)x < pm->width &&
            (uint32_t)y < pm->height) ? 1 : 0;
}

void interfacekit_pixmap_blend(interfacekit_pixmap_t *pm, int32_t x, int32_t y, interfacekit_rgba_t color)
{
    uint32_t alpha = color >> 24;
    uint32_t current;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint32_t *dst;

    if (!interfacekit_pixmap_contains(pm, x, y)) {
        return;
    }
    dst = &pm->pixels[(uint32_t)y * pm->stride + (uint32_t)x];
    if (alpha == 0u) {
        return;
    }
    if (alpha == 255u) {
        *dst = color;
        return;
    }
    current = *dst;
    r = (uint8_t)(((color >> 16) & 0xffu) * alpha / 255u +
                  ((current >> 16) & 0xffu) * (255u - alpha) / 255u);
    g = (uint8_t)(((color >> 8) & 0xffu) * alpha / 255u +
                  ((current >> 8) & 0xffu) * (255u - alpha) / 255u);
    b = (uint8_t)((color & 0xffu) * alpha / 255u +
                  (current & 0xffu) * (255u - alpha) / 255u);
    *dst = interfacekit_rgba(r, g, b, 255u);
}

void interfacekit_pixmap_fill(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, interfacekit_rgba_t color)
{
    int32_t x;
    int32_t y;

    if (pm == 0) {
        return;
    }
    for (y = rect.y; y < rect.y + (int32_t)rect.h; y++) {
        for (x = rect.x; x < rect.x + (int32_t)rect.w; x++) {
            interfacekit_pixmap_blend(pm, x, y, color);
        }
    }
}

void interfacekit_pixmap_frame(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, interfacekit_rgba_t color)
{
    if (rect.w == 0u || rect.h == 0u) {
        return;
    }
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){rect.x, rect.y, rect.w, 1u}, color);
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){rect.x, rect.y + (int32_t)rect.h - 1,
                                   rect.w, 1u}, color);
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){rect.x, rect.y, 1u, rect.h}, color);
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){rect.x + (int32_t)rect.w - 1, rect.y,
                                   1u, rect.h}, color);
}

const uint8_t *interfacekit_glyph(uint8_t ch)
{
    uint8_t c = ch;

    if (c >= 'a' && c <= 'z') {
        c = (uint8_t)(c - 0x20u);
    }
    if (c < 0x20u || c > 0x5fu) {
        c = '?';
    }
    return g_font[c - 0x20u];
}

void interfacekit_pixmap_text(interfacekit_pixmap_t *pm, int32_t x, int32_t y, const char *text,
                    interfacekit_rgba_t color, uint32_t scale)
{
    int32_t px = x;
    uint32_t i;

    if (pm == 0 || text == 0 || scale == 0u) {
        return;
    }
    for (i = 0u; text[i] != '\0'; i++) {
        const uint8_t *glyph = interfacekit_glyph((uint8_t)text[i]);
        uint32_t column;

        for (column = 0u; column < (uint32_t)INTERFACEKIT_FONT_WIDTH; column++) {
            uint32_t row;

            for (row = 0u; row < (uint32_t)INTERFACEKIT_FONT_HEIGHT; row++) {
                if ((glyph[column] & (1u << row)) == 0u) {
                    continue;
                }
                interfacekit_pixmap_fill(pm, (interfacekit_rect_t){px + (int32_t)column * (int32_t)scale,
                                               y + (int32_t)row * (int32_t)scale,
                                               scale, scale}, color);
            }
        }
        px += (int32_t)((INTERFACEKIT_FONT_WIDTH + 1u) * scale);
    }
}

void interfacekit_framebuffer_init(interfacekit_framebuffer_t *fb, void *base,
                                  uint32_t width, uint32_t height,
                                  uint32_t pitch, uint8_t bpp,
                                  uint8_t red_size, uint8_t red_shift,
                                  uint8_t green_size, uint8_t green_shift,
                                  uint8_t blue_size, uint8_t blue_shift)
{
    if (fb == 0) {
        return;
    }
    fb->pixels = (uint8_t *)base;
    fb->width = width;
    fb->height = height;
    fb->pitch = pitch;
    fb->bpp = bpp;
    fb->red_size = red_size;
    fb->red_shift = red_shift;
    fb->green_size = green_size;
    fb->green_shift = green_shift;
    fb->blue_size = blue_size;
    fb->blue_shift = blue_shift;
}

void interfacekit_fb_blend(interfacekit_framebuffer_t *fb, int32_t x, int32_t y, interfacekit_rgba_t color)
{
    uint32_t alpha = color >> 24;
    uint32_t packed;
    uint8_t *dst;

    if (fb == 0 || fb->pixels == 0 || x < 0 || y < 0 ||
        (uint32_t)x >= fb->width || (uint32_t)y >= fb->height) {
        return;
    }
    packed = interfacekit_pixel_pack(fb, color);
    dst = fb->pixels + (uint32_t)y * fb->pitch +
          (uint32_t)x * (fb->bpp / 8u);
    if (alpha == 0u) {
        return;
    }
    if (alpha == 255u) {
        dst[0] = (uint8_t)(packed & 0xffu);
        dst[1] = (uint8_t)((packed >> 8) & 0xffu);
        dst[2] = (uint8_t)((packed >> 16) & 0xffu);
        if (fb->bpp == 32u) {
            dst[3] = 0xffu;
        }
        return;
    }

    {
        uint32_t old = (uint32_t)dst[0] | ((uint32_t)dst[1] << 8) |
                       ((uint32_t)dst[2] << 16);
        uint8_t r = scale_up((uint8_t)((old >> fb->red_shift) &
                          ((1u << fb->red_size) - 1u)), fb->red_size);
        uint8_t g = scale_up((uint8_t)((old >> fb->green_shift) &
                          ((1u << fb->green_size) - 1u)), fb->green_size);
        uint8_t b = scale_up((uint8_t)((old >> fb->blue_shift) &
                          ((1u << fb->blue_size) - 1u)), fb->blue_size);
        uint8_t nr = (uint8_t)(((color >> 16) & 0xffu) * alpha / 255u +
                               r * (255u - alpha) / 255u);
        uint8_t ng = (uint8_t)(((color >> 8) & 0xffu) * alpha / 255u +
                               g * (255u - alpha) / 255u);
        uint8_t nb = (uint8_t)((color & 0xffu) * alpha / 255u +
                               b * (255u - alpha) / 255u);
        packed = interfacekit_pixel_pack(fb, interfacekit_rgba(nr, ng, nb, 255u));
    }
    dst[0] = (uint8_t)(packed & 0xffu);
    dst[1] = (uint8_t)((packed >> 8) & 0xffu);
    dst[2] = (uint8_t)((packed >> 16) & 0xffu);
    if (fb->bpp == 32u) {
        dst[3] = 0xffu;
    }
}

void interfacekit_fb_fill(interfacekit_framebuffer_t *fb, interfacekit_rect_t rect, interfacekit_rgba_t color)
{
    int32_t x;
    int32_t y;

    if (fb == 0) {
        return;
    }
    for (y = rect.y; y < rect.y + (int32_t)rect.h; y++) {
        for (x = rect.x; x < rect.x + (int32_t)rect.w; x++) {
            interfacekit_fb_blend(fb, x, y, color);
        }
    }
}

void interfacekit_fb_blit(interfacekit_framebuffer_t *fb, int32_t x, int32_t y,
                const interfacekit_pixmap_t *pm, uint8_t alpha)
{
    uint32_t row;
    uint32_t col;

    if (fb == 0 || pm == 0 || pm->pixels == 0) {
        return;
    }
    for (row = 0u; row < pm->height; row++) {
        for (col = 0u; col < pm->width; col++) {
            uint32_t value = pm->pixels[row * pm->stride + col];
            uint32_t a = value >> 24;

            if (a == 0u) {
                continue;
            }
            if (alpha != 255u) {
                a = a * alpha / 255u;
            }
            interfacekit_fb_blend(fb, x + (int32_t)col, y + (int32_t)row,
                        (value & 0x00ffffffu) | (a << 24));
        }
    }
}

void interfacekit_fb_cursor(interfacekit_framebuffer_t *fb, int32_t x, int32_t y, interfacekit_rgba_t color)
{
    uint32_t row;
    uint32_t col;

    if (fb == 0) {
        return;
    }
    for (row = 0u; row < sizeof(g_cursor) / sizeof(g_cursor[0]); row++) {
        for (col = 0u; g_cursor[row][col] != '\0'; col++) {
            char c = g_cursor[row][col];
            interfacekit_rgba_t shade;

            if (c == 'X') {
                shade = color;
            } else if (c == 'o') {
                shade = interfacekit_rgba(0u, 0u, 0u, 255u);
            } else {
                continue;
            }
            interfacekit_fb_blend(fb, x + (int32_t)col, y + (int32_t)row, shade);
        }
    }
}
