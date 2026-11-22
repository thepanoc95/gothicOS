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
#define INTERFACEKIT_SHADOW_BYTES (4u * 1024u * 1024u)
#define INTERFACEKIT_CURSOR_W     8u
#define INTERFACEKIT_CURSOR_H     14u

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

/* The compositor draws into this bounded shadow first. Only complete, changed
 * rectangles are copied to the claimed framebuffer, so scanout never sees a
 * half-composited desktop or a full-screen repaint during cursor movement. */
static uint8_t g_shadow[INTERFACEKIT_SHADOW_BYTES] __attribute__((aligned(16)));
static interfacekit_framebuffer_t g_shadow_fb;

typedef struct {
    uint32_t full;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
} interfacekit_dirty_t;

static interfacekit_dirty_t g_dirty;
static uint32_t g_compose_dirty;

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

static void mark_dirty(void)
{
    g_dirty.full = 1u;
    g_compose_dirty = 1u;
}

static void mark_dirty_rect(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    if (g_dirty.full != 0u || w == 0u || h == 0u || g_fb_ok == 0u) {
        return;
    }
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    if ((uint32_t)x0 >= g_fb.width || (uint32_t)y0 >= g_fb.height) {
        return;
    }
    x1 = x + (int32_t)w;
    y1 = y + (int32_t)h;
    if (x1 < x0 || y1 < y0) {
        return;
    }
    if (x1 > (int32_t)g_fb.width) {
        x1 = (int32_t)g_fb.width;
    }
    if (y1 > (int32_t)g_fb.height) {
        y1 = (int32_t)g_fb.height;
    }
    if (g_dirty.x1 <= g_dirty.x0 || g_dirty.y1 <= g_dirty.y0) {
        g_dirty.x0 = x0;
        g_dirty.y0 = y0;
        g_dirty.x1 = x1;
        g_dirty.y1 = y1;
    } else {
        if (x0 < g_dirty.x0) g_dirty.x0 = x0;
        if (y0 < g_dirty.y0) g_dirty.y0 = y0;
        if (x1 > g_dirty.x1) g_dirty.x1 = x1;
        if (y1 > g_dirty.y1) g_dirty.y1 = y1;
    }
    g_compose_dirty = 1u;
}

static void mark_window_rect(const interfacekit_window_t *window)
{
    mark_dirty_rect(window->x, window->y,
                    window->width + 2u * INTERFACEKIT_BORDER,
                    window->height + INTERFACEKIT_TITLE_H +
                        2u * INTERFACEKIT_BORDER);
}

static void mark_taskbar(void)
{
    mark_dirty_rect(0, (int32_t)(g_fb.height - INTERFACEKIT_TASKBAR_H),
                    g_fb.width, INTERFACEKIT_TASKBAR_H);
}

static void mark_cursor(int32_t x, int32_t y)
{
    mark_dirty_rect(x, y, INTERFACEKIT_CURSOR_W, INTERFACEKIT_CURSOR_H);
}

static interfacekit_dirty_t take_dirty(void)
{
    interfacekit_dirty_t result = g_dirty;

    g_dirty.full = 0u;
    g_dirty.x0 = 0;
    g_dirty.y0 = 0;
    g_dirty.x1 = 0;
    g_dirty.y1 = 0;
    g_compose_dirty = 0u;
    return result;
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
    mark_window_rect(window);
    mark_taskbar();
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
    mark_window_rect(window);
    mark_taskbar();
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
    mark_window_rect(window);   /* old position */
    window->x = x;
    window->y = y;
    window->dirty = 1u;
    mark_window_rect(window);   /* new position */
    return 0;
}

int interfacekit_window_raise(uint32_t id)
{
    interfacekit_window_t *window = find_window(id);

    if (window == (interfacekit_window_t *)0) {
        return -1;
    }
    if (window->z == g_z && g_focused == id) {
        return 0;
    }
    {
        interfacekit_window_t *previous = find_window(g_focused);

        if (previous != (interfacekit_window_t *)0) {
            mark_window_rect(previous);
        }
    }
    window->z = ++g_z;
    window->dirty = 1u;
    g_focused = id;
    mark_window_rect(window);
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
    mark_dirty_rect(window->x + (int32_t)INTERFACEKIT_BORDER,
                    window->y + (int32_t)INTERFACEKIT_TITLE_H,
                    window->width, window->height);
    return 0;   /* present is not a raise: use window_raise() for focus */
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
            if (g_cursor_x != event->x || g_cursor_y != event->y) {
                mark_cursor(g_cursor_x, g_cursor_y);
                g_cursor_x = event->x;
                g_cursor_y = event->y;
                mark_cursor(g_cursor_x, g_cursor_y);
            }
        }
        target = window_at(g_cursor_x, g_cursor_y);
        if (target == (interfacekit_window_t *)0) {
            continue;
        }
        /* Only a fresh click changes z-order/focus; mouse-up must not shuffle
         * windows or force another full desktop repaint. */
        if (event->type == (uint32_t)NK64_INPUT_MOUSE_DOWN) {
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
    interfacekit_framebuffer_t *fb = &g_shadow_fb;
    interfacekit_rect_t bar;
    uint32_t row;
    uint32_t i;

    if (g_fb_ok == 0u) {
        return;
    }

    /* Vertical midnight gradient, drawn into the shadow buffer. */
    for (row = 0u; row < fb->height; row++) {
        uint32_t shade = (row * 24u) / (fb->height == 0u ? 1u : fb->height);
        interfacekit_fb_fill(fb, (interfacekit_rect_t){0, (int32_t)row, fb->width, 1u},
                   interfacekit_rgba((uint8_t)(12u + shade),
                           (uint8_t)(18u + shade),
                           (uint8_t)(42u + shade), 255u));
    }

    /* Taskbar. */
    bar = (interfacekit_rect_t){0, (int32_t)(fb->height - INTERFACEKIT_TASKBAR_H),
                      fb->width, INTERFACEKIT_TASKBAR_H};
    interfacekit_fb_fill(fb, bar, interfacekit_rgba(20u, 24u, 32u, 255u));
    interfacekit_fb_fill(fb, (interfacekit_rect_t){0, bar.y, fb->width, 1u},
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
            interfacekit_fb_blit(fb, 8, bar.y + 9, &strip, 255u);
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

            interfacekit_fb_fill(fb, outer, interfacekit_rgba(170u, 182u, 204u, 255u));
            interfacekit_fb_fill(fb, (interfacekit_rect_t){w->x + (int32_t)INTERFACEKIT_BORDER,
                                          w->y + (int32_t)INTERFACEKIT_TITLE_H,
                                          w->width, w->height},
                       interfacekit_rgba(10u, 12u, 18u, 255u));
            interfacekit_fb_fill(fb, (interfacekit_rect_t){w->x + (int32_t)INTERFACEKIT_BORDER, w->y,
                                          w->width, INTERFACEKIT_TITLE_H},
                       w->id == g_focused ? interfacekit_rgba(36u, 86u, 160u, 255u)
                                          : interfacekit_rgba(52u, 60u, 76u, 255u));
            interfacekit_fb_blit(fb, w->x + (int32_t)INTERFACEKIT_BORDER + 4,
                       w->y + 5, title_strip(w->title), 255u);
            interfacekit_fb_blit(fb, w->x + (int32_t)INTERFACEKIT_BORDER,
                       w->y + (int32_t)INTERFACEKIT_TITLE_H, &w->front, 255u);
            w->dirty = 0u;
        }
    }
    for (i = 0u; i < (uint32_t)INTERFACEKIT_MAX_WINDOWS; i++) {
        if (g_windows[i].id != 0u) {
            g_windows[i].visible = 1u;
        }
    }

    interfacekit_fb_cursor(fb, g_cursor_x, g_cursor_y,
                 interfacekit_rgba(245u, 245u, 250u, 255u));
}

/* Publish the changed part of the shadow frame to the real framebuffer. This
 * is the only place in ring 3 that writes claimed framebuffer pixels. */
static void flush_shadow(const interfacekit_dirty_t *dirty)
{
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t row;
    uint32_t bytes;
    uint32_t bytes_per_pixel;

    if (g_fb_ok == 0u || dirty == (const interfacekit_dirty_t *)0) {
        return;
    }
    if (dirty->full != 0u) {
        x0 = 0u;
        y0 = 0u;
        x1 = g_fb.width;
        y1 = g_fb.height;
    } else {
        if (dirty->x1 <= dirty->x0 || dirty->y1 <= dirty->y0) {
            return;
        }
        x0 = (uint32_t)dirty->x0;
        y0 = (uint32_t)dirty->y0;
        x1 = (uint32_t)dirty->x1;
        y1 = (uint32_t)dirty->y1;
        if (x1 > g_fb.width) x1 = g_fb.width;
        if (y1 > g_fb.height) y1 = g_fb.height;
    }
    bytes_per_pixel = g_fb.bpp / 8u;
    bytes = (x1 - x0) * bytes_per_pixel;
    if (bytes == 0u || x1 * bytes_per_pixel > g_shadow_fb.pitch) {
        return;
    }

    /* Copy whole dwords where possible; the byte loop is only for odd-sized
     * frames or unaligned rectangles. */
    x0 &= ~3u;
    x1 = (x1 + 3u) & ~3u;
    if (x1 > g_fb.width) x1 = g_fb.width;
    if ((bytes_per_pixel == 4u) && (x0 % 4u) == 0u &&
        (g_fb.pitch % 4u) == 0u && (g_shadow_fb.pitch % 4u) == 0u) {
        uint32_t *dst = (uint32_t *)(void *)(g_fb.pixels +
                           (uint64_t)y0 * g_fb.pitch + x0 * 4u);
        const uint32_t *src = (const uint32_t *)(const void *)(g_shadow_fb.pixels +
                               (uint64_t)y0 * g_shadow_fb.pitch + x0 * 4u);
        uint32_t words = x1 - x0;

        for (row = y0; row < y1; row++) {
            uint32_t i;

            for (i = 0u; i < words; i++) {
                dst[i] = src[i];
            }
            dst = (uint32_t *)(void *)((uint8_t *)dst + g_fb.pitch);
            src = (const uint32_t *)(const void *)((const uint8_t *)src +
                                                   g_shadow_fb.pitch);
        }
        return;
    }

    bytes = (x1 - x0) * bytes_per_pixel;
    for (row = y0; row < y1; row++) {
        nk_memcpy(g_fb.pixels + (uint64_t)row * g_fb.pitch + x0 * bytes_per_pixel,
                  g_shadow_fb.pixels + (uint64_t)row * g_shadow_fb.pitch +
                      x0 * bytes_per_pixel,
                  bytes);
    }
}

void interfacekit_server_entry(void)
{
    nk64_fbinfo_t info;
    uint64_t map;
    uint64_t row_bytes;
    uint64_t shadow_bytes;
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
    if (info.bpp != 24u && info.bpp != 32u) {
        nk_printf("[InterfaceKit] unsupported framebuffer depth %u; desktop disabled\n",
                  info.bpp);
        for (;;) {
            (void)nk64_sleep(100u);
            (void)nk64_yield();
        }
    }

    row_bytes = (uint64_t)info.width * (info.bpp / 8u);
    shadow_bytes = row_bytes * info.height;
    if (row_bytes == 0u || row_bytes > info.pitch ||
        shadow_bytes > (uint64_t)INTERFACEKIT_SHADOW_BYTES) {
        nk_printf("[InterfaceKit] framebuffer %ux%u exceeds the %u-byte shadow; "
                  "desktop disabled\n", info.width, info.height,
                  (uint32_t)INTERFACEKIT_SHADOW_BYTES);
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
    interfacekit_framebuffer_init(&g_shadow_fb, g_shadow, info.width,
                                 info.height, (uint32_t)row_bytes,
                                 (uint8_t)info.bpp,
                                 info.red_size, info.red_shift,
                                 info.green_size, info.green_shift,
                                 info.blue_size, info.blue_shift);
    g_fb_ok = 1u;
    g_cursor_x = (int32_t)(info.width / 2u);
    g_cursor_y = (int32_t)(info.height / 2u);
    mark_dirty();
    g_ready = 1u;

    nk_printf("[InterfaceKit] desktop %ux%u @ %u bpp, %u windows ready\n",
              info.width, info.height, info.bpp, (uint32_t)INTERFACEKIT_MAX_WINDOWS);

    for (;;) {
        interfacekit_dirty_t frame_dirty;

        handle_input();
        if (g_compose_dirty != 0u) {
            /* Snapshot before compositing: writes from the demo task during
             * this pass stay queued for the next frame instead of being
             * published from a stale shadow buffer. */
            frame_dirty = take_dirty();
            compose();
            flush_shadow(&frame_dirty);
        }
        (void)nk64_sleep(1u);
        (void)nk64_yield();
    }
}
