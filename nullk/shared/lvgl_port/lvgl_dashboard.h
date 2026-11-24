/*
 * lvgl_dashboard - the gothicOS shell surface drawn with LVGL.
 *
 * One implementation, two consumers: the host simulator and the ring-3
 * desktop task. The layout is derived from the active screen size, so it works
 * on a 1024x768 QEMU framebuffer as well as on the simulator's 1280x800.
 */
#ifndef NULLK_LVGL_DASHBOARD_H
#define NULLK_LVGL_DASHBOARD_H

#include "lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the dashboard on the active screen. Safe to call once after
 * lvgl_port_display_init(). */
void lvgl_dashboard_create(void);

/* 0 = dashboard, 1 = terminal. Also callable before the user clicks a tab. */
void lvgl_dashboard_show_tab(uint32_t index);

/* The dev console pane (see lvgl_terminal.h), or NULL before create(). */
lv_obj_t *lvgl_dashboard_terminal(void);

/* Appends a line to the terminal tab. Cheap enough for a per-frame log. */
void lvgl_dashboard_log(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* Updates the footer's input counter. Lets a port prove that kernel input
 * events actually reached LVGL without changing anything else on screen. */
void lvgl_dashboard_set_input_count(uint32_t events);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LVGL_DASHBOARD_H */
