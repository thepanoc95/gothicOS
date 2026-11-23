/*
 * Standalone framebuffer console for the native Linux ABI v3 port.
 *
 * This is deliberately not InterfaceKit and not Linux fbcon. It uses only the
 * ABI v3 FB_INFO/FB_MAP primitives and raw evdev keyboard events, so nksh stays
 * visible and interactive on a DRM framebuffer when tty0 has no painter (for
 * example virtio-gpu with CONFIG_FB and DRM_FBDEV_EMULATION disabled).
 *
 * Layout: a title bar, the character grid, a live GUI dashboard on the right
 * (status cards, progress bar, shortcut list) and a status bar. Everything is
 * drawn with the port's own 6x10 rounded font and widget engine, so the CLI
 * looks intentional without any desktop stack.
 *
 * Console output from the init and nksh threads is mirrored here by the syscall
 * shim; keyboard events become bytes (plus ANSI sequences for arrows/Home/End/
 * Delete) and are queued for nksh's fd 0 reads. EV_KEY autorepeat (value 2) is
 * ignored, so holding a key no longer floods the line with one letter.
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
#include "nullk/printk.h"
#include "nullk_linux/evdev_keycode.h"
#include "nullk_linux/fb_console.h"
#include "nullk_linux/fb_font.h"
#include "nullk_linux/fb_gfx.h"
#include "nullk_linux/fb_gui.h"

#define FB_CONSOLE_MAX_COLS     200u
#define FB_CONSOLE_MAX_ROWS     64u
#define FB_CONSOLE_INPUT_BYTES  512u
#define FB_CONSOLE_KEYBOARDS    8u
#define FB_CONSOLE_EVENT_PATHS  32u
#define FB_CONSOLE_EVENT_BATCH  32u
#define FB_CONSOLE_PANEL_MIN    260u
#define FB_CONSOLE_PANEL_MAX    420u

static nkfb_surface_t g_surf;
static nkfb_ui_t g_ui;
static const nkfb_theme_t *g_theme = &nkfb_theme_dark;
static int g_ready;
static int g_overlay;      /* `gui demo` element showcase is on screen */
static int g_panel_on = 1; /* right-hand dashboard */

static uint32_t g_width;
static uint32_t g_height;
static uint32_t g_scale;
static uint32_t g_pad;
static uint32_t g_cell_w;
static uint32_t g_cell_h;
static uint32_t g_header_x;
static uint32_t g_header_y;
static uint32_t g_header_w;
static uint32_t g_header_h;
static uint32_t g_status_x;
static uint32_t g_status_y;
static uint32_t g_status_w;
static uint32_t g_status_h;
static uint32_t g_panel_x;
static uint32_t g_panel_y;
static uint32_t g_panel_w;
static uint32_t g_panel_h;
static int32_t g_text_x;
static int32_t g_text_y;

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
static int g_ctrl;
static int g_caps;
static pthread_t g_kbd_thread;
static int g_kbd_thread_started;

/* ------------------------------------------------------------------ */
/* Text grid painting.                                                 */
/* ------------------------------------------------------------------ */

static void draw_glyph(uint8_t ch, int32_t x, int32_t y, uint32_t color)
{
    nkfb_glyph(&g_surf, ch, x, y, color, g_scale);
}

static void draw_cursor_locked(void)
{
    uint32_t col;
    int32_t x;
    int32_t y;
    uint32_t bar_h = 2u * g_scale;

    if (g_cursor_row >= g_rows || g_cols == 0u) {
        return;
    }
    col = (g_cursor_col < g_cols) ? g_cursor_col : (g_cols - 1u);
    x = g_text_x + (int32_t)(col * g_cell_w);
    y = g_text_y + (int32_t)(g_cursor_row * g_cell_h) +
        (int32_t)(g_cell_h - bar_h - g_scale);
    nkfb_fill_round(&g_surf, x, y, g_cell_w - g_scale, bar_h, g_scale,
                    g_theme->accent);
}

static void draw_row_locked(uint32_t row)
{
    uint32_t col;

    if (row >= g_rows) {
        return;
    }
    nkfb_fill(&g_surf, g_text_x, g_text_y + (int32_t)(row * g_cell_h),
              g_cols * g_cell_w, g_cell_h, g_theme->panel);
    for (col = 0u; col < g_cols; col++) {
        char c = g_text[row][col];

        if ((uint8_t)c >= 0x20u && (uint8_t)c <= 0x7eu) {
            draw_glyph((uint8_t)c,
                       g_text_x + (int32_t)(col * g_cell_w),
                       g_text_y + (int32_t)(row * g_cell_h) + (int32_t)g_scale,
                       g_theme->text);
        }
    }
    if (row == g_cursor_row) {
        draw_cursor_locked();
    }
}

static void redraw_text_locked(void)
{
    uint32_t row;

    for (row = 0u; row < g_rows; row++) {
        draw_row_locked(row);
    }
}

static void scroll_rows_locked(void)
{
    if (g_rows > 1u) {
        memmove(g_text[0], g_text[1],
                (size_t)((g_rows - 1u) * (FB_CONSOLE_MAX_COLS + 1u)));
    }
    memset(g_text[g_rows - 1u], ' ', g_cols);
    g_text[g_rows - 1u][g_cols] = '\0';
    g_cursor_row = g_rows - 1u;
}

static int newline_locked(void)
{
    int scrolled = 0;

    if (g_cursor_row + 1u < g_rows) {
        g_cursor_row++;
    } else {
        scroll_rows_locked();
        scrolled = 1;
    }
    g_cursor_col = 0u;
    return scrolled;
}

static void clear_grid_locked(void)
{
    uint32_t row;

    memset(g_text, ' ', sizeof(g_text));
    for (row = 0u; row < FB_CONSOLE_MAX_ROWS; row++) {
        g_text[row][FB_CONSOLE_MAX_COLS] = '\0';
    }
    g_cursor_row = 0u;
    g_cursor_col = 0u;
}

/* ------------------------------------------------------------------ */
/* Frame painting.                                                     */
/* ------------------------------------------------------------------ */

static void format_clock(char *out, size_t cap, uint64_t ticks, uint32_t hz)
{
    uint64_t hz_safe = (hz != 0u) ? hz : 100u;
    uint64_t seconds = ticks / hz_safe;

    (void)snprintf(out, cap, "%u:%02u:%02u", (unsigned)(seconds / 3600u),
                   (unsigned)((seconds % 3600u) / 60u),
                   (unsigned)(seconds % 60u));
}

static void fill_info(nkfb_gui_info_t *info)
{
    nk64_info_t ki;
    char clock[24];

    memset(info, 0, sizeof(*info));
    memset(&ki, 0, sizeof(ki));
    (void)nk64_getinfo(&ki);
    format_clock(clock, sizeof(clock), ki.ticks, ki.tick_hz);

    info->title = "null_kernel";
    info->subtitle = "gothicOS CLI GUI engine";
    info->abi = "ABI v3";
    info->mode = "Linux LKM";
    info->width = g_width;
    info->height = g_height;
    info->bpp = g_surf.bpp;
    info->cols = g_cols;
    info->rows = g_rows;
    info->used_rows = g_cursor_row + 1u;
    info->keyboards = (uint32_t)((g_kbd_count > 0) ? g_kbd_count : 0);
    info->tasks = ki.task_count;
    info->tick_hz = (ki.tick_hz != 0u) ? ki.tick_hz : 100u;
    info->ticks = ki.ticks;
    (void)clock;
}

static void draw_header_locked(void)
{
    char right[64];
    uint32_t r = g_scale * 3u + 2u;
    int32_t text_y = (int32_t)(g_header_y + g_header_h / 2u) -
                     (int32_t)((NKFB_FONT_H * g_scale) / 2u);
    int32_t right_w;

    (void)snprintf(right, sizeof(right), "ABI v3  |  %ux%u @ %ubpp",
                   g_width, g_height, (unsigned)g_surf.bpp);
    nkfb_fill_round(&g_surf, (int32_t)g_header_x, (int32_t)g_header_y,
                    g_header_w, g_header_h, r, g_theme->panel_alt);
    nkfb_stroke_round(&g_surf, (int32_t)g_header_x, (int32_t)g_header_y,
                      g_header_w, g_header_h, r, 1u, g_theme->border);
    nkfb_fill(&g_surf, (int32_t)(g_header_x + r), (int32_t)g_header_y +
              (int32_t)g_header_h - 2, g_header_w - 2u * r, 2u,
              g_theme->accent_dim);

    (void)nkfb_text(&g_surf, (int32_t)g_header_x + (int32_t)g_pad, text_y,
                    "null_kernel", g_theme->accent, g_scale);
    (void)nkfb_text(&g_surf,
                    (int32_t)g_header_x + (int32_t)g_pad +
                        (int32_t)nkfb_text_width("null_kernel ", g_scale),
                    text_y, "framebuffer console", g_theme->text, g_scale);
    right_w = (int32_t)nkfb_text_width(right, g_scale);
    if ((int32_t)g_header_x + (int32_t)g_header_w - right_w -
            (int32_t)g_pad > 0) {
        (void)nkfb_text(&g_surf,
                        (int32_t)g_header_x + (int32_t)g_header_w - right_w -
                            (int32_t)g_pad,
                        text_y, right, g_theme->text_dim, g_scale);
    }
}

static void draw_status_locked(void)
{
    char left[64];
    char right[80];
    char clock[24];
    nk64_info_t ki;

    memset(&ki, 0, sizeof(ki));
    (void)nk64_getinfo(&ki);
    format_clock(clock, sizeof(clock), ki.ticks, ki.tick_hz);
    (void)snprintf(left, sizeof(left), "nk) shell  %ux%u  %ux", g_cols, g_rows,
                   g_scale);
    (void)snprintf(right, sizeof(right), "%u task(s)  |  up %s  |  %s",
                   ki.task_count, clock,
                   g_panel_on ? "gui panel" : "console only");
    nkfb_ui_statusbar(&g_ui, (int32_t)g_status_x, (int32_t)g_status_y,
                      g_status_w, g_status_h, left, right);
}

static void draw_panel_locked(void)
{
    nkfb_gui_info_t info;

    if (g_panel_on == 0 || g_panel_w == 0u) {
        return;
    }
    fill_info(&info);
    nkfb_gui_dashboard(&g_ui, (int32_t)g_panel_x, (int32_t)g_panel_y, g_panel_w,
                       g_panel_h, &info);
}

static void repaint_locked(void)
{
    nkfb_clip_reset(&g_surf);
    nkfb_fill(&g_surf, 0, 0, g_width, g_height, g_theme->bg);
    draw_header_locked();
    nkfb_fill_round(&g_surf, g_text_x - (int32_t)g_pad / 2,
                    g_text_y - (int32_t)g_pad / 2,
                    g_cols * g_cell_w + g_pad, g_rows * g_cell_h + g_pad,
                    g_scale * 2u + 2u, g_theme->panel);
    nkfb_stroke_round(&g_surf, g_text_x - (int32_t)g_pad / 2,
                      g_text_y - (int32_t)g_pad / 2,
                      g_cols * g_cell_w + g_pad, g_rows * g_cell_h + g_pad,
                      g_scale * 2u + 2u, 1u, g_theme->border);
    draw_panel_locked();
    draw_status_locked();
    redraw_text_locked();
}

/* ------------------------------------------------------------------ */
/* Geometry.                                                           */
/* ------------------------------------------------------------------ */

static void compute_layout(void)
{
    int32_t avail;
    uint32_t w = g_width;
    uint32_t h = g_height;

    g_scale = (w >= 720u && h >= 480u) ? 2u : 1u;
    g_pad = 3u * g_scale + 4u;
    g_cell_w = NKFB_FONT_ADVANCE * g_scale;
    g_cell_h = (NKFB_FONT_H + 2u) * g_scale;
    g_header_x = g_pad / 2u;
    g_header_y = g_pad / 2u;
    g_header_w = w - g_pad;
    g_header_h = g_cell_h + g_pad;
    g_status_h = g_cell_h + g_pad / 2u;
    g_status_x = g_pad / 2u;
    g_status_y = h - g_pad / 2u - g_status_h;
    g_status_w = w - g_pad;
    g_text_x = (int32_t)g_pad;
    g_text_y = (int32_t)(g_header_y + g_header_h + g_pad / 2u);

    g_panel_w = 0u;
    if (g_panel_on != 0 && w >= 900u) {
        g_panel_w = w / 3u;
        if (g_panel_w < FB_CONSOLE_PANEL_MIN) {
            g_panel_w = FB_CONSOLE_PANEL_MIN;
        }
        if (g_panel_w > FB_CONSOLE_PANEL_MAX) {
            g_panel_w = FB_CONSOLE_PANEL_MAX;
        }
    }
    g_panel_x = (g_panel_w != 0u) ?
                    (w - g_pad / 2u - g_panel_w) : (w - g_pad / 2u);
    g_panel_y = (uint32_t)g_text_y;
    g_panel_h = (g_status_y > g_pad && g_status_y - g_pad > g_panel_y) ?
                    (g_status_y - g_pad / 2u - g_panel_y) : 0u;

    avail = (int32_t)(g_panel_x - g_pad) - g_text_x;
    if (avail < (int32_t)g_cell_w) {
        avail = (int32_t)(g_status_x + g_status_w) - g_text_x -
                (int32_t)g_pad;
    }
    g_cols = (avail > 0) ? (uint32_t)(avail / (int32_t)g_cell_w) : 1u;
    if (g_cols > FB_CONSOLE_MAX_COLS) {
        g_cols = FB_CONSOLE_MAX_COLS;
    }
    avail = (int32_t)g_status_y - (int32_t)g_pad / 2 - g_text_y;
    g_rows = (avail > 0) ? (uint32_t)(avail / (int32_t)g_cell_h) : 1u;
    if (g_rows > FB_CONSOLE_MAX_ROWS) {
        g_rows = FB_CONSOLE_MAX_ROWS;
    }
    if (g_cols == 0u) {
        g_cols = 1u;
    }
    if (g_rows == 0u) {
        g_rows = 1u;
    }
    if (g_cursor_col >= g_cols) {
        g_cursor_col = g_cols - 1u;
    }
    if (g_cursor_row >= g_rows) {
        g_cursor_row = g_rows - 1u;
    }
}

static void draw_showcase_locked(void)
{
    nkfb_gui_info_t info;

    fill_info(&info);
    nkfb_gui_showcase(&g_ui, 0, 0, g_width, g_height, &info);
}

/* ------------------------------------------------------------------ */
/* Character output.                                                   */
/* ------------------------------------------------------------------ */

/* `paint` is 0 while the modal `gui demo` showcase owns the screen: output
 * still updates the character grid so nothing is lost, but it must not draw
 * over the demo (and must not dismiss it, or the shell's own prompt would
 * cancel the showcase immediately). */
static void put_char_locked(uint8_t byte, int paint)
{
    uint32_t old_row = g_cursor_row;
    char c = (char)byte;
    int scrolled = 0;

    if (c == '\f') {
        clear_grid_locked();
        if (paint != 0) {
            repaint_locked();
        }
        return;
    }
    if (c == '\r') {
        g_cursor_col = 0u;
    } else if (c == '\n') {
        scrolled = newline_locked();
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
            scrolled |= newline_locked();
        }
        g_text[g_cursor_row][g_cursor_col] = c;
        g_cursor_col++;
        if (g_cursor_col >= g_cols) {
            scrolled |= newline_locked();
        }
    } else {
        return;
    }

    if (paint == 0) {
        return;
    }
    if (scrolled != 0) {
        redraw_text_locked();
        return;
    }
    draw_row_locked(old_row);
    if (g_cursor_row != old_row) {
        draw_row_locked(g_cursor_row);
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

/* ------------------------------------------------------------------ */
/* Keyboard.                                                           */
/* ------------------------------------------------------------------ */

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
    uint8_t seq[12];
    size_t len;
    size_t i;
    int press;

    if (ev->type != EV_KEY) {
        return;
    }
    /* EV_KEY value 2 is autorepeat. Treating it as a new press is what made a
     * single held key repeat forever on the console. */
    if (ev->value == 2) {
        return;
    }
    press = (ev->value != 0);
    if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
        g_shift = press;
        return;
    }
    if (ev->code == KEY_LEFTCTRL || ev->code == KEY_RIGHTCTRL) {
        g_ctrl = press;
        return;
    }
    if (ev->code == KEY_CAPSLOCK) {
        if (press) {
            g_caps = !g_caps;
        }
        return;
    }
    if (!press) {
        return;
    }
    if (g_overlay != 0) {
        /* The showcase is a modal demo: dismiss it instead of typing into the
         * hidden shell prompt. */
        g_overlay = 0;
        pthread_mutex_lock(&g_render_lock);
        repaint_locked();
        pthread_mutex_unlock(&g_render_lock);
        return;
    }

    len = nk64_evdev_key_to_sequence((unsigned int)ev->code, g_shift, g_caps,
                                     seq, sizeof(seq));
    if (len == 0u) {
        return;
    }
    if (g_ctrl != 0 && len == 1u && seq[0] >= (uint8_t)'a' &&
        seq[0] <= (uint8_t)'z') {
        seq[0] = (uint8_t)(seq[0] - (uint8_t)'a' + 1u);
    }
    for (i = 0u; i < len; i++) {
        input_push(seq[i]);
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

/* ------------------------------------------------------------------ */
/* Public entry points.                                                */
/* ------------------------------------------------------------------ */

void nk64_fb_console_init(void)
{
    nk64_fbinfo_t info;
    uint64_t base;
    static const char banner[] =
        "[fbconsole] rounded 6x10 text, GUI dashboard, evdev input\n";

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

    nkfb_surface_init(&g_surf, (void *)(uintptr_t)base, info.width, info.height,
                      (uint32_t)info.pitch, (uint8_t)info.bpp,
                      info.red_size, info.red_shift, info.green_size,
                      info.green_shift, info.blue_size, info.blue_shift);
    nkfb_ui_init(&g_ui, &g_surf, g_theme, 2u);

    g_width = info.width;
    g_height = info.height;

    if (g_width <= 32u || g_height <= 64u) {
        return;
    }
    compute_layout();
    if (g_cols == 0u || g_rows == 0u) {
        return;
    }
    nkfb_ui_init(&g_ui, &g_surf, g_theme, g_scale);
    clear_grid_locked();

    pthread_mutex_lock(&g_render_lock);
    repaint_locked();
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
        put_char_locked(data[i], g_overlay == 0);
    }
    if (g_overlay == 0) {
        /* Keep the dashboard honest: tick counter, buffer use, task count. */
        draw_panel_locked();
        draw_status_locked();
    }
    pthread_mutex_unlock(&g_render_lock);
}

void nk64_fb_console_clear(void)
{
    if (g_ready == 0) {
        return;
    }
    pthread_mutex_lock(&g_render_lock);
    g_overlay = 0;
    clear_grid_locked();
    repaint_locked();
    pthread_mutex_unlock(&g_render_lock);
}

int nk64_fb_console_gui(int command)
{
    if (g_ready == 0) {
        return -1;
    }
    pthread_mutex_lock(&g_render_lock);
    switch (command) {
    case NKFB_GUI_DEMO:
        g_overlay = 1;
        draw_showcase_locked();
        break;
    case NKFB_GUI_CLOSE:
        g_overlay = 0;
        repaint_locked();
        break;
    case NKFB_GUI_PANEL_ON:
        g_panel_on = 1;
        compute_layout();
        repaint_locked();
        break;
    case NKFB_GUI_PANEL_OFF:
        g_panel_on = 0;
        compute_layout();
        repaint_locked();
        break;
    case NKFB_GUI_REFRESH:
    default:
        g_overlay = 0;
        repaint_locked();
        break;
    }
    pthread_mutex_unlock(&g_render_lock);
    return 0;
}
