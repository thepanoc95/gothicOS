/*
 * Standalone framebuffer console for the native Linux ABI v3 port.
 *
 * This is deliberately not InterfaceKit and not Linux fbcon. It uses only the
 * ABI v3 FB_INFO/FB_MAP primitives and raw evdev keyboard events, so nksh stays
 * visible and interactive on a DRM framebuffer when tty0 has no painter (for
 * example virtio-gpu with CONFIG_FB and DRM_FBDEV_EMULATION disabled).
 *
 * Console output from the init and nksh threads is mirrored here by the syscall
 * shim; keyboard bytes are queued for nksh's fd 0 reads.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "libnk64.h"
#include "nullk/interfacekit.h"
#include "nullk/printk.h"
#include "nullk_linux/evdev_keycode.h"
#include "nullk_linux/fb_console.h"

/* The shared InterfaceKit font is 3x5. Scale it 4x so a 1024x768 scanout gives
 * a readable ~63x29 terminal instead of a dense 126x60 one. */
#define FB_CONSOLE_SCALE        4u
#define FB_CONSOLE_CELL_W       ((INTERFACEKIT_FONT_WIDTH + 1u) * FB_CONSOLE_SCALE)
#define FB_CONSOLE_CELL_H       ((INTERFACEKIT_FONT_HEIGHT + 1u) * FB_CONSOLE_SCALE)
#define FB_CONSOLE_HEADER_ROWS  2u
#define FB_CONSOLE_HEADER_H     (FB_CONSOLE_HEADER_ROWS * FB_CONSOLE_CELL_H + 8u)
#define FB_CONSOLE_MAX_COLS     160u
#define FB_CONSOLE_MAX_ROWS     64u
#define FB_CONSOLE_INPUT_BYTES  256u
#define FB_CONSOLE_KEYBOARDS    8u
#define FB_CONSOLE_EVENT_PATHS  32u
#define FB_CONSOLE_EVENT_BATCH  32u

#define FB_COLOR_BG      interfacekit_rgba(8u, 12u, 24u, 255u)
#define FB_COLOR_HEADER  interfacekit_rgba(20u, 44u, 92u, 255u)
#define FB_COLOR_TEXT    interfacekit_rgba(224u, 236u, 255u, 255u)
#define FB_COLOR_ACCENT  interfacekit_rgba(104u, 196u, 255u, 255u)

static interfacekit_framebuffer_t g_fb;
static int g_ready;

static char g_text[FB_CONSOLE_MAX_ROWS][FB_CONSOLE_MAX_COLS + 1u];
static uint32_t g_cols;
static uint32_t g_rows;
static uint32_t g_cursor_row;
static uint32_t g_cursor_col;
static pthread_mutex_t g_render_lock = PTHREAD_MUTEX_INITIALIZER;

static uint8_t g_input[FB_CONSOLE_INPUT_BYTES];
static uint32_t g_input_head;
static uint32_t g_input_tail;
static pthread_mutex_t g_input_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_kbd_fds[FB_CONSOLE_KEYBOARDS];
static int g_kbd_count;
static unsigned char g_kbd_seen[FB_CONSOLE_EVENT_PATHS];
static int g_kbd_logged;
static int g_shift;
static int g_caps;
static pthread_t g_kbd_thread;
static int g_kbd_thread_started;

static void draw_glyph(uint8_t ch, int32_t x, int32_t y,
                       interfacekit_rgba_t color)
{
    const uint8_t *glyph = interfacekit_glyph(ch);
    uint32_t col;
    uint32_t row;
    uint32_t dy;
    uint32_t dx;

    for (row = 0u; row < INTERFACEKIT_FONT_HEIGHT; row++) {
        for (col = 0u; col < INTERFACEKIT_FONT_WIDTH; col++) {
            if ((glyph[col] & (1u << row)) == 0u) {
                continue;
            }
            for (dy = 0u; dy < FB_CONSOLE_SCALE; dy++) {
                for (dx = 0u; dx < FB_CONSOLE_SCALE; dx++) {
                    interfacekit_fb_blend(&g_fb,
                                          x + (int32_t)(col * FB_CONSOLE_SCALE + dx),
                                          y + (int32_t)(row * FB_CONSOLE_SCALE + dy),
                                          color);
                }
            }
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char *text,
                      interfacekit_rgba_t color)
{
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        draw_glyph((uint8_t)*text, x, y, color);
        x += (int32_t)((INTERFACEKIT_FONT_WIDTH + 1u) * FB_CONSOLE_SCALE);
        text++;
    }
}

/* All draw helpers below are called with g_render_lock held. */
static void draw_cursor(void)
{
    uint32_t col;
    int32_t x;
    int32_t y;

    if (g_cursor_row >= g_rows || g_cols == 0u) {
        return;
    }
    col = g_cursor_col < g_cols ? g_cursor_col : g_cols - 1u;
    x = (int32_t)(col * FB_CONSOLE_CELL_W);
    y = (int32_t)(FB_CONSOLE_HEADER_H +
                  g_cursor_row * FB_CONSOLE_CELL_H +
                  FB_CONSOLE_CELL_H - 2u * FB_CONSOLE_SCALE);
    interfacekit_fb_fill(&g_fb,
                         (interfacekit_rect_t){x, y,
                                               FB_CONSOLE_CELL_W,
                                               2u * FB_CONSOLE_SCALE},
                         FB_COLOR_ACCENT);
}

static void draw_row(uint32_t row)
{
    uint32_t col;

    if (row >= g_rows) {
        return;
    }
    for (col = 0u; col < g_cols; col++) {
        int32_t x = (int32_t)(col * FB_CONSOLE_CELL_W);
        int32_t y = (int32_t)(FB_CONSOLE_HEADER_H + row * FB_CONSOLE_CELL_H);

        interfacekit_fb_fill(&g_fb,
                             (interfacekit_rect_t){x, y,
                                                   FB_CONSOLE_CELL_W,
                                                   FB_CONSOLE_CELL_H},
                             FB_COLOR_BG);
        if (g_text[row][col] >= 0x20 && g_text[row][col] <= 0x7e) {
            draw_glyph((uint8_t)g_text[row][col], x, y, FB_COLOR_TEXT);
        }
    }
    if (row == g_cursor_row) {
        draw_cursor();
    }
}

static void redraw_all(void)
{
    uint32_t row;

    for (row = 0u; row < g_rows; row++) {
        draw_row(row);
    }
}

static void scroll_rows(void)
{
    if (g_rows > 1u) {
        memmove(g_text[0], g_text[1],
                (size_t)((g_rows - 1u) * (FB_CONSOLE_MAX_COLS + 1u)));
    }
    memset(g_text[g_rows - 1u], ' ', g_cols);
    g_text[g_rows - 1u][g_cols] = '\0';
    g_cursor_row = g_rows - 1u;
}

static int newline(void)
{
    int scrolled = 0;

    if (g_cursor_row + 1u < g_rows) {
        g_cursor_row++;
    } else {
        scroll_rows();
        scrolled = 1;
    }
    g_cursor_col = 0u;
    return scrolled;
}

static void put_char_locked(uint8_t byte)
{
    uint32_t old_row = g_cursor_row;
    char c = (char)byte;
    int scrolled = 0;

    if (c == '\r') {
        g_cursor_col = 0u;
    } else if (c == '\n') {
        scrolled = newline();
    } else if (c == '\b') {
        if (g_cursor_col != 0u) {
            g_cursor_col--;
        }
    } else if (c == '\t') {
        uint32_t next = (g_cursor_col + 4u) & ~3u;

        if (next > g_cols) {
            next = g_cols;
        }
        while (g_cursor_col < next) {
            g_text[g_cursor_row][g_cursor_col] = ' ';
            g_cursor_col++;
        }
    } else if (c >= 0x20 && c <= 0x7e) {
        if (g_cursor_col >= g_cols) {
            scrolled |= newline();
        }
        g_text[g_cursor_row][g_cursor_col] = c;
        g_cursor_col++;
        if (g_cursor_col >= g_cols) {
            scrolled |= newline();
        }
    } else {
        return;
    }

    if (scrolled != 0) {
        redraw_all();
        return;
    }
    draw_row(old_row);
    if (g_cursor_row != old_row) {
        draw_row(g_cursor_row);
    }
}

static void input_push(uint8_t byte)
{
    uint32_t next;

    pthread_mutex_lock(&g_input_lock);
    next = (g_input_tail + 1u) % FB_CONSOLE_INPUT_BYTES;
    if (next == g_input_head) {
        g_input_head = (g_input_head + 1u) % FB_CONSOLE_INPUT_BYTES;
    }
    g_input[g_input_tail] = byte;
    g_input_tail = next;
    pthread_mutex_unlock(&g_input_lock);
}

static int key_has_bit(const unsigned char *bits, size_t bits_len,
                       unsigned int code)
{
    if ((size_t)(code / 8u) >= bits_len) {
        return 0;
    }
    return (int)((bits[code / 8u] >> (code % 8u)) & 1u);
}

static void keyboard_event(const struct input_event *ev)
{
    uint8_t ch;

    if (ev->type != EV_KEY) {
        return;
    }
    if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
        g_shift = ev->value != 0;
        return;
    }
    if (ev->code == KEY_CAPSLOCK) {
        /* value 2 is key autorepeat, not a new caps-lock press. */
        if (ev->value == 1) {
            g_caps = !g_caps;
        }
        return;
    }
    if (ev->value == 0) {
        return;
    }
    ch = nk64_evdev_key_to_ascii((unsigned int)ev->code, g_shift, g_caps);
    if (ch != 0u) {
        input_push(ch);
    }
}

static void keyboard_read(int fd)
{
    struct input_event events[FB_CONSOLE_EVENT_BATCH];
    ssize_t got;

    for (;;) {
        got = read(fd, events, sizeof(events));
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (got == 0) {
            return;
        }
        for (size_t i = 0u; i < (size_t)got / sizeof(events[0]); i++) {
            keyboard_event(&events[i]);
        }
        if ((size_t)got < sizeof(events)) {
            return;
        }
    }
}

static void open_keyboards(void)
{
    unsigned char bits[(KEY_MAX + 7u) / 8u];
    char path[64];
    int fd;
    uint32_t i;

    for (i = 0u; i < FB_CONSOLE_EVENT_PATHS &&
                  (uint32_t)g_kbd_count < FB_CONSOLE_KEYBOARDS; i++) {
        /* The keyboard thread runs this scan repeatedly so devices that appear
         * later can still be attached. Remember every path we opened: opening
         * the same evdev node more than once makes every keystroke repeat once
         * per descriptor. */
        if (g_kbd_seen[i] != 0u) {
            continue;
        }
        (void)snprintf(path, sizeof(path), "/dev/input/event%u", i);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue; /* leave unseen so a later hotplug can be picked up */
        }
        g_kbd_seen[i] = 1u;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0 ||
            !key_has_bit(bits, sizeof(bits), KEY_A) ||
            !key_has_bit(bits, sizeof(bits), KEY_ENTER) ||
            !key_has_bit(bits, sizeof(bits), KEY_SPACE)) {
            (void)close(fd);
            continue;
        }
        g_kbd_fds[g_kbd_count++] = fd;
    }
}

static void keyboard_close_at(uint32_t index)
{
    if (g_kbd_fds[index] >= 0) {
        (void)close(g_kbd_fds[index]);
        g_kbd_fds[index] = -1;
    }
}

static void keyboard_compact(void)
{
    uint32_t read_index;
    uint32_t write_index = 0u;

    for (read_index = 0u; read_index < (uint32_t)g_kbd_count; read_index++) {
        if (g_kbd_fds[read_index] >= 0) {
            g_kbd_fds[write_index++] = g_kbd_fds[read_index];
        }
    }
    g_kbd_count = (int)write_index;
}

static void log_keyboards(void)
{
    char msg[96];
    int len = snprintf(msg, sizeof(msg),
                       "[fbconsole] %d evdev keyboard(s) attached\n",
                       g_kbd_count);

    if (len > 0) {
        (void)nk64_log(NK_KERN_INFO, msg, (uint32_t)len);
    }
}

static void *keyboard_main(void *arg)
{
    struct pollfd pfds[FB_CONSOLE_KEYBOARDS];
    int owners[FB_CONSOLE_KEYBOARDS];
    int count;
    int i;

    (void)arg;
    for (;;) {
        if ((uint32_t)g_kbd_count < FB_CONSOLE_KEYBOARDS) {
            open_keyboards();
            if (!g_kbd_logged && g_kbd_count > 0) {
                log_keyboards();
                g_kbd_logged = 1;
            }
        }
        count = 0;
        for (i = 0; i < g_kbd_count; i++) {
            if (g_kbd_fds[i] < 0) {
                continue;
            }
            pfds[count].fd = g_kbd_fds[i];
            pfds[count].events = POLLIN;
            pfds[count].revents = 0;
            owners[count] = i;
            count++;
        }
        if (count == 0) {
            (void)poll(NULL, 0u, 200);
            continue;
        }
        if (poll(pfds, (nfds_t)count, 200) < 0) {
            continue;
        }
        for (i = 0; i < count; i++) {
            if (pfds[i].revents == 0) {
                continue;
            }
            if ((pfds[i].revents & POLLIN) != 0) {
                keyboard_read(pfds[i].fd);
            }
            if ((pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                keyboard_close_at((uint32_t)owners[i]);
            }
        }
        keyboard_compact();
    }
    return NULL;
}

static void start_keyboard(void)
{
    pthread_attr_t attr;

    if (g_kbd_thread_started != 0) {
        return;
    }
    if (pthread_attr_init(&attr) != 0) {
        return;
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&g_kbd_thread, &attr, keyboard_main, NULL) == 0) {
        g_kbd_thread_started = 1;
    }
    pthread_attr_destroy(&attr);
}

void nk64_fb_console_init(void)
{
    nk64_fbinfo_t info;
    uint64_t base;
    uint32_t row;
    static const char banner[] = "[nullk] framebuffer console ready\n";

    if (g_ready != 0) {
        return;
    }
    memset(&info, 0, sizeof(info));
    if (nk64_fb_info(&info) != (uint32_t)NK_STATUS_OK ||
        info.present == 0u || (info.bpp != 32u && info.bpp != 24u)) {
        return;
    }
    base = nk64_fb_map();
    if (base == 0u || info.width == 0u || info.height == 0u ||
        info.pitch < (uint64_t)info.width * (info.bpp / 8u)) {
        return;
    }

    interfacekit_framebuffer_init(&g_fb, (void *)(uintptr_t)base,
                                  info.width, info.height, info.pitch,
                                  info.bpp,
                                  info.red_size, info.red_shift,
                                  info.green_size, info.green_shift,
                                  info.blue_size, info.blue_shift);

    if (info.width <= 16u || info.height <= FB_CONSOLE_HEADER_H + 8u) {
        return;
    }
    g_cols = (info.width - 16u) / FB_CONSOLE_CELL_W;
    if (g_cols > FB_CONSOLE_MAX_COLS) {
        g_cols = FB_CONSOLE_MAX_COLS;
    }
    g_rows = (info.height - FB_CONSOLE_HEADER_H - 8u) / FB_CONSOLE_CELL_H;
    if (g_rows > FB_CONSOLE_MAX_ROWS) {
        g_rows = FB_CONSOLE_MAX_ROWS;
    }
    if (g_cols == 0u || g_rows == 0u) {
        return;
    }

    memset(g_text, ' ', sizeof(g_text));
    for (row = 0u; row < FB_CONSOLE_MAX_ROWS; row++) {
        g_text[row][FB_CONSOLE_MAX_COLS] = '\0';
    }
    g_cursor_row = 0u;
    g_cursor_col = 0u;

    pthread_mutex_lock(&g_render_lock);
    interfacekit_fb_fill(&g_fb,
                         (interfacekit_rect_t){0, 0, info.width, info.height},
                         FB_COLOR_BG);
    interfacekit_fb_fill(&g_fb,
                         (interfacekit_rect_t){0, 0, info.width, FB_CONSOLE_HEADER_H},
                         FB_COLOR_HEADER);
    draw_text((int32_t)(FB_CONSOLE_SCALE * 2u),
              (int32_t)(FB_CONSOLE_SCALE * 2u),
              "null_kernel nk) framebuffer console", FB_COLOR_ACCENT);
    draw_text((int32_t)(FB_CONSOLE_SCALE * 2u),
              (int32_t)(FB_CONSOLE_SCALE * 2u + FB_CONSOLE_CELL_H),
              "keyboard /dev/input/event*  -  no InterfaceKit", FB_COLOR_TEXT);
    redraw_all();
    pthread_mutex_unlock(&g_render_lock);

    g_ready = 1;
    nk64_fb_console_output_push((const uint8_t *)banner, sizeof(banner) - 1u);
    start_keyboard();
}

int nk64_fb_console_active(void)
{
    return g_ready;
}

size_t nk64_fb_console_input_get(uint8_t *out, size_t cap)
{
    size_t count = 0u;

    if (out == NULL || cap == 0u) {
        return 0u;
    }
    pthread_mutex_lock(&g_input_lock);
    while (count < cap && g_input_head != g_input_tail) {
        out[count++] = g_input[g_input_head];
        g_input_head = (g_input_head + 1u) % FB_CONSOLE_INPUT_BYTES;
    }
    pthread_mutex_unlock(&g_input_lock);
    return count;
}

void nk64_fb_console_output_push(const uint8_t *data, size_t len)
{
    size_t i;

    if (g_ready == 0 || data == NULL || len == 0u) {
        return;
    }
    pthread_mutex_lock(&g_render_lock);
    for (i = 0u; i < len; i++) {
        put_char_locked(data[i]);
    }
    pthread_mutex_unlock(&g_render_lock);
}
