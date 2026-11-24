/*
 * LVGL host simulator for null_kernel.
 *
 * Renders the shared gothicOS dashboard (nullk/shared/lvgl_port/lvgl_dashboard.c)
 * through the real port layer into an interfacekit_framebuffer_t, exactly as
 * the ring-3 desktop task does into its mapped framebuffer, then writes a PPM.
 * No QEMU, kernel or display needed.
 *
 * Usage: lvgl_sim [out.ppm] [width] [height] [tab]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lvgl_dashboard.h"
#include "lvgl_port.h"
#include "nullk/interfacekit.h"

#define MAX_W 1280
#define MAX_H 800

static uint8_t g_pixels[MAX_W * MAX_H * 4u] __attribute__((aligned(16)));
static uint8_t g_draw[MAX_W * (MAX_H / 10) * 4u] __attribute__((aligned(16)));
static uint32_t g_tick_ms;

static uint32_t fake_tick(void)
{
    return g_tick_ms;
}

static int write_ppm(const char *path, uint32_t width, uint32_t height)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL) {
        return -1;
    }
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (uint32_t i = 0; i < width * height; i++) {
        const uint8_t *p = g_pixels + (size_t)i * 4u;
        uint8_t rgb[3] = {p[2], p[1], p[0]};

        fwrite(rgb, 1, sizeof(rgb), file);
    }
    fclose(file);
    return 0;
}

int main(int argc, char **argv)
{
    interfacekit_framebuffer_t fb;
    lvgl_port_event_t move = {0};
    lvgl_port_event_t press = {0};
    lvgl_port_event_t key = {0};
    const char *out = argc > 1 ? argv[1] : "dashboard.ppm";
    uint32_t width = argc > 2 ? (uint32_t)atoi(argv[2]) : MAX_W;
    uint32_t height = argc > 3 ? (uint32_t)atoi(argv[3]) : MAX_H;
    uint32_t tab = argc > 4 ? (uint32_t)atoi(argv[4]) : 0u;

    if (width > MAX_W || height > MAX_H || width < 320 || height < 240) {
        fprintf(stderr, "size must be within 320x240 .. %dx%d\n", MAX_W, MAX_H);
        return 1;
    }

    interfacekit_framebuffer_init(&fb, g_pixels, width, height, width * 4u, 32,
                                  8, 16, 8, 8, 8, 0);
    lvgl_port_set_tick_cb(fake_tick);
    if (lvgl_port_display_init(&fb, g_draw, sizeof(g_draw)) == NULL) {
        fprintf(stderr, "lvgl_port_display_init failed\n");
        return 1;
    }

    lvgl_dashboard_create();
    lvgl_dashboard_log("lvgl_sim host renderer, LVGL %d.%d",
                       (int)LVGL_VERSION_MAJOR, (int)LVGL_VERSION_MINOR);
    lvgl_dashboard_log("framebuffer %ux%u, 32bpp", (unsigned)width,
                       (unsigned)height);
    lvgl_dashboard_log("tick callback: fake, +33 ms per frame");
    lvgl_dashboard_log("terminal is a generic lvgl_terminal pane");

    /* Exercise the input path: a pointer move, a click, and a key press. */
    move.type = LVGL_PORT_POINTER_MOVE;
    move.x = 300;
    move.y = 300;
    press.type = LVGL_PORT_POINTER_DOWN;
    press.x = 300;
    press.y = 300;
    key.type = LVGL_PORT_KEY_DOWN;
    key.key = 0x103u;
    key.pressed = 1;
    lvgl_port_push_event(&move);
    lvgl_port_push_event(&press);
    lvgl_port_push_event(&key);

    for (int frame = 0; frame < 12; frame++) {
        g_tick_ms += 33;
        lvgl_port_pump();
    }

    lvgl_dashboard_log("pointer moved to 300,300");
    lvgl_dashboard_log("pointer pressed at 300,300");
    lvgl_dashboard_log("key 0x103 (TAB) down");
    lvgl_dashboard_log("12 frames rendered, %u events consumed",
                       (unsigned)lvgl_port_events_consumed());
    lvgl_dashboard_show_tab(tab);
    g_tick_ms += 33;
    lvgl_port_pump();

    printf("rendered %ux%u tab=%u, %u input events consumed, pointer=%u keypad=%u\n",
           width, height, (unsigned)tab, (unsigned)lvgl_port_events_consumed(),
           (unsigned)(lvgl_port_pointer() != NULL),
           (unsigned)(lvgl_port_keypad() != NULL));

    if (write_ppm(out, width, height) != 0) {
        fprintf(stderr, "could not write %s\n", out);
        return 1;
    }
    return 0;
}
