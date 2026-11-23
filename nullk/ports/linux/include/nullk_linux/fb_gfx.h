/* Linear framebuffer drawing core for the native Linux port.
 *
 * Small, self-contained pixel operations (blend, fills, rounded rectangles,
 * gradients, lines and the console font) on top of the ABI v3 FB_INFO/FB_MAP
 * scanout. It shares no state and no code with InterfaceKit, so the CLI stays
 * drawable with CONFIG_FB and DRM_FBDEV_EMULATION both disabled.
 */
#ifndef NULLK_LINUX_FB_GFX_H
#define NULLK_LINUX_FB_GFX_H

#include <stdint.h>

#define NKFB_RGBA(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | \
     ((uint32_t)(g) << 8) | (uint32_t)(b))

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;      /* bytes per scanline */
    uint8_t bpp;         /* 16, 24 or 32 */
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
    /* Clip rectangle used by every operation below. */
    int32_t clip_x;
    int32_t clip_y;
    int32_t clip_w;
    int32_t clip_h;
} nkfb_surface_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
} nkfb_rect_t;

void nkfb_surface_init(nkfb_surface_t *s, void *base, uint32_t width,
                       uint32_t height, uint32_t pitch, uint8_t bpp,
                       uint8_t red_size, uint8_t red_shift, uint8_t green_size,
                       uint8_t green_shift, uint8_t blue_size,
                       uint8_t blue_shift);
void nkfb_clip_reset(nkfb_surface_t *s);
void nkfb_clip_set(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                   uint32_t h);

uint32_t nkfb_pack(const nkfb_surface_t *s, uint32_t rgba);

void nkfb_pixel(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t rgba);
void nkfb_fill(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
               uint32_t h, uint32_t rgba);
void nkfb_fill_round(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                     uint32_t h, uint32_t radius, uint32_t rgba);
void nkfb_stroke_round(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, uint32_t radius, uint32_t thickness,
                       uint32_t rgba);
void nkfb_fill_gradient(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, uint32_t top, uint32_t bottom);
void nkfb_hline(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t w,
                uint32_t rgba);
void nkfb_vline(nkfb_surface_t *s, int32_t x, int32_t y, uint32_t h,
                uint32_t rgba);

void nkfb_glyph(nkfb_surface_t *s, uint8_t ch, int32_t x, int32_t y,
                uint32_t rgba, uint32_t scale);
int32_t nkfb_text(nkfb_surface_t *s, int32_t x, int32_t y, const char *text,
                  uint32_t rgba, uint32_t scale);
uint32_t nkfb_text_width(const char *text, uint32_t scale);
uint32_t nkfb_text_height(uint32_t scale);

#endif /* NULLK_LINUX_FB_GFX_H */
