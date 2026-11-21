/*
 * InterfaceKit - graphical UI server for the AMD64 port.
 *
 * The kernel only hands us a linear framebuffer mapping and an input event
 * ring. This service owns all drawing: it keeps a bounded pixel pool, manages
 * opaque windows with front/back buffers, routes input to the topmost window,
 * and composites a desktop. Client tasks call the InterfaceKit API from the same
 * address space (see include/nullk/interfacekit_server.h).
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/printk.h"
#include "nullk/interfacekit.h"
#include "nullk/interfacekit_server.h"

#define INTERFACEKIT_TASKBAR_H   28u
#define INTERFACEKIT_BORDER      2u
#define INTERFACEKIT_TITLE_H     18u
#define INTERFACEKIT_MAX_W       480u
#define INTERFACEKIT_MAX_H       320u

typedef struct {
    uint32_t id;
    char title[INTERFACEKIT_TITLE_MAX];
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z;
    uint8_t visible;
    uint8_t dirty;
    interfacekit_pixmap_t front;
    interfacekit_pixmap_t back;
    nk64_input_event_t events[INTERFACEKIT_EVENT_QUEUE];
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;
} interfacekit_window_t;

static uint8_t g_pool[INTERFACEKIT_POOL_BYTES] __attribute__((aligned(16)));
static uint32_t g_pool_used;
static interfacekit_window_t g_windows[INTERFACEKIT_MAX_WINDOWS];
static uint32_t g_next_id = 1u;
static uint32_t g_z;
static uint32_t g_ready;
static uint32_t g_focused;
static int32_t g_cursor_x;
static int32_t g_cursor_y;
static interfacekit_framebuffer_t g_fb;
static uint32_t g_fb_ok;

static void copy_string(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0u;

    if (dst == 0 || cap == 0u) {
        return;
    }
    if (src != 0) {
        while (i + 1u < cap && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static uint8_t *pool_alloc(uint32_t bytes)
{
    uint8_t *result;

    bytes = (bytes + 15u) & ~15u;
    if (bytes == 0u || bytes > INTERFACEKIT_POOL_BYTES - g_pool_used) {
        return (uint8_t *)0;
    }
    result = &g_pool[g_pool_used];
    g_pool_used += bytes;
    return result;
}

uint32_t interfacekit_server_ready(void)
{
    return g_ready;
}

uint32_t interfacekit_desktop_width(void)
{
    return g_fb_ok != 0u ? g_fb.width : 0u;
}

uint32_t interfacekit_desktop_height(void)
{
    return g_fb_ok != 0u ? g_fb.height : 0u;
}

uint32_t interfacekit_window_count(void)
{
    uint32_t i;
    uint32_t count = 0u;

    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0u) {
            count++;
        }
    }
    return count;
}

uint32_t interfacekit_window_create(const char *title, int32_t x, int32_t y,
                          uint32_t width, uint32_t height)
{
    interfacekit_window_t *window = (interfacekit_window_t *)0;
    uint32_t i;
    uint32_t bytes;
    uint8_t *front;
    uint8_t *back;

    if (g_ready == 0u || width == 0u || height == 0u ||
        width > INTERFACEKIT_MAX_W || height > INTERFACEKIT_MAX_H) {
        return 0u;
    }
    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        if (g_windows[i].id == 0u) {
            window = &g_windows[i];
            break;
        }
    }
    if (window == (interfacekit_window_t *)0) {
        return 0u;
    }

    bytes = width * height * 4u;
    front = pool_alloc(bytes);
    back = pool_alloc(bytes);
    if (front == (uint8_t *)0 || back == (uint8_t *)0) {
        return 0u;
    }

    for (i = 0u; i < sizeof(*window); i++) {
        ((uint8_t *)window)[i] = 0u;
    }
    window->id = g_next_id++;
    copy_string(window->title, title, (uint32_t)sizeof(window->title));
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->z = ++g_z;
    window->visible = 1u;
    window->dirty = 1u;
    interfacekit_pixmap_init(&window->front, front, width, height, width);
    interfacekit_pixmap_init(&window->back, back, width, height, width);
    interfacekit_pixmap_fill(&window->front,
                   (interfacekit_rect_t){0, 0, width, height},
                   interfacekit_rgba(24u, 28u, 40u, 255u));
    interfacekit_pixmap_fill(&window->back,
                   (interfacekit_rect_t){0, 0, width, height},
                   interfacekit_rgba(24u, 28u, 40u, 255u));
    g_focused = window->id;
    return window->id;
}

static interfacekit_window_t *find_window(uint32_t id)
{
    uint32_t i;

    if (id == 0u) {
        return (interfacekit_window_t *)0;
    }
    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        if (g_windows[i].id == id) {
            return &g_windows[i];
        }
    }
    return (interfacekit_window_t *)0;
}

int interfacekit_window_destroy(uint32_t id)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0) {
        return -1;
    }
    window->id = 0u;            /* pool is bump-only: buffers stay reserved */
    if (g_focused == id) {
        g_focused = 0u;
    }
    return 0;
}

int interfacekit_window_move(uint32_t id, int32_t x, int32_t y)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0) {
        return -1;
    }
    window->x = x;
    window->y = y;
    window->dirty = 1u;
    return 0;
}

int interfacekit_window_raise(uint32_t id)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0) {
        return -1;
    }
    window->z = ++g_z;
    window->dirty = 1u;
    g_focused = id;
    return 0;
}

interfacekit_pixmap_t *interfacekit_window_backbuffer(uint32_t id)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0) {
        return (interfacekit_pixmap_t *)0;
    }
    return &window->back;
}

int interfacekit_window_present(uint32_t id)
{
    interfacekit_window_t *window = find_window(id);
    interfacekit_pixmap_t swap;

    if (window == (interfacekit_window_t *)0) {
        return -1;
    }
    swap = window->front;
    window->front = window->back;
    window->back = swap;
    window->dirty = 1u;
    window->z = ++g_z;
    g_focused = id;
    return 0;
}

int interfacekit_window_push_event(uint32_t id, const nk64_input_event_t *event)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0 || event == 0 ||
        window->event_count >= (uint32_t)INTERFACEKIT_EVENT_QUEUE) {
        return -1;
    }
    window->events[window->event_tail] = *event;
    window->event_tail = (window->event_tail + 1u) % (uint32_t)INTERFACEKIT_EVENT_QUEUE;
    window->event_count++;
    return 0;
}

uint32_t interfacekit_window_pop_event(uint32_t id, nk64_input_event_t *out)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0 || out == 0 || window->event_count == 0u) {
        return 0u;
    }
    *out = window->events[window->event_head];
    window->event_head = (window->event_head + 1u) % (uint32_t)INTERFACEKIT_EVENT_QUEUE;
    window->event_count--;
    return 1u;
}

static interfacekit_window_t *window_at(int32_t px, int32_t py)
{
    interfacekit_window_t *best = (interfacekit_window_t *)0;
    uint32_t i;

    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        interfacekit_window_t *w = &g_windows[i];

        if (w->id == 0u || w->visible == 0u) {
            continue;
        }
        if (px < w->x || py < w->y ||
            px >= w->x + (int32_t)w->width + (int32_t)(2u * INTERFACEKIT_BORDER) ||
            py >= w->y + (int32_t)w->height + (int32_t)(INTERFACEKIT_TITLE_H + INTERFACEKIT_BORDER)) {
            continue;
        }
        if (best == (interfacekit_window_t *)0 || w->z > best->z) {
            best = w;
        }
    }
    return best;
}

static void handle_input(void)
{
    nk64_input_event_t events[16];
    uint32_t count = nk64_input_read(events, 16u);
    uint32_t i;

    for (i = 0u; i < count; i++) {
        nk64_input_event_t *event = &events[i];
        interfacekit_window_t *target;

        if (event->type == (uint32_t)NK64_INPUT_MOUSE_MOVE) {
            g_cursor_x = event->x;
            g_cursor_y = event->y;
        }
        target = window_at(g_cursor_x, g_cursor_y);
        if (target == (interfacekit_window_t *)0) {
            continue;
        }
        if (event->type == (uint32_t)NK64_INPUT_MOUSE_DOWN ||
            event->type == (uint32_t)NK64_INPUT_MOUSE_UP) {
            interfacekit_window_raise(target->id);
        }
        if (g_focused != 0u && g_focused != target->id &&
            event->type == (uint32_t)NK64_INPUT_KEY_DOWN) {
            target = find_window(g_focused);
        }
        if (target == (interfacekit_window_t *)0) {
            continue;
        }
        event->dx = g_cursor_x - (target->x + (int32_t)INTERFACEKIT_BORDER);
        event->dy = g_cursor_y - (target->y + (int32_t)INTERFACEKIT_TITLE_H);
        (void)interfacekit_window_push_event(target->id, event);
    }
}

/* Reusable one-line pixmap for window titles and the taskbar label. */
static interfacekit_pixmap_t *title_strip(const char *text)
{
    static uint32_t pixels[112u * 8u];
    static interfacekit_pixmap_t strip;

    interfacekit_pixmap_init(&strip, pixels, 112u, 8u, 112u);
    interfacekit_pixmap_clear(&strip);
    interfacekit_pixmap_text(&strip, 2, 1, text, interfacekit_rgba(235u, 242u, 255u, 255u), 1u);
    return &strip;
}

static void compose(void)
{
    interfacekit_rect_t bar;
    uint32_t row;
    uint32_t i;

    if (g_fb_ok == 0u) {
        return;
    }

    /* Vertical midnight gradient. */
    for (row = 0u; row < g_fb.height; row++) {
        uint32_t shade = (row * 24u) / (g_fb.height == 0u ? 1u : g_fb.height);
        interfacekit_fb_fill(&g_fb, (interfacekit_rect_t){0, (int32_t)row, g_fb.width, 1u},
                   interfacekit_rgba((uint8_t)(12u + shade),
                           (uint8_t)(18u + shade),
                           (uint8_t)(42u + shade), 255u));
    }

    /* Taskbar. */
    bar = (interfacekit_rect_t){0, (int32_t)(g_fb.height - INTERFACEKIT_TASKBAR_H),
                      g_fb.width, INTERFACEKIT_TASKBAR_H};
    interfacekit_fb_fill(&g_fb, bar, interfacekit_rgba(20u, 24u, 32u, 255u));
    interfacekit_fb_fill(&g_fb, (interfacekit_rect_t){0, bar.y, g_fb.width, 1u},
               interfacekit_rgba(90u, 130u, 190u, 255u));
    {
        char status[48];
        uint32_t n = 0u;
        const char prefix[] = "INTERFACEKIT  DESKTOP  WINDOWS ";
        const char suffix[] = "  F1 HELP";

        while (prefix[n] != '\0') { status[n] = prefix[n]; n++; }
        status[n++] = (char)('0' + (char)(interfacekit_window_count() % 10u));
        for (i = 0u; suffix[i] != '\0'; i++) {
            status[n++] = suffix[i];
        }
        status[n] = '\0';
        /* Text is drawn on an RGBA strip, then composited in one blit. */
        {
            interfacekit_pixmap_t strip;
            static uint32_t strip_pixels[144u * 8u];
            uint32_t w = 144u;

            interfacekit_pixmap_init(&strip, strip_pixels, w, 8u, w);
            interfacekit_pixmap_clear(&strip);
            interfacekit_pixmap_text(&strip, 2, 1, status,
                           interfacekit_rgba(200u, 220u, 255u, 255u), 1u);
            interfacekit_fb_blit(&g_fb, 8, bar.y + 9, &strip, 255u);
        }
    }

    /* Opaque windows in bottom-to-top z order. */
    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        interfacekit_window_t *w = (interfacekit_window_t *)0;
        uint32_t j;

        for (j = 0u; j < (uint32_t)INTERFACEKIT_MAX_WINDOWS; j++) {
            interfacekit_window_t *candidate = &g_windows[j];
            if (candidate->id == 0u || candidate->visible == 0u) {
                continue;
            }
            if (w == (interfacekit_window_t *)0 || candidate->z < w->z) {
                w = candidate;
            }
        }
        if (w == (interfacekit_window_t *)0) {
            break;
        }
        w->visible = 0u;        /* consumed for this pass; restored below */
        {
            interfacekit_rect_t outer = {w->x, w->y,
                               w->width + 2u * INTERFACEKIT_BORDER,
                               w->height + INTERFACEKIT_TITLE_H + 2u * INTERFACEKIT_BORDER};

            interfacekit_fb_fill(&g_fb, outer, interfacekit_rgba(170u, 182u, 204u, 255u));
            interfacekit_fb_fill(&g_fb, (interfacekit_rect_t){w->x + (int32_t)INTERFACEKIT_BORDER,
                                          w->y + (int32_t)INTERFACEKIT_TITLE_H,
                                          w->width, w->height},
                       interfacekit_rgba(10u, 12u, 18u, 255u));
            interfacekit_fb_fill(&g_fb, (interfacekit_rect_t){w->x + (int32_t)INTERFACEKIT_BORDER, w->y,
                                          w->width, INTERFACEKIT_TITLE_H},
                       w->id == g_focused ? interfacekit_rgba(36u, 86u, 160u, 255u)
                                          : interfacekit_rgba(52u, 60u, 76u, 255u));
            interfacekit_fb_blit(&g_fb, w->x + (int32_t)INTERFACEKIT_BORDER + 4,
                       w->y + 5, title_strip(w->title), 255u);
            interfacekit_fb_blit(&g_fb, w->x + (int32_t)INTERFACEKIT_BORDER,
                       w->y + (int32_t)INTERFACEKIT_TITLE_H, &w->front, 255u);
            w->dirty = 0u;
        }
    }
    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0u) {
            g_windows[i].visible = 1u;
        }
    }

    interfacekit_fb_cursor(&g_fb, g_cursor_x, g_cursor_y,
                 interfacekit_rgba(245u, 245u, 250u, 255u));
}

void interfacekit_server_entry(void)
{
    nk64_fbinfo_t info;
    uint64_t map;
    uint32_t i;

    for (i = 0u; i < sizeof(info); i++) {
        ((uint8_t *)&info)[i] = 0u;
    }
    if (nk64_fb_info(&info) != 0u || info.present == 0u) {
        nk_printf("[InterfaceKit] no framebuffer reported; desktop disabled\n");
        for (;;) {
            (void)nk64_sleep(100u);
            (void)nk64_yield();
        }
    }
    map = nk64_fb_map();
    if (map == 0u) {
        nk_printf("[InterfaceKit] framebuffer claim failed\n");
        for (;;) {
            (void)nk64_sleep(100u);
            (void)nk64_yield();
        }
    }

    interfacekit_framebuffer_init(&g_fb, (void *)(uintptr_t)map, info.width,
                                 info.height, info.pitch, (uint8_t)info.bpp,
                                 info.red_size, info.red_shift,
                                 info.green_size, info.green_shift,
                                 info.blue_size, info.blue_shift);
    g_fb_ok = 1u;
    g_cursor_x = (int32_t)(info.width / 2u);
    g_cursor_y = (int32_t)(info.height / 2u);
    g_ready = 1u;

    nk_printf("[InterfaceKit] desktop %ux%u @ %u bpp, %u windows ready\n",
              info.width, info.height, info.bpp, (uint32_t)INTERFACEKIT_MAX_WINDOWS);

    for (;;) {
        handle_input();
        compose();
        (void)nk64_sleep(1u);
        (void)nk64_yield();
    }
}
