/*
 * InterfaceKit demo - a ring-3 graphical client for InterfaceKit.
 *
 * This task never touches the framebuffer or the PS/2 controller. It waits for
 * the UI server, creates one window, renders into the back buffer handed to it
 * by the server, and consumes routed input events through the InterfaceKit API.
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/printk.h"
#include "nullk/interfacekit.h"
#include "nullk/interfacekit_server.h"
#include "nullk/interfacekit_widgets.h"

#define DEMO_W 240u
#define DEMO_H 128u
#define TEXT_MAX 20u

static void append_char(char *text, uint32_t *len, char c)
{
    if (*len + 1u >= TEXT_MAX) {
        return;
    }
    text[(*len)++] = c;
    text[*len] = '\0';
}

static void handle_events(uint32_t window, char *text, uint32_t *len,
                          int32_t *mx, int32_t *my, uint32_t *pressed)
{
    nk64_input_event_t event;

    while (interfacekit_window_pop_event(window, &event) != 0u) {
        if (event.type == (uint32_t)NK64_INPUT_MOUSE_MOVE ||
            event.type == (uint32_t)NK64_INPUT_MOUSE_DOWN ||
            event.type == (uint32_t)NK64_INPUT_MOUSE_UP) {
            if (event.dx < 0) {
                *mx = 0;
            } else if ((uint32_t)event.dx >= DEMO_W) {
                *mx = (int32_t)DEMO_W - 1;
            } else {
                *mx = event.dx;
            }
            if (event.dy < 0) {
                *my = 0;
            } else if ((uint32_t)event.dy >= DEMO_H) {
                *my = (int32_t)DEMO_H - 1;
            } else {
                *my = event.dy;
            }
            if (event.type == (uint32_t)NK64_INPUT_MOUSE_DOWN) {
                *pressed = 1u;
            } else if (event.type == (uint32_t)NK64_INPUT_MOUSE_UP) {
                *pressed = 0u;
            }
            continue;
        }

        if (event.type != (uint32_t)NK64_INPUT_KEY_DOWN) {
            continue;
        }
        if (event.code == (uint32_t)NK64_KEY_BACKSPACE) {
            if (*len != 0u) {
                (*len)--;
                text[*len] = '\0';
            }
            continue;
        }
        if (event.code == (uint32_t)NK64_KEY_ENTER) {
            *len = 0u;
            text[0] = '\0';
            continue;
        }
        if (event.code >= 0x20u && event.code < 0x7fu) {
            append_char(text, len, (char)event.code);
        }
    }
}

static void draw(interfacekit_pixmap_t *pm, const char *text, int32_t mx, int32_t my,
                 uint32_t pressed, uint32_t frame)
{
    interfacekit_style_t style = interfacekit_style_default();
    interfacekit_rgba_t accent = interfacekit_rgba(90u, 170u, 255u, 255u);
    int32_t dot_x;

    if (pm == (interfacekit_pixmap_t *)0) {
        return;
    }

    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){0, 0, DEMO_W, DEMO_H},
                             interfacekit_rgba(12u, 18u, 32u, 255u));
    interfacekit_widget_panel(pm,
                              (interfacekit_rect_t){2, 2, DEMO_W - 4u,
                                                    DEMO_H - 4u}, &style);
    interfacekit_pixmap_text(pm, 8, 8, "INTERFACEKIT", accent, 2u);
    interfacekit_widget_label(pm, (interfacekit_rect_t){8, 28, 174u, 12u},
                              text, &style);
    interfacekit_widget_button(pm, (interfacekit_rect_t){186, 26, 44u, 16u},
                               "SEND", pressed, &style);
    interfacekit_widget_progress(pm, (interfacekit_rect_t){8, 48, 222u, 10u},
                                 (frame * 3u) % 101u, &style);

#if INTERFACEKIT_HAS_TOGGLE
    interfacekit_widget_toggle(pm, (interfacekit_rect_t){8, 68, 26u, 12u},
                               (frame / 32u) & 1u, &style);
#endif
#if INTERFACEKIT_HAS_SLIDER
    interfacekit_widget_slider(pm, (interfacekit_rect_t){42, 68, 188u, 12u},
                               (frame * 2u) % 101u, &style);
#endif

    dot_x = (int32_t)(frame % (DEMO_W - 8u)) + 4;
    interfacekit_pixmap_fill(pm, (interfacekit_rect_t){dot_x, 90, 6u, 6u},
                             interfacekit_rgba(245u, 120u, 80u, 255u));

    if (pressed != 0u) {
        interfacekit_pixmap_fill(pm, (interfacekit_rect_t){mx - 2, my - 2, 5u, 5u},
                                 interfacekit_rgba(255u, 230u, 90u, 255u));
    } else {
        interfacekit_pixmap_fill(pm, (interfacekit_rect_t){mx - 1, my - 1, 3u, 3u},
                                 interfacekit_rgba(240u, 240u, 255u, 255u));
    }
}

void interfacekit_demo_entry(void)
{
    char text[TEXT_MAX];
    uint32_t len;
    uint32_t window;
    uint32_t frame = 0u;
    uint32_t pressed = 0u;
    int32_t mx = (int32_t)(DEMO_W / 2u);
    int32_t my = (int32_t)(DEMO_H / 2u);

    while (interfacekit_server_ready() == 0u) {
        (void)nk64_sleep(1u);
        (void)nk64_yield();
    }

    text[0] = 'H';
    text[1] = 'E';
    text[2] = 'L';
    text[3] = 'L';
    text[4] = 'O';
    text[5] = '\0';
    len = 5u;

    window = interfacekit_window_create("InterfaceKit Demo", 32, 28, DEMO_W, DEMO_H);
    if (window == 0u) {
        nk_printf("[InterfaceKitDemo] window create failed\n");
        nk64_exit(1);
    }
    nk_printf("[InterfaceKitDemo] window id=%u ready\n", window);

    for (;;) {
        handle_events(window, text, &len, &mx, &my, &pressed);
        draw(interfacekit_window_backbuffer(window), text, mx, my, pressed, frame);
        (void)interfacekit_window_present(window);
        frame++;
        (void)nk64_sleep(2u);
        (void)nk64_yield();
    }
}
