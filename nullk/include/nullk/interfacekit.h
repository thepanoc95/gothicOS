/*
 * InterfaceKit - portable drawing core for null_kernel.
 *
 * This header is board-neutral: embedded, ARM64 and AMD64 builds can all use
 * it. It has no syscall, allocator, clock or architecture types. Surfaces are
 * RGBA8888 pixmaps backed by caller-owned static memory, and framebuffers are
 * described by generic pixel masks rather than by a Limine/ABI v3 structure.
 *
 * Window management and AMD64 input routing live in interfacekit_server.h.
 * Common controls live in interfacekit_widgets.h.
 */
#ifndef NULLK_INTERFACEKIT_H
#define NULLK_INTERFACEKIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INTERFACEKIT_FONT_WIDTH  3u
#define INTERFACEKIT_FONT_HEIGHT 5u

/* A, R, G, B each occupy one byte. Alpha 255 is opaque, 0 is transparent. */
typedef uint32_t interfacekit_rgba_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
} interfacekit_rect_t;

typedef struct {
    uint32_t *pixels;           /* RGBA8888, top-down, stride in pixels */
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} interfacekit_pixmap_t;

/*
 * Generic linear framebuffer descriptor. `pitch` is bytes per scanline and
 * the red_size/red_shift style fields describe the packed pixel layout for
 * 16/24/32 bpp targets. Embedded displays and Limine framebuffers both fit
 * this shape.
 */
typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
} interfacekit_framebuffer_t;

/* ---- Pure drawing helpers (shared by servers, clients and host tests) ---- */

interfacekit_rgba_t interfacekit_rgba(uint8_t r, uint8_t g, uint8_t b,
                                       uint8_t a);
uint32_t interfacekit_pixel_pack(const interfacekit_framebuffer_t *fb,
                                 interfacekit_rgba_t color);

void interfacekit_pixmap_init(interfacekit_pixmap_t *pm, void *memory,
                              uint32_t width, uint32_t height,
                              uint32_t stride);
void interfacekit_pixmap_clear(interfacekit_pixmap_t *pm);
int interfacekit_pixmap_contains(const interfacekit_pixmap_t *pm, int32_t x,
                                 int32_t y);
void interfacekit_pixmap_blend(interfacekit_pixmap_t *pm, int32_t x, int32_t y,
                               interfacekit_rgba_t color);
void interfacekit_pixmap_fill(interfacekit_pixmap_t *pm,
                              interfacekit_rect_t rect,
                              interfacekit_rgba_t color);
void interfacekit_pixmap_frame(interfacekit_pixmap_t *pm,
                               interfacekit_rect_t rect,
                               interfacekit_rgba_t color);
void interfacekit_pixmap_text(interfacekit_pixmap_t *pm, int32_t x, int32_t y,
                              const char *text, interfacekit_rgba_t color,
                              uint32_t scale);
const uint8_t *interfacekit_glyph(uint8_t ch);

void interfacekit_framebuffer_init(interfacekit_framebuffer_t *fb, void *base,
                                   uint32_t width, uint32_t height,
                                   uint32_t pitch, uint8_t bpp,
                                   uint8_t red_size, uint8_t red_shift,
                                   uint8_t green_size, uint8_t green_shift,
                                   uint8_t blue_size, uint8_t blue_shift);
void interfacekit_fb_blend(interfacekit_framebuffer_t *fb, int32_t x, int32_t y,
                           interfacekit_rgba_t color);
void interfacekit_fb_fill(interfacekit_framebuffer_t *fb,
                          interfacekit_rect_t rect,
                          interfacekit_rgba_t color);
void interfacekit_fb_blit(interfacekit_framebuffer_t *fb, int32_t x, int32_t y,
                          const interfacekit_pixmap_t *pm, uint8_t alpha);
void interfacekit_fb_cursor(interfacekit_framebuffer_t *fb, int32_t x,
                            int32_t y, interfacekit_rgba_t color);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_INTERFACEKIT_H */
