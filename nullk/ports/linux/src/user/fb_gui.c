/*
 * Minimal GUI engine for the null_kernel Linux CLI framebuffer.
 *
 * Immediate-mode widget painting: window chrome, cards, key/value rows,
 * dividers, badges, buttons, progress bars, lists, tables and a status bar.
 * No allocation, no globals, no InterfaceKit, no input handling - the console
 * in fb_console.c owns geometry and calls these helpers.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "nullk_linux/fb_font.h"
#include "nullk_linux/fb_gfx.h"
#include "nullk_linux/fb_gui.h"

const nkfb_theme_t nkfb_theme_dark = {
    .bg = NKFB_RGBA(7u, 10u, 18u, 255u),
    .panel = NKFB_RGBA(14u, 22u, 38u, 255u),
    .panel_alt = NKFB_RGBA(20u, 32u, 52u, 255u),
    .border = NKFB_RGBA(36u, 57u, 92u, 255u),
    .text = NKFB_RGBA(223u, 233u, 255u, 255u),
    .text_dim = NKFB_RGBA(138u, 160u, 196u, 255u),
    .accent = NKFB_RGBA(108u, 198u, 255u, 255u),
    .accent_dim = NKFB_RGBA(43u, 111u, 168u, 255u),
    .good = NKFB_RGBA(88u, 214u, 141u, 255u),
    .warn = NKFB_RGBA(244u, 201u, 93u, 255u),
    .bad = NKFB_RGBA(239u, 106u, 106u, 255u),
    .shadow = NKFB_RGBA(0u, 0u, 0u, 96u),
    .button = NKFB_RGBA(27u, 44u, 72u, 255u),
    .button_active = NKFB_RGBA(42u, 69u, 112u, 255u),
};

static uint32_t glyph_advance(const nkfb_ui_t *ui)
{
    return NKFB_FONT_ADVANCE * ui->scale;
}

void nkfb_ui_init(nkfb_ui_t *ui, nkfb_surface_t *surface,
                  const nkfb_theme_t *theme, uint32_t scale)
{
    if (ui == NULL) {
        return;
    }
    ui->surface = surface;
    ui->theme = (theme != NULL) ? theme : &nkfb_theme_dark;
    ui->scale = (scale == 0u) ? 1u : scale;
    ui->line_h = (NKFB_FONT_H + 2u) * ui->scale;
    ui->gap = 4u * ui->scale + 2u;
}

uint32_t nkfb_ui_line(const nkfb_ui_t *ui)
{
    return (ui != NULL) ? ui->line_h : 0u;
}

uint32_t nkfb_ui_gap(const nkfb_ui_t *ui)
{
    return (ui != NULL) ? ui->gap : 0u;
}

static int32_t text_top(const nkfb_ui_t *ui, int32_t y)
{
    /* Center the 10-row face inside the line box, leaving the descender rows
     * free at the bottom. */
    return y + (int32_t)ui->scale;
}

void nkfb_ui_label(nkfb_ui_t *ui, int32_t x, int32_t y, const char *text,
                   uint32_t color)
{
    if (ui == NULL || ui->surface == NULL || text == NULL) {
        return;
    }
    (void)nkfb_text(ui->surface, x, text_top(ui, y), text, color, ui->scale);
}

void nkfb_ui_window(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                    uint32_t h, const char *title, const char *subtitle)
{
    nkfb_surface_t *s;
    const nkfb_theme_t *t;
    uint32_t r;
    uint32_t title_h;
    uint32_t i;

    if (ui == NULL || ui->surface == NULL || w < 8u || h < 8u) {
        return;
    }
    s = ui->surface;
    t = ui->theme;
    r = 4u * ui->scale + 2u;
    title_h = ui->line_h + ui->gap;

    nkfb_fill_round(s, x + (int32_t)ui->scale * 2, y + (int32_t)ui->scale * 3,
                    w, h, r, t->shadow);
    nkfb_fill_round(s, x, y, w, h, r, t->panel);
    nkfb_stroke_round(s, x, y, w, h, r, 1u, t->border);

    nkfb_clip_set(s, x, y, w, title_h + r);
    nkfb_fill_gradient(s, x, y, w, title_h, t->panel_alt, t->panel);
    nkfb_hline(s, x, y + (int32_t)title_h, w, t->border);
    /* Window dots, drawn as real pixels so they read at 1x too. */
    for (i = 0u; i < 3u; i++) {
        uint32_t dot = (i == 0u) ? t->bad : ((i == 1u) ? t->warn : t->good);
        nkfb_fill_round(s, x + (int32_t)ui->gap +
                           (int32_t)(i * (ui->scale * 4u + 4u)) +
                           (i == 0u ? 0 : 0),
                        y + (int32_t)(title_h / 2u) - (int32_t)(ui->scale + 1u),
                        ui->scale * 2u + 2u, ui->scale * 2u + 2u,
                        ui->scale + 1u, dot);
    }
    nkfb_ui_label(ui, x + (int32_t)ui->gap + (int32_t)(ui->scale * 22u),
                  y + (int32_t)ui->gap / 2, title, t->text);
    if (subtitle != NULL) {
        int32_t sub_x = x + (int32_t)ui->gap + (int32_t)(ui->scale * 22u) +
                        (int32_t)nkfb_text_width(title, ui->scale) +
                        (int32_t)glyph_advance(ui);
        int32_t sub_w = (int32_t)nkfb_text_width(subtitle, ui->scale);

        if (sub_x + sub_w < x + (int32_t)w - (int32_t)ui->gap) {
            nkfb_ui_label(ui, sub_x, y + (int32_t)ui->gap / 2, subtitle,
                          t->text_dim);
        }
    }
    nkfb_clip_reset(s);
}

int32_t nkfb_ui_card(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                     uint32_t h, const char *title)
{
    nkfb_surface_t *s;
    const nkfb_theme_t *t;
    uint32_t r;
    uint32_t title_h;

    if (ui == NULL || ui->surface == NULL || w < 8u || h < 8u) {
        return y;
    }
    s = ui->surface;
    t = ui->theme;
    r = 3u * ui->scale + 2u;
    title_h = ui->line_h + ui->gap / 2u;

    nkfb_fill_round(s, x + (int32_t)ui->scale, y + (int32_t)ui->scale * 2,
                    w, h, r, t->shadow);
    nkfb_fill_round(s, x, y, w, h, r, t->panel_alt);
    nkfb_stroke_round(s, x, y, w, h, r, 1u, t->border);
    if (title != NULL) {
        nkfb_ui_label(ui, x + (int32_t)ui->gap, y + (int32_t)ui->gap / 2,
                      title, t->accent);
        nkfb_hline(s, x + (int32_t)ui->gap, y + (int32_t)title_h,
                   w - ui->gap * 2u, t->border);
    }
    return y + (int32_t)title_h + (int32_t)ui->gap / 2;
}

int32_t nkfb_ui_row(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                    const char *key, const char *value)
{
    const nkfb_theme_t *t;
    int32_t vw;

    if (ui == NULL || ui->surface == NULL) {
        return y;
    }
    t = ui->theme;
    nkfb_ui_label(ui, x, y, key, t->text_dim);
    if (value != NULL) {
        vw = (int32_t)nkfb_text_width(value, ui->scale);
        if (vw > (int32_t)(w / 2u)) {
            /* Keep long values from colliding with the key column. */
            int32_t max_chars = (int32_t)((w / 2u) / glyph_advance(ui));
            char cut[48];
            int32_t n = 0;

            while (value[n] != '\0' && n < max_chars && n < 45) {
                cut[n] = value[n];
                n++;
            }
            cut[n] = '\0';
            vw = (int32_t)nkfb_text_width(cut, ui->scale);
            nkfb_ui_label(ui, x + (int32_t)w - vw, y, cut, t->text);
        } else {
            nkfb_ui_label(ui, x + (int32_t)w - vw, y, value, t->text);
        }
    }
    return y + (int32_t)ui->line_h;
}

int32_t nkfb_ui_rowf(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                     const char *key, const char *fmt, ...)
{
    char value[48];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(value, sizeof(value), fmt, ap);
    va_end(ap);
    return nkfb_ui_row(ui, x, y, w, key, value);
}

void nkfb_ui_divider(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w)
{
    if (ui == NULL || ui->surface == NULL) {
        return;
    }
    nkfb_hline(ui->surface, x, y, w, ui->theme->border);
}

void nkfb_ui_badge(nkfb_ui_t *ui, int32_t x, int32_t y, const char *text,
                   uint32_t color)
{
    uint32_t pad = ui->scale * 3u;
    uint32_t w;
    uint32_t h;

    if (ui == NULL || ui->surface == NULL || text == NULL) {
        return;
    }
    w = nkfb_text_width(text, ui->scale) + pad * 2u;
    h = ui->line_h;
    nkfb_fill_round(ui->surface, x, y, w, h, h / 2u,
                    (color & 0x00ffffffu) | 0x33000000u);
    nkfb_ui_label(ui, x + (int32_t)pad, y, text, color);
}

uint32_t nkfb_ui_button(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, const char *text, int primary)
{
    const nkfb_theme_t *t;
    uint32_t fill;
    uint32_t ink;
    int32_t tw;

    if (ui == NULL || ui->surface == NULL || text == NULL) {
        return 0u;
    }
    t = ui->theme;
    fill = primary ? t->accent : t->button;
    ink = primary ? t->bg : t->text;
    nkfb_fill_round(ui->surface, x, y, w, h, ui->scale * 3u + 2u, fill);
    nkfb_stroke_round(ui->surface, x, y, w, h, ui->scale * 3u + 2u, 1u,
                      primary ? t->accent : t->border);
    tw = (int32_t)nkfb_text_width(text, ui->scale);
    nkfb_ui_label(ui, x + ((int32_t)w - tw) / 2,
                  y + ((int32_t)h - (int32_t)ui->line_h) / 2, text, ink);
    return w;
}

void nkfb_ui_progress(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                      uint32_t h, uint32_t percent, uint32_t color)
{
    uint32_t fill_w;

    if (ui == NULL || ui->surface == NULL || w == 0u || h == 0u) {
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    nkfb_fill_round(ui->surface, x, y, w, h, h / 2u, ui->theme->panel);
    nkfb_stroke_round(ui->surface, x, y, w, h, h / 2u, 1u, ui->theme->border);
    fill_w = (w - 2u) * percent / 100u;
    if (fill_w >= 2u) {
        nkfb_fill_round(ui->surface, x + 1, y + 1, fill_w, h - 2u,
                        (h > 2u) ? (h - 2u) / 2u : 1u, color);
    }
}

void nkfb_ui_list(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                  uint32_t row_h, const char *const *items, uint32_t count,
                  int32_t selected)
{
    const nkfb_theme_t *t;
    uint32_t i;

    if (ui == NULL || ui->surface == NULL || items == NULL || count == 0u) {
        return;
    }
    t = ui->theme;
    nkfb_fill_round(ui->surface, x, y, w,
                    (int32_t)row_h * (int32_t)count +
                        (int32_t)ui->gap / 2,
                    ui->scale * 2u + 2u, t->panel);
    for (i = 0u; i < count; i++) {
        int32_t ry = y + (int32_t)(i * row_h) + (int32_t)ui->gap / 4;
        int is_sel = ((int32_t)i == selected);

        if (is_sel) {
            nkfb_fill_round(ui->surface, x + 2, ry, w - 4u, row_h - 2u,
                            ui->scale * 2u, t->button_active);
            nkfb_fill(ui->surface, x + 2, ry + 2, ui->scale + 1u,
                      row_h - 6u, t->accent);
        }
        nkfb_ui_label(ui, x + (int32_t)ui->gap, ry, items[i],
                      is_sel ? t->text : t->text_dim);
    }
}

uint32_t nkfb_ui_table(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       const char *const *headers, uint32_t cols,
                       const char *const *cells, uint32_t rows)
{
    const nkfb_theme_t *t;
    uint32_t col_w;
    uint32_t i;
    uint32_t j;
    int32_t cy = y;

    if (ui == NULL || ui->surface == NULL || cols == 0u || headers == NULL) {
        return 0u;
    }
    t = ui->theme;
    col_w = w / cols;
    for (i = 0u; i < cols; i++) {
        nkfb_ui_label(ui, x + (int32_t)(i * col_w), cy, headers[i],
                      t->text_dim);
    }
    cy += (int32_t)ui->line_h;
    nkfb_hline(ui->surface, x, cy - (int32_t)ui->gap / 2, w, t->border);
    for (j = 0u; j < rows; j++) {
        if ((j % 2u) == 0u) {
            nkfb_fill(ui->surface, x - (int32_t)ui->gap / 2, cy - 1,
                      w + ui->gap, ui->line_h + 1u, t->panel_alt);
        }
        for (i = 0u; i < cols; i++) {
            const char *cell = cells[j * cols + i];

            nkfb_ui_label(ui, x + (int32_t)(i * col_w), cy,
                          (cell != NULL) ? cell : "", t->text);
        }
        cy += (int32_t)ui->line_h;
    }
    return (uint32_t)(cy - y);
}

void nkfb_ui_statusbar(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, const char *left, const char *right)
{
    const nkfb_theme_t *t;
    int32_t lx;
    int32_t rx;

    if (ui == NULL || ui->surface == NULL || w == 0u || h == 0u) {
        return;
    }
    t = ui->theme;
    nkfb_fill_round(ui->surface, x, y, w, h, ui->scale * 2u + 2u,
                    t->panel_alt);
    nkfb_stroke_round(ui->surface, x, y, w, h, ui->scale * 2u + 2u, 1u,
                      t->border);
    lx = x + (int32_t)ui->gap;
    nkfb_fill_round(ui->surface, lx,
                    y + (int32_t)(h / 2u) - (int32_t)(ui->scale + 1u),
                    ui->scale * 2u + 2u, ui->scale * 2u + 2u, ui->scale + 1u,
                    t->good);
    nkfb_ui_label(ui, lx + (int32_t)(ui->scale * 4u), y, left, t->text);
    if (right != NULL) {
        rx = x + (int32_t)w - (int32_t)nkfb_text_width(right, ui->scale) -
             (int32_t)ui->gap;
        if (rx > lx) {
            nkfb_ui_label(ui, rx, y, right, t->text_dim);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Composite panels.                                                   */
/* ------------------------------------------------------------------ */

static void format_uptime(const nkfb_gui_info_t *info, char *out, size_t cap)
{
    uint64_t hz = (info->tick_hz != 0u) ? info->tick_hz : 100u;
    uint64_t seconds = info->ticks / hz;
    unsigned hours = (unsigned)(seconds / 3600u);
    unsigned minutes = (unsigned)((seconds % 3600u) / 60u);
    unsigned secs = (unsigned)(seconds % 60u);

    (void)snprintf(out, cap, "%u:%02u:%02u", hours, minutes, secs);
}

static uint32_t rows_card_height(const nkfb_ui_t *ui, uint32_t rows)
{
    return ui->line_h + ui->gap / 2u + rows * ui->line_h + ui->gap;
}

void nkfb_gui_dashboard(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, const nkfb_gui_info_t *info)
{
    static const char *const shortcuts[] = {
        "Ctrl+L     clear",
        "Ctrl+C     cancel line",
        "Up / Down  history",
        "Tab        complete",
        "gui demo   elements",
    };
    char uptime[24];
    char buffer[32];
    int32_t cy = y;
    uint32_t card_h;
    uint32_t pct;
    int32_t content;
    const nkfb_theme_t *t;

    if (ui == NULL || ui->surface == NULL || info == NULL || w == 0u || h == 0u) {
        return;
    }
    t = ui->theme;
    format_uptime(info, uptime, sizeof(uptime));

    card_h = rows_card_height(ui, 4u);
    if (cy + (int32_t)card_h <= y + (int32_t)h) {
        content = nkfb_ui_card(ui, x, cy, w, card_h, "System");
        content = nkfb_ui_row(ui, x + (int32_t)ui->gap, content,
                              w - ui->gap * 2u, "ABI", info->abi);
        content = nkfb_ui_row(ui, x + (int32_t)ui->gap, content,
                              w - ui->gap * 2u, "Mode", info->mode);
        content = nkfb_ui_rowf(ui, x + (int32_t)ui->gap, content,
                               w - ui->gap * 2u, "Tasks", "%u", info->tasks);
        content = nkfb_ui_row(ui, x + (int32_t)ui->gap, content,
                              w - ui->gap * 2u, "Uptime", uptime);
        (void)content;
        cy += (int32_t)card_h + (int32_t)ui->gap;
    }

    card_h = rows_card_height(ui, 4u);
    if (cy + (int32_t)card_h <= y + (int32_t)h) {
        content = nkfb_ui_card(ui, x, cy, w, card_h, "Display");
        content = nkfb_ui_rowf(ui, x + (int32_t)ui->gap, content,
                               w - ui->gap * 2u, "Size", "%ux%u",
                               info->width, info->height);
        content = nkfb_ui_rowf(ui, x + (int32_t)ui->gap, content,
                               w - ui->gap * 2u, "Panes", "%ux%u",
                               info->cols, info->rows);
        content = nkfb_ui_rowf(ui, x + (int32_t)ui->gap, content,
                               w - ui->gap * 2u, "Depth", "%u bpp",
                               info->bpp);
        content = nkfb_ui_rowf(ui, x + (int32_t)ui->gap, content,
                               w - ui->gap * 2u, "Keys", "%u dev",
                               info->keyboards);
        (void)content;
        cy += (int32_t)card_h + (int32_t)ui->gap;
    }

    card_h = rows_card_height(ui, 2u) + ui->line_h;
    if (cy + (int32_t)card_h <= y + (int32_t)h) {
        int32_t bar_y;
        uint32_t bar_w = w - ui->gap * 2u;

        content = nkfb_ui_card(ui, x, cy, w, card_h, "Console");
        pct = (info->rows != 0u)
                  ? (info->used_rows * 100u) / info->rows
                  : 0u;
        if (pct > 100u) {
            pct = 100u;
        }
        (void)snprintf(buffer, sizeof(buffer), "%u%%", pct);
        content = nkfb_ui_row(ui, x + (int32_t)ui->gap, content, bar_w,
                              "Buffer", buffer);
        bar_y = content;
        nkfb_ui_progress(ui, x + (int32_t)ui->gap, bar_y + 2, bar_w,
                         ui->scale * 3u + 2u, pct, t->accent);
        cy += (int32_t)card_h + (int32_t)ui->gap;
    }

    card_h = ui->line_h + ui->gap / 2u + ui->line_h +
             (uint32_t)(sizeof(shortcuts) / sizeof(shortcuts[0])) *
                 ui->line_h +
             ui->gap * 2u;
    if (cy + (int32_t)card_h <= y + (int32_t)h) {
        content = nkfb_ui_card(ui, x, cy, w, card_h, "Shortcuts");
        (void)content;
        nkfb_ui_list(ui, x + (int32_t)ui->gap, cy + (int32_t)ui->line_h +
                                        (int32_t)ui->gap / 2,
                     w - ui->gap * 2u, ui->line_h,
                     shortcuts,
                     (uint32_t)(sizeof(shortcuts) / sizeof(shortcuts[0])), -1);
        cy += (int32_t)card_h;
    }
}

void nkfb_gui_showcase(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, const nkfb_gui_info_t *info)
{
    static const char *const headers[] = {"key", "state", "glyph"};
    static const char *const cells[] = {
        "l", "letter", "l",
        "s", "letter", "s",
        "Enter", "submit", "nl",
        "Up", "history", "esc[A",
        "Ctrl+L", "clear", "^L",
        "Tab", "complete", "->",
    };
    static const char *const actions[] = {
        "ABI v3 control plane",
        "SecureKernel policy online",
        "usb64 RAM disk attached",
    };
    char uptime[24];
    char tasks[24];
    char buffer[24];
    int32_t cx;
    int32_t cy;
    uint32_t gap = ui->gap;
    uint32_t col_w;
    uint32_t row_h = ui->line_h;
    uint32_t card_h;
    int32_t content;
    uint32_t pct;

    if (ui == NULL || ui->surface == NULL || info == NULL || w < 64u ||
        h < 64u) {
        return;
    }
    format_uptime(info, uptime, sizeof(uptime));
    (void)snprintf(tasks, sizeof(tasks), "%u", info->tasks);
    pct = (info->rows != 0u) ? (info->used_rows * 100u) / info->rows : 0u;
    if (pct > 100u) {
        pct = 100u;
    }
    (void)snprintf(buffer, sizeof(buffer), "%u%%", pct);

    nkfb_fill(ui->surface, x, y, w, h, ui->theme->bg);
    nkfb_ui_window(ui, x + (int32_t)gap, y + (int32_t)gap,
                   w - gap * 2u, h - gap * 2u,
                   (info->title != NULL) ? info->title : "null_kernel",
                   info->subtitle);

    col_w = (w >= 620u) ? (w - gap * 4u) / 2u : (w - gap * 2u);
    cx = x + (int32_t)gap * 2;
    cy = y + (int32_t)gap * 2 + (int32_t)ui->line_h + (int32_t)gap;
    card_h = rows_card_height(ui, 3u) + row_h;
    content = nkfb_ui_card(ui, cx, cy, col_w, card_h, "System");
    content = nkfb_ui_row(ui, cx + (int32_t)gap, content,
                          col_w - gap * 2u, "ABI", info->abi);
    content = nkfb_ui_row(ui, cx + (int32_t)gap, content,
                          col_w - gap * 2u, "Mode", info->mode);
    content = nkfb_ui_row(ui, cx + (int32_t)gap, content,
                          col_w - gap * 2u, "Uptime", uptime);
    content = nkfb_ui_row(ui, cx + (int32_t)gap, content,
                          col_w - gap * 2u, "Tasks", tasks);

    cy += (int32_t)card_h + (int32_t)gap;
    card_h = rows_card_height(ui, 2u) + row_h;
    content = nkfb_ui_card(ui, cx, cy, col_w, card_h, "Console");
    content = nkfb_ui_rowf(ui, cx + (int32_t)gap, content, col_w - gap * 2u,
                           "Grid", "%ux%u @ %u bpp", info->cols, info->rows,
                           info->bpp);
    content = nkfb_ui_row(ui, cx + (int32_t)gap, content, col_w - gap * 2u,
                          "Buffer", buffer);
    nkfb_ui_progress(ui, cx + (int32_t)gap, content + 3,
                     col_w - gap * 2u, ui->scale * 3u + 2u, pct,
                     ui->theme->accent);

    cy += (int32_t)card_h + (int32_t)gap;
    if (cy + (int32_t)(row_h * 4u) < y + (int32_t)h - (int32_t)(gap * 3u)) {
        uint32_t badge_w = nkfb_text_width("READY", ui->scale) + gap;

        card_h = row_h * 4u + gap;
        nkfb_ui_card(ui, cx, cy, col_w, card_h, "Actions");
        nkfb_ui_badge(ui, cx + (int32_t)(col_w - badge_w) - (int32_t)gap,
                      cy + (int32_t)gap / 2, "READY", ui->theme->good);
        (void)nkfb_ui_button(ui, cx + (int32_t)gap, cy + (int32_t)row_h * 2,
                             col_w / 2u - gap * 3u / 2u, row_h * 2u - gap,
                             "Run", 1);
        (void)nkfb_ui_button(ui, cx + (int32_t)gap * 2 +
                                 (int32_t)(col_w / 2u - gap * 3u / 2u),
                             cy + (int32_t)row_h * 2,
                             col_w / 2u - gap * 3u / 2u, row_h * 2u - gap,
                             "Cancel", 0);
    }

    cy += (int32_t)card_h + (int32_t)gap;
    if (cy + (int32_t)(row_h * 3u) < y + (int32_t)h - (int32_t)(gap * 4u)) {
        const uint32_t swatches[5] = {
            ui->theme->accent, ui->theme->good, ui->theme->warn,
            ui->theme->bad, ui->theme->text_dim,
        };
        uint32_t sw = row_h - gap / 2u;
        uint32_t i;

        card_h = row_h * 2u + gap;
        content = nkfb_ui_card(ui, cx, cy, col_w, card_h, "Theme");
        for (i = 0u; i < 5u; i++) {
            int32_t sx = cx + (int32_t)gap +
                         (int32_t)(i * (sw + gap / 2u));

            nkfb_fill_round(ui->surface, sx, content + (int32_t)gap / 2, sw, sw,
                            sw / 3u, swatches[i]);
        }
        (void)content;
    }

    if (w >= 620u) {
        int32_t rx = x + (int32_t)gap * 3 + (int32_t)col_w;

        cy = y + (int32_t)gap * 2 + (int32_t)ui->line_h + (int32_t)gap;
        card_h = row_h * 8u;
        content = nkfb_ui_card(ui, rx, cy, col_w, card_h, "Key map");
        (void)content;
        (void)nkfb_ui_table(ui, rx + (int32_t)gap, cy + (int32_t)row_h +
                                            (int32_t)gap / 2,
                            col_w - gap * 2u, headers, 3u, cells,
                            6u);
        cy += (int32_t)card_h + (int32_t)gap;
        card_h = row_h * 5u + gap;
        content = nkfb_ui_card(ui, rx, cy, col_w, card_h, "Services");
        (void)content;
        nkfb_ui_list(ui, rx + (int32_t)gap,
                     content + (int32_t)gap / 2, col_w - gap * 2u, row_h,
                     actions, 3u, 1);
    }

    if (h > gap * 8u) {
        nkfb_ui_statusbar(ui, x + (int32_t)gap,
                          y + (int32_t)h - (int32_t)gap * 3 -
                              (int32_t)ui->line_h,
                          w - gap * 2u, ui->line_h + gap,
                          "press any key to return to nk)", "ABI v3 GUI");
    }
}
