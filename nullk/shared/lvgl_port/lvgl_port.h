/*
 * lvgl_port - LVGL 9 front end for null_kernel's linear framebuffers.
 *
 * This is the board-neutral seam between LVGL and the kernel: LVGL never sees
 * Limine, an ABI header or a syscall. It renders into its own XRGB8888 draw
 * buffer and the port packs those pixels into whatever pixel layout the
 * interfacekit_framebuffer_t describes. The same code therefore serves the
 * AMD64 Limine framebuffer, the Linux port's /dev/fb0 and a host simulator.
 *
 * Input arrives as plain lvgl_port_event_t records. On AMD64 a task turns
 * nk64_input_event_t into these with lvgl_port_event_from_nk64(); the Linux
 * port feeds the same records from evdev.
 *
 * Everything here is single-threaded: one task calls lvgl_port_pump() from its
 * main loop. LVGL's own timer list decides when the next refresh is due.
 */
#ifndef NULLK_LVGL_PORT_H
#define NULLK_LVGL_PORT_H

#include <stdint.h>

#include "nullk/interfacekit.h"

#ifndef LV_CONF_INCLUDE_SIMPLE
#define LV_CONF_INCLUDE_SIMPLE
#endif
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_PORT_EVENT_QUEUE 32u

/* Monotonic milliseconds. Supplied by the task: ABI v3 ticks, CLOCK_MONOTONIC
 * on Linux, or a fake counter in the host simulator. */
typedef uint32_t (*lvgl_port_tick_fn)(void);

typedef enum {
    LVGL_PORT_KEY_DOWN = 1u,
    LVGL_PORT_KEY_UP = 2u,
    LVGL_PORT_POINTER_MOVE = 3u,
    LVGL_PORT_POINTER_DOWN = 4u,
    LVGL_PORT_POINTER_UP = 5u
} lvgl_port_event_type_t;

typedef struct {
    uint32_t type;      /* lvgl_port_event_type_t */
    uint32_t key;       /* ASCII, or an NK64_KEY_* style special code */
    int32_t x;          /* absolute pointer position */
    int32_t y;
    uint32_t buttons;   /* bit 0 left, bit 1 right, bit 2 middle */
    uint8_t pressed;    /* key events: 1 down, 0 up */
    uint8_t reserved[3];
} lvgl_port_event_t;

/*
 * Create the LVGL display for `fb` and register the pointer and keypad
 * devices. `draw_buffer` is borrowed for the lifetime of the display and
 * should hold at least one tenth of the screen in XRGB8888. Returns NULL when
 * the framebuffer has no usable geometry or the draw buffer is too small.
 */
lv_display_t *lvgl_port_display_init(const interfacekit_framebuffer_t *fb,
                                     void *draw_buffer,
                                     uint32_t draw_buffer_bytes);

void lvgl_port_set_tick_cb(lvgl_port_tick_fn fn);

/* Milliseconds since boot, as seen by LVGL. Falls back to 0 without a callback. */
uint32_t lvgl_port_tick_ms(void);

/* Keypad focus group (buttons, checkboxes, lists). Created on first call. */
lv_group_t *lvgl_port_group(void);

lv_indev_t *lvgl_port_pointer(void);
lv_indev_t *lvgl_port_keypad(void);

/* Queue one event. Returns 0 on success, -1 when the queue is full. */
int lvgl_port_push_event(const lvgl_port_event_t *event);

/* Run LVGL's timers and refresh. Returns ms until LVGL next needs attention. */
uint32_t lvgl_port_pump(void);

/* Events consumed by the input devices so far, for diagnostics and tests. */
uint32_t lvgl_port_events_consumed(void);

/* Last pointer position and button state handed to LVGL, for diagnostics. */
void lvgl_port_last_pointer(int32_t *x, int32_t *y, uint32_t *buttons);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LVGL_PORT_H */
