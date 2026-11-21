/*
 * INTERFACEKIT common widgets: grained glass controls rendered into RGBA8888 pixmaps.
 *
 * The look is built from layered translucent washes, deterministic paper grain
 * and soft pigment bleed at the edges, all computed per pixel without floats,
 * heap or platform headers. The same object compiles for AMD64, ARM64 and
 * tiny embedded framebuffer targets; INTERFACEKIT_EMBEDDED reduces the effect and
 * removes the advanced controls (see interfacekit_widgets.h).
 */
#include <stdint.h>

#include "nullk/interfacekit_widgets.h"

interfacekit_style_t interfacekit_style_default(void)
{
    interfacekit_style_t style;

    style.wash = interfacekit_rgba(96u, 140u, 190u, 96u);
    style.edge = interfacekit_rgba(210u, 228u, 255u, 150u);
    style.text = interfacekit_rgba(242u, 247u, 255u, 255u);
    style.accent = interfacekit_rgba(90u, 170u, 255u, 220u);
    style.grain = 72u;
    style.bleed = 2u;
    style.text_scale = 1u;
    style.compact = 0u;
    return style;
}

interfacekit_style_t interfacekit_style_embedded(void)
{
    interfacekit_style_t style = interfacekit_style_default();

    style.grain = 24u;
    style.bleed = 0u;
    style.text_scale = 1u;
    style.compact = 1u;
    return style;
}

static uint8_t clamp_byte(int32_t value)
{
    if (value < 0) {
        return 0u;
    }
    if (value > 255) {
        return 255u;
    }
    return (uint8_t)value;
}

static uint32_t grain_hash(int32_t x, int32_t y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed;

    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static uint32_t edge_distance(interfacekit_rect_t rect, int32_t x, int32_t y)
{
    uint32_t left = (uint32_t)(x - rect.x);
    uint32_t right = (uint32_t)(rect.x + (int32_t)rect.w - 1 - x);
    uint32_t top = (uint32_t)(y - rect.y);
    uint32_t bottom = (uint32_t)(rect.y + (int32_t)rect.h - 1 - y);
    uint32_t d = left;

    if (right < d) {
        d = right;
    }
    if (top < d) {
        d = top;
    }
    if (bottom < d) {
        d = bottom;
    }
    return d;
}

void interfacekit_glass_fill(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                     interfacekit_rgba_t tint, const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    uint32_t seed = 0x51ed270bu;
    int32_t x;
    int32_t y;

    if (pm == (interfacekit_pixmap_t *)0) {
        return;
    }
    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    if (((tint >> 24) & 0xffu) == 0u || rect.w == 0u || rect.h == 0u) {
        return;
    }

    for (y = rect.y; y < rect.y + (int32_t)rect.h; y++) {
        for (x = rect.x; x < rect.x + (int32_t)rect.w; x++) {
            uint32_t hash = grain_hash(x, y, seed);
            int32_t jitter = (int32_t)(hash & 0xffu) - 128;
            int32_t grain = jitter * (int32_t)s->grain / 256;
            uint32_t cell = (uint32_t)(x / 6) ^ (uint32_t)(y / 5);
            int32_t bloom = ((cell & 3u) == 0u) ? 6 :
                            ((cell & 3u) == 2u) ? -5 : 0;
            int32_t r = (int32_t)((tint >> 16) & 0xffu) + grain / 2 + bloom;
            int32_t g = (int32_t)((tint >> 8) & 0xffu) + grain / 3 + bloom;
            int32_t b = (int32_t)(tint & 0xffu) + grain / 4;
            int32_t a = (int32_t)(tint >> 24) + grain / 3;

            if (s->bleed != 0u) {
                uint32_t d = edge_distance(rect, x, y);

                if (d < (uint32_t)s->bleed) {
                    uint32_t mix = ((uint32_t)s->bleed - d) * 255u /
                                   ((uint32_t)s->bleed + 1u);
                    uint32_t inv = 255u - mix;

                    r = (r * (int32_t)inv +
                         (int32_t)((s->edge >> 16) & 0xffu) * (int32_t)mix) / 255;
                    g = (g * (int32_t)inv +
                         (int32_t)((s->edge >> 8) & 0xffu) * (int32_t)mix) / 255;
                    b = (b * (int32_t)inv +
                         (int32_t)(s->edge & 0xffu) * (int32_t)mix) / 255;
                    a = (a * (int32_t)inv +
                         (int32_t)(s->edge >> 24) * (int32_t)mix) / 255;
                }
            }

            interfacekit_pixmap_blend(pm, x, y,
                              interfacekit_rgba(clamp_byte(r), clamp_byte(g),
                                        clamp_byte(b), clamp_byte(a)));
        }
    }
}

void interfacekit_widget_panel(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                       const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    interfacekit_glass_fill(pm, rect, s->wash, s);
    if (rect.w > 4u && rect.h > 4u) {
        interfacekit_rect_t inner = {rect.x + 2, rect.y + 2,
                             rect.w - 4u, rect.h - 4u};

        interfacekit_glass_fill(pm, inner,
                        (s->accent & 0x00ffffffu) | (0x20u << 24), s);
        interfacekit_pixmap_frame(pm, inner,
                          (s->edge & 0x00ffffffu) | (0x4au << 24));
    }
    interfacekit_pixmap_frame(pm, rect, s->edge);
}

static uint32_t text_width(const char *text, uint32_t scale)
{
    uint32_t len = 0u;

    if (text == (const char *)0) {
        return 0u;
    }
    while (text[len] != '\0') {
        len++;
    }
    return len * ((uint32_t)INTERFACEKIT_FONT_WIDTH + 1u) * scale;
}

void interfacekit_widget_label(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, const char *text,
                       const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    uint32_t scale;
    uint32_t width;
    int32_t x;
    int32_t y;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    scale = s->text_scale == 0u ? 1u : (uint32_t)s->text_scale;
    width = text_width(text, scale);
    x = width <= rect.w ? rect.x + (int32_t)((rect.w - width) / 2u) : rect.x;
    y = rect.y + (int32_t)((rect.h > (uint32_t)INTERFACEKIT_FONT_HEIGHT * scale)
                            ? ((rect.h - (uint32_t)INTERFACEKIT_FONT_HEIGHT * scale) / 2u)
                            : 0u);
    interfacekit_pixmap_text(pm, x, y, text, s->text, scale);
}

void interfacekit_widget_button(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                        const char *label, uint32_t pressed,
                        const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    interfacekit_rgba_t tint;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    tint = (s->accent & 0x00ffffffu) |
           ((pressed != 0u ? 0xd0u : 0x9au) << 24);
    interfacekit_glass_fill(pm, rect, tint, s);
    if (pressed != 0u && rect.w > 2u && rect.h > 2u) {
        interfacekit_rect_t shade = {rect.x + 1, rect.y + 1, rect.w - 2u, rect.h - 2u};

        interfacekit_glass_fill(pm, shade,
                        (s->wash & 0x00ffffffu) | (0x50u << 24), s);
    }
    interfacekit_pixmap_frame(pm, rect, s->edge);
    interfacekit_widget_label(pm, rect, label, s);
}

void interfacekit_widget_progress(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                          uint32_t percent, const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    uint32_t filled;
    interfacekit_rect_t value;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    interfacekit_glass_fill(pm, rect,
                    (s->wash & 0x00ffffffu) | (0x70u << 24), s);
    filled = rect.w * percent / 100u;
    if (filled != 0u) {
        value = (interfacekit_rect_t){rect.x, rect.y, filled, rect.h};
        interfacekit_glass_fill(pm, value, s->accent, s);
    }
    interfacekit_pixmap_frame(pm, rect, s->edge);
}

#if INTERFACEKIT_HAS_TOGGLE
void interfacekit_widget_toggle(interfacekit_pixmap_t *pm, interfacekit_rect_t rect, uint32_t on,
                        const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    uint32_t knob;
    int32_t knob_x;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    interfacekit_glass_fill(pm, rect,
                    (on != 0u ? s->accent : s->wash), s);
    interfacekit_pixmap_frame(pm, rect, s->edge);
    if (rect.w <= 4u || rect.h <= 4u) {
        return;
    }
    knob = rect.h - 4u;
    if (knob > rect.w - 4u) {
        knob = rect.w - 4u;
    }
    knob_x = on != 0u
                 ? rect.x + (int32_t)(rect.w - knob - 2u)
                 : rect.x + 2;
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){knob_x, rect.y + 2, knob, knob},
                     on != 0u ? s->text
                              : (s->edge & 0x00ffffffu) | (0xd0u << 24));
}
#endif

#if INTERFACEKIT_HAS_SLIDER
void interfacekit_widget_slider(interfacekit_pixmap_t *pm, interfacekit_rect_t rect,
                        uint32_t percent, const interfacekit_style_t *style)
{
    interfacekit_style_t fallback;
    const interfacekit_style_t *s = style;
    interfacekit_rect_t track;
    interfacekit_rect_t value;
    uint32_t knob;
    int32_t knob_x;

    if (s == (const interfacekit_style_t *)0) {
        fallback = interfacekit_style_default();
        s = &fallback;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    track = (interfacekit_rect_t){rect.x, rect.y + (int32_t)(rect.h / 2u) - 1,
                          rect.w, rect.h > 2u ? 3u : rect.h};
    interfacekit_glass_fill(pm, track,
                    (s->wash & 0x00ffffffu) | (0x80u << 24), s);
    value = (interfacekit_rect_t){rect.x, track.y, track.w * percent / 100u, track.h};
    if (value.w != 0u) {
        interfacekit_glass_fill(pm, value, s->accent, s);
    }
    interfacekit_pixmap_frame(pm, track, s->edge);
    knob = rect.h > 6u ? rect.h - 4u : rect.h;
    knob_x = rect.x + (int32_t)(track.w * percent / 100u);
    if (knob_x + (int32_t)knob > rect.x + (int32_t)rect.w) {
        knob_x = rect.x + (int32_t)rect.w - (int32_t)knob;
    }
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){knob_x, rect.y, knob, rect.h},
                     s->text);
}
#endif
