/*
 * lvgl64 - ring-3 LVGL desktop task.
 *
 * This is the bare-metal ("embedded") display path: the task claims the sole
 * framebuffer mapping with NK64_SC_FB_INFO / NK64_SC_FB_MAP, renders the
 * gothicOS shell with LVGL through nullk/shared/lvgl_port, and feeds PS/2
 * events from NK64_SC_INPUT_READ into the LVGL input devices.
 *
 * The same source builds for the Linux port, where the ABI v3 shim implements
 * those three calls over /dev/fb0 and evdev, so an unmodified task runs as a
 * hosted libnk application.
 */
#include <stdint.h>

#include "libnk64.h"
#include "lvgl_dashboard.h"
#include "lvgl_port.h"
#include "nullk/interfacekit.h"
#include "nullk/printk.h"

/* One tenth of a 1280x1024 screen; lvgl_port rejects smaller buffers. */
#define LVGL_DRAW_BYTES (512u * 1024u)
#define LVGL_POLL_MAX 16u

static uint8_t g_draw[LVGL_DRAW_BYTES] __attribute__((aligned(64)));
static interfacekit_framebuffer_t g_fb;
static uint32_t g_tick_hz = 100u;
static uint32_t g_tick_origin;

static uint32_t lvgl_tick_ms(void)
{
    uint64_t ticks = nk64_ticks();

    if (g_tick_hz == 0u) {
        return (uint32_t)ticks;
    }
    return (uint32_t)((ticks * 1000u) / g_tick_hz);
}

static int push_input(const nk64_input_event_t *event)
{
    lvgl_port_event_t out;

    nk_memset(&out, 0, sizeof(out));
    out.x = event->x;
    out.y = event->y;
    out.buttons = event->buttons;

    switch (event->type) {
    case NK64_INPUT_KEY_DOWN:
        out.type = LVGL_PORT_KEY_DOWN;
        out.key = event->code;
        out.pressed = 1;
        break;
    case NK64_INPUT_KEY_UP:
        out.type = LVGL_PORT_KEY_UP;
        out.key = event->code;
        break;
    case NK64_INPUT_MOUSE_MOVE:
        out.type = LVGL_PORT_POINTER_MOVE;
        break;
    case NK64_INPUT_MOUSE_DOWN:
        out.type = LVGL_PORT_POINTER_DOWN;
        break;
    case NK64_INPUT_MOUSE_UP:
        out.type = LVGL_PORT_POINTER_UP;
        break;
    default:
        return 0;
    }
    return lvgl_port_push_event(&out) == 0 ? 1 : 0;
}

static void idle_forever(const char *reason)
{
    nk_printf("[lvgl64] %s; desktop disabled\n", reason);
    for (;;) {
        (void)nk64_sleep(100u);
        (void)nk64_yield();
    }
}

void lvgl_desktop_entry(void)
{
    nk64_fbinfo_t info;
    nk64_info_t kernel;
    uint64_t map;
    uint32_t pushed = 0;
    uint32_t frames = 0;

    nk_memset(&info, 0, sizeof(info));
    if (nk64_fb_info(&info) != 0u || info.present == 0u) {
        idle_forever("no framebuffer from the kernel");
    }
    if (info.bpp != 24u && info.bpp != 32u) {
        idle_forever("unsupported framebuffer depth");
    }
    if ((uint64_t)info.width * (info.bpp / 8u) > (uint64_t)info.pitch) {
        idle_forever("inconsistent framebuffer pitch");
    }
    if ((uint32_t)info.width * (uint32_t)info.height / 10u * 4u > LVGL_DRAW_BYTES) {
        idle_forever("framebuffer larger than the static draw buffer");
    }

    map = nk64_fb_map();
    if (map == 0u) {
        idle_forever("framebuffer already claimed or unmappable");
    }

    nk_memset(&kernel, 0, sizeof(kernel));
    if (nk64_getinfo(&kernel) == 0u && kernel.tick_hz != 0u) {
        g_tick_hz = kernel.tick_hz;
    }
    g_tick_origin = lvgl_tick_ms();

    interfacekit_framebuffer_init(&g_fb, (void *)(uintptr_t)map, info.width,
                                  info.height, info.pitch, (uint8_t)info.bpp,
                                  info.red_size, info.red_shift,
                                  info.green_size, info.green_shift,
                                  info.blue_size, info.blue_shift);

    lvgl_port_set_tick_cb(lvgl_tick_ms);
    if (lvgl_port_display_init(&g_fb, g_draw, sizeof(g_draw)) == NULL) {
        idle_forever("lvgl_port_display_init failed");
    }
    lvgl_dashboard_create();
    lvgl_dashboard_log("null_kernel LVGL desktop, LVGL %d.%d.%d",
                       (int)LVGL_VERSION_MAJOR, (int)LVGL_VERSION_MINOR,
                       (int)LVGL_VERSION_PATCH);
    lvgl_dashboard_log("framebuffer %ux%u %ubpp, pitch %u",
                       (uint32_t)info.width, (uint32_t)info.height,
                       (uint32_t)info.bpp, (uint32_t)info.pitch);
    lvgl_dashboard_log("tick: %u Hz from NK64_SC_TICKS", (uint32_t)g_tick_hz);
    lvgl_dashboard_log("input: NK64_SC_INPUT_READ, PS/2 mouse+keyboard");

    nk_printf("[lvgl64] framebuffer %ux%u %ubpp pitch=%u, tick=%uHz, "
              "LVGL %d.%d.%d online\n",
              (uint32_t)info.width, (uint32_t)info.height, (uint32_t)info.bpp,
              (uint32_t)info.pitch, (uint32_t)g_tick_hz,
              (int)LVGL_VERSION_MAJOR, (int)LVGL_VERSION_MINOR,
              (int)LVGL_VERSION_PATCH);
    (void)g_tick_origin;

    for (;;) {
        nk64_input_event_t events[LVGL_POLL_MAX];
        uint32_t count = nk64_input_read(events, LVGL_POLL_MAX);
        uint32_t i;

        for (i = 0; i < count; i++) {
            lvgl_port_event_t probe;

            pushed += (uint32_t)push_input(&events[i]);
            /* A few early events are echoed into the console so the tab proves
             * the kernel input path without a serial cable. */
            if (pushed <= 8u) {
                nk_memset(&probe, 0, sizeof(probe));
                lvgl_dashboard_log("event %u: type %u at %d,%d code 0x%x",
                                   (unsigned)pushed,
                                   (unsigned)events[i].type,
                                   (int)events[i].x, (int)events[i].y,
                                   (unsigned)events[i].code);
            }
        }
        if (count != 0u) {
            lvgl_dashboard_set_input_count(pushed);
        }
        (void)lvgl_port_pump();
        frames++;
        if ((frames % 200u) == 0u) {
            int32_t px = 0;
            int32_t py = 0;

            lvgl_port_last_pointer(&px, &py, NULL);
            nk_printf("[lvgl64] %u frames, %u input events pushed, "
                      "%u consumed, cursor %d,%d\n",
                      (uint32_t)frames, (uint32_t)pushed,
                      (uint32_t)lvgl_port_events_consumed(),
                      (int)px, (int)py);
            lvgl_dashboard_log("%u frames, %u events (cursor %d,%d)",
                               (unsigned)frames, (unsigned)pushed, (int)px,
                               (int)py);
        }
        (void)nk64_sleep(1u);
        (void)nk64_yield();
    }
}
