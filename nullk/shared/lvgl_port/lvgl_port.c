/*
 * lvgl_port - LVGL 9 display/tick/input glue for null_kernel.
 *
 * The flush callback is the only place that knows about pixel formats. LVGL
 * hands us XRGB8888 bytes (B, G, R, X in memory order); we pack them into the
 * framebuffer's masks and write them with its pitch.
 */
#include <stddef.h>

#include "lvgl_port.h"

typedef struct {
    lvgl_port_event_t entries[LVGL_PORT_EVENT_QUEUE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} lvgl_port_queue_t;

static const interfacekit_framebuffer_t *g_fb;
static lv_display_t *g_display;
static lv_indev_t *g_pointer;
static lv_indev_t *g_keypad;
static lv_group_t *g_group;
static lvgl_port_tick_fn g_tick_cb;
static lvgl_port_queue_t g_events;
static uint32_t g_consumed;
static int32_t g_last_x;
static int32_t g_last_y;
static uint32_t g_last_buttons;

static uint32_t pack_channel(uint8_t value, uint8_t size, uint8_t shift)
{
    uint32_t scaled;

    if (size == 0u) {
        return 0u;
    }
    if (size >= 8u) {
        scaled = value;
    } else {
        scaled = (uint32_t)value >> (8u - size);
    }
    return scaled << shift;
}

static uint32_t pack_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    return pack_channel(r, g_fb->red_size, g_fb->red_shift) |
           pack_channel(g, g_fb->green_size, g_fb->green_shift) |
           pack_channel(b, g_fb->blue_size, g_fb->blue_shift);
}

static void store_pixel(uint8_t *row, uint32_t x, uint32_t value)
{
    uint32_t bytes = (uint32_t)g_fb->bpp / 8u;

    if (bytes == 3u) {
        uint8_t *p = row + (size_t)x * 3u;
        p[0] = (uint8_t)(value & 0xffu);
        p[1] = (uint8_t)((value >> 8) & 0xffu);
        p[2] = (uint8_t)((value >> 16) & 0xffu);
    } else if (bytes == 4u) {
        uint32_t *p = (uint32_t *)(void *)(row + (size_t)x * 4u);
        *p = value;
    } else if (bytes == 2u) {
        uint16_t *p = (uint16_t *)(void *)(row + (size_t)x * 2u);
        *p = (uint16_t)value;
    }
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px)
{
    const uint8_t *src = px;
    int32_t line;
    int32_t width = lv_area_get_width(area);

    if (g_fb == NULL || g_fb->pixels == NULL) {
        lv_display_flush_ready(display);
        return;
    }

    for (line = area->y1; line <= area->y2; line++) {
        uint8_t *row;
        int32_t col;

        if (line < 0 || (uint32_t)line >= g_fb->height) {
            src += (size_t)width * 4u;
            continue;
        }
        row = g_fb->pixels + (size_t)line * g_fb->pitch;
        for (col = 0; col < width; col++) {
            int32_t x = area->x1 + col;

            if (x >= 0 && (uint32_t)x < g_fb->width) {
                store_pixel(row, (uint32_t)x,
                            pack_pixel(src[2], src[1], src[0]));
            }
            src += 4;
        }
    }
    lv_display_flush_ready(display);
}

void lvgl_port_set_tick_cb(lvgl_port_tick_fn fn)
{
    g_tick_cb = fn;
}

uint32_t lvgl_port_tick_ms(void)
{
    return g_tick_cb != NULL ? g_tick_cb() : 0u;
}

static uint32_t map_key(uint32_t key)
{
    switch (key) {
    case 0x101u: return LV_KEY_UP;
    case 0x102u: return LV_KEY_DOWN;
    case 0x103u: return LV_KEY_LEFT;
    case 0x104u: return LV_KEY_RIGHT;
    case 0x105u: return LV_KEY_HOME;
    case 0x106u: return LV_KEY_END;
    case 0x107u: return LV_KEY_PREV;
    case 0x108u: return LV_KEY_NEXT;
    case 0x10au: return LV_KEY_DEL;
    case 0x0au:  return LV_KEY_ENTER;
    case 0x1bu:  return LV_KEY_ESC;
    case 0x09u:  return LV_KEY_NEXT;
    default:     return key;
    }
}

/*
 * Take the oldest queued event whose type is in `type_mask`. Pointer and
 * keypad devices drain the same queue, so the head may belong to the other
 * device; the matching entry is removed and the rest keep their order.
 */
static int take_event(lvgl_port_event_t *out, uint32_t type_mask)
{
    uint32_t i;

    for (i = 0; i < g_events.count; i++) {
        uint32_t index = (g_events.head + i) % LVGL_PORT_EVENT_QUEUE;

        if ((type_mask & (1u << g_events.entries[index].type)) == 0u) {
            continue;
        }
        *out = g_events.entries[index];
        for (; i + 1u < g_events.count; i++) {
            uint32_t from = (g_events.head + i + 1u) % LVGL_PORT_EVENT_QUEUE;
            uint32_t to = (g_events.head + i) % LVGL_PORT_EVENT_QUEUE;

            g_events.entries[to] = g_events.entries[from];
        }
        g_events.count--;
        g_consumed++;
        return 1;
    }
    return 0;
}

static uint32_t pointer_mask(void)
{
    return (1u << LVGL_PORT_POINTER_MOVE) | (1u << LVGL_PORT_POINTER_DOWN) |
           (1u << LVGL_PORT_POINTER_UP);
}

static uint32_t key_mask(void)
{
    return (1u << LVGL_PORT_KEY_DOWN) | (1u << LVGL_PORT_KEY_UP);
}

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int32_t last_x;
    static int32_t last_y;
    lvgl_port_event_t ev;

    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    if (take_event(&ev, pointer_mask())) {
        last_x = ev.x;
        last_y = ev.y;
        g_last_x = ev.x;
        g_last_y = ev.y;
        g_last_buttons = ev.buttons;
        if (ev.type == LVGL_PORT_POINTER_DOWN) {
            data->state = LV_INDEV_STATE_PRESSED;
        }
    }
    data->point.x = (int32_t)last_x;
    data->point.y = (int32_t)last_y;
}

static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t last_key;
    lvgl_port_event_t ev;

    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    if (take_event(&ev, key_mask())) {
        last_key = ev.key;
        if (ev.type == LVGL_PORT_KEY_DOWN) {
            data->state = LV_INDEV_STATE_PRESSED;
        }
    }
    data->key = map_key(last_key);
}

lv_display_t *lvgl_port_display_init(const interfacekit_framebuffer_t *fb,
                                     void *draw_buffer,
                                     uint32_t draw_buffer_bytes)
{
    uint32_t pixels;

    if (fb == NULL || fb->pixels == NULL || fb->width == 0u || fb->height == 0u) {
        return NULL;
    }
    pixels = fb->width * fb->height;
    if (draw_buffer == NULL || draw_buffer_bytes < (pixels / 10u) * 4u) {
        return NULL;
    }

    g_fb = fb;
    lv_init();
    lv_tick_set_cb(lvgl_port_tick_ms);

    g_display = lv_display_create((int32_t)fb->width, (int32_t)fb->height);
    if (g_display == NULL) {
        return NULL;
    }
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(g_display, draw_buffer, NULL, draw_buffer_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_display, flush_cb);
    lv_display_set_default(g_display);

    g_pointer = lv_indev_create();
    if (g_pointer != NULL) {
        lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_pointer, pointer_read_cb);
        lv_indev_set_display(g_pointer, g_display);
    }

    g_keypad = lv_indev_create();
    if (g_keypad != NULL) {
        lv_indev_set_type(g_keypad, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(g_keypad, keypad_read_cb);
        lv_indev_set_display(g_keypad, g_display);
        lv_indev_set_group(g_keypad, lvgl_port_group());
    }

    return g_display;
}

lv_group_t *lvgl_port_group(void)
{
    if (g_group == NULL) {
        g_group = lv_group_create();
        if (g_group != NULL) {
            lv_group_set_default(g_group);
        }
    }
    return g_group;
}

lv_indev_t *lvgl_port_pointer(void)
{
    return g_pointer;
}

lv_indev_t *lvgl_port_keypad(void)
{
    return g_keypad;
}

int lvgl_port_push_event(const lvgl_port_event_t *event)
{
    uint32_t index;

    if (event == NULL) {
        return -1;
    }
    if (g_events.count >= LVGL_PORT_EVENT_QUEUE) {
        return -1;
    }
    index = (g_events.head + g_events.count) % LVGL_PORT_EVENT_QUEUE;
    g_events.entries[index] = *event;
    g_events.count++;
    return 0;
}

uint32_t lvgl_port_pump(void)
{
    return lv_timer_handler();
}

uint32_t lvgl_port_events_consumed(void)
{
    return g_consumed;
}

void lvgl_port_last_pointer(int32_t *x, int32_t *y, uint32_t *buttons)
{
    if (x != NULL) *x = g_last_x;
    if (y != NULL) *y = g_last_y;
    if (buttons != NULL) *buttons = g_last_buttons;
}
