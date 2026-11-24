/*
 * lvgl_terminal - a small, generic log console widget for LVGL ports.
 *
 * Nothing here knows about null_kernel, gothicOS or the dashboard: it is a
 * scrolling text pane with a title bar that any LVGL program (the ring-3
 * desktop task, the host simulator, a board bring-up demo) can create and
 * append lines to. It exists so a port has somewhere to show what the kernel
 * and the task are doing while there is no serial console in sight.
 */
#ifndef NULLK_LVGL_TERMINAL_H
#define NULLK_LVGL_TERMINAL_H

#include <stdint.h>

#include "lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the pane at (x, y) with size (w, h) inside parent and returns it.
 * All later calls take the returned object. */
lv_obj_t *lvgl_terminal_create(lv_obj_t *parent, int32_t x, int32_t y,
                               int32_t w, int32_t h);

/* Title shown in the pane's header row. Copied, so `title` may be temporary. */
void lvgl_terminal_set_title(lv_obj_t *terminal, const char *title);

/* Appends one line and scrolls to it. The oldest line is dropped once the
 * ring is full, so memory use is bounded by the pane's size. */
void lvgl_terminal_log(lv_obj_t *terminal, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Same, taking a prepared line. */
void lvgl_terminal_log_line(lv_obj_t *terminal, const char *text);

/* Empties the pane, keeping the title. */
void lvgl_terminal_clear(lv_obj_t *terminal);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LVGL_TERMINAL_H */
