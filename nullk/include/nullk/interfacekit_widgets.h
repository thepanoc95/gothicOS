/*
 * INTERFACEKIT common widgets - one portable control set for every null_kernel port.
 *
 * The widget layer is pure C over RGBA8888 pixmaps. It has no framebuffer,
 * syscall, clock or allocator dependency, so AMD64, ARM64 and small embedded
 * boards can all draw the same controls. Embedded builds define INTERFACEKIT_EMBEDDED
 * to drop the advanced widgets and reduce the grained-glass effect; the core
 * panel/label/button/progress controls remain common to every profile.
 */
#ifndef NULLK_INTERFACEKIT_WIDGETS_H
#define NULLK_INTERFACEKIT_WIDGETS_H

#include <stdint.h>

#include "nullk/interfacekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Full desktop profiles (AMD64 and ARM64) get every widget. Boards that define
 * INTERFACEKIT_EMBEDDED, or non-desktop targets without an explicit full
 * profile, get the reduced common set.
 */
#if defined(INTERFACEKIT_EMBEDDED)
#define INTERFACEKIT_PROFILE_EMBEDDED 1
#define INTERFACEKIT_PROFILE_AMD64    0
#define INTERFACEKIT_PROFILE_ARM64    0
#elif defined(__aarch64__) || defined(_M_ARM64)
#define INTERFACEKIT_PROFILE_EMBEDDED 0
#define INTERFACEKIT_PROFILE_AMD64    0
#define INTERFACEKIT_PROFILE_ARM64    1
#elif defined(__x86_64__) || defined(_M_X64)
#define INTERFACEKIT_PROFILE_EMBEDDED 0
#define INTERFACEKIT_PROFILE_AMD64    1
#define INTERFACEKIT_PROFILE_ARM64    0
#else
#define INTERFACEKIT_PROFILE_EMBEDDED 1
#define INTERFACEKIT_PROFILE_AMD64    0
#define INTERFACEKIT_PROFILE_ARM64    0
#endif

#define INTERFACEKIT_PROFILE_DESKTOP \
    (INTERFACEKIT_PROFILE_AMD64 || INTERFACEKIT_PROFILE_ARM64)

/* Always available, including small embedded framebuffer targets. */
#define INTERFACEKIT_HAS_PANEL   1
#define INTERFACEKIT_HAS_LABEL   1
#define INTERFACEKIT_HAS_BUTTON  1
#define INTERFACEKIT_HAS_PROGRESS 1

/* Full desktop profiles (AMD64 and ARM64) also get these controls. */
#define INTERFACEKIT_HAS_TOGGLE  (!INTERFACEKIT_PROFILE_EMBEDDED)
#define INTERFACEKIT_HAS_SLIDER  (!INTERFACEKIT_PROFILE_EMBEDDED)

/*
 * Visual style for the watercolor / grained-glass look.
 *   wash   - translucent base colour of glass surfaces
 *   edge   - soft border pigment
 *   text   - foreground text
 *   accent - active control colour
 *   grain  - 0..255 amount of deterministic paper grain
 *   bleed  - 0..3 watercolor edge spread
 */
typedef struct {
    interfacekit_rgba_t wash;
    interfacekit_rgba_t edge;
    interfacekit_rgba_t text;
    interfacekit_rgba_t accent;
    uint8_t grain;
    uint8_t bleed;
    uint8_t text_scale;
    uint8_t compact;
} interfacekit_style_t;

/* Style presets; no global mutable state. */
interfacekit_style_t interfacekit_style_default(void);
interfacekit_style_t interfacekit_style_embedded(void);

/*
 * Fill a rectangle with translucent, deterministically grained pigment.
 * This is the primitive behind every glass widget and is safe to call from
 * embedded code with a small static pixmap.
 */
void interfacekit_glass_fill(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                     interfacekit_rgba_t tint, const interfacekit_style_t *style);

/* Common widgets. */
void interfacekit_widget_panel(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                       const interfacekit_style_t *style);
void interfacekit_widget_label(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, const char *text,
                       const interfacekit_style_t *style);
void interfacekit_widget_button(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                        const char *label, uint32_t pressed,
                        const interfacekit_style_t *style);
void interfacekit_widget_progress(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                          uint32_t percent, const interfacekit_style_t *style);

#if INTERFACEKIT_HAS_TOGGLE
void interfacekit_widget_toggle(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, uint32_t on,
                        const interfacekit_style_t *style);
#endif

#if INTERFACEKIT_HAS_SLIDER
void interfacekit_widget_slider(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                        uint32_t percent, const interfacekit_style_t *style);
#endif

#ifdef __cplusplus
}
#endif

#endif /* NULLK_INTERFACEKIT_WIDGETS_H */
