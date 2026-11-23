/* Small GUI engine for the null_kernel Linux CLI framebuffer.
 *
 * The Linux port needs more than a character grid: status cards, tables,
 * progress bars, badges, buttons, lists and window chrome make diagnostics and
 * future gothicOS userland panels readable on the scanout. This is a widget
 * toolkit, not a compositor: every call paints immediately into a
 * nkfb_surface_t, keeps no global state, and needs neither InterfaceKit nor an
 * input loop. The console owns the framebuffer and decides when to paint.
 */
#ifndef NULLK_LINUX_FB_GUI_H
#define NULLK_LINUX_FB_GUI_H

#include <stdint.h>

#include "nullk_linux/fb_gfx.h"

typedef struct {
    uint32_t bg;
    uint32_t panel;
    uint32_t panel_alt;
    uint32_t border;
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;
    uint32_t accent_dim;
    uint32_t good;
    uint32_t warn;
    uint32_t bad;
    uint32_t shadow;
    uint32_t button;
    uint32_t button_active;
} nkfb_theme_t;

typedef struct {
    nkfb_surface_t *surface;
    const nkfb_theme_t *theme;
    uint32_t scale;      /* font scale */
    uint32_t line_h;     /* one text line plus leading */
    uint32_t gap;        /* padding unit derived from scale */
} nkfb_ui_t;

/* Live values shown by dashboards. The console fills this from FB_INFO and
 * GETINFO; nothing here is invented. */
typedef struct {
    const char *title;
    const char *subtitle;
    const char *abi;
    const char *mode;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t cols;
    uint32_t rows;
    uint32_t used_rows;
    uint32_t keyboards;
    uint32_t tasks;
    uint32_t tick_hz;
    uint64_t ticks;
} nkfb_gui_info_t;

extern const nkfb_theme_t nkfb_theme_dark;

void nkfb_ui_init(nkfb_ui_t *ui, nkfb_surface_t *surface,
                  const nkfb_theme_t *theme, uint32_t scale);
uint32_t nkfb_ui_line(const nkfb_ui_t *ui);
uint32_t nkfb_ui_gap(const nkfb_ui_t *ui);

/* Window chrome with a gradient title bar and a soft drop shadow. */
void nkfb_ui_window(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                    uint32_t h, const char *title, const char *subtitle);
/* Rounded card with a section title; returns the y of the first content row. */
int32_t nkfb_ui_card(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                     uint32_t h, const char *title);
void nkfb_ui_label(nkfb_ui_t *ui, int32_t x, int32_t y, const char *text,
                   uint32_t color);
/* Key/value line, right-aligning the value; returns the next row's y. */
int32_t nkfb_ui_row(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                    const char *key, const char *value);
int32_t nkfb_ui_rowf(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                     const char *key, const char *fmt, ...);
void nkfb_ui_divider(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w);
void nkfb_ui_badge(nkfb_ui_t *ui, int32_t x, int32_t y, const char *text,
                   uint32_t color);
uint32_t nkfb_ui_button(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, const char *text, int primary);
void nkfb_ui_progress(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                      uint32_t h, uint32_t percent, uint32_t color);
void nkfb_ui_list(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                  uint32_t row_h, const char *const *items, uint32_t count,
                  int32_t selected);
uint32_t nkfb_ui_table(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       const char *const *headers, uint32_t cols,
                       const char *const *cells, uint32_t rows);
void nkfb_ui_statusbar(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, const char *left, const char *right);

/* Console side panel: system, display and console telemetry plus shortcuts. */
void nkfb_gui_dashboard(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                        uint32_t h, const nkfb_gui_info_t *info);
/* Full-surface element showcase behind `gui demo`. */
void nkfb_gui_showcase(nkfb_ui_t *ui, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, const nkfb_gui_info_t *info);

#endif /* NULLK_LINUX_FB_GUI_H */
