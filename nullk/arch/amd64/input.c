/*
 * Minimal PS/2 keyboard + mouse driver for the AMD64 port.
 *
 * The 8042 is polled with bounded loops and never sleeps: handlers run in IRQ
 * context and only append decoded events to a fixed ring. The UI server drains
 * that ring through NK64_SC_INPUT_READ. Ring 0 knows nothing about windows,
 * cursors or drawing.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop

#define NK64_PS2_DATA 0x60u
#define NK64_PS2_STATUS 0x64u
#define NK64_PS2_OUT_FULL 0x01u
#define NK64_PS2_IN_FULL 0x02u

#define NK64_INPUT_RING 64u
#define NK64_PS2_SPIN 200000u

static nk64_input_event_t g_events[NK64_INPUT_RING];
static uint32_t g_head;
static uint32_t g_tail;
static uint32_t g_count;

static uint8_t g_ready;
static uint8_t g_extended;
static uint8_t g_break;
static uint32_t g_modifiers;
static uint8_t g_caps;

static uint8_t g_mouse_packet[3];
static uint32_t g_mouse_index;
static int32_t g_mouse_x;
static int32_t g_mouse_y;
static uint32_t g_mouse_buttons;

static void ps2_wait_output(void)
{
    uint32_t i;

    for (i = 0u; i < NK64_PS2_SPIN; i++) {
        if ((nk64_inb(NK64_PS2_STATUS) & NK64_PS2_IN_FULL) == 0u) {
            return;
        }
    }
}

static void ps2_wait_input(void)
{
    uint32_t i;

    for (i = 0u; i < NK64_PS2_SPIN; i++) {
        if ((nk64_inb(NK64_PS2_STATUS) & NK64_PS2_OUT_FULL) != 0u) {
            return;
        }
    }
}

static void ps2_command(uint8_t command)
{
    ps2_wait_output();
    nk64_outb(NK64_PS2_STATUS, command);
}

static uint8_t ps2_read(void)
{
    ps2_wait_input();
    return nk64_inb(NK64_PS2_DATA);
}

static void ps2_drain(void)
{
    uint32_t i;

    for (i = 0u; i < 32u; i++) {
        if ((nk64_inb(NK64_PS2_STATUS) & NK64_PS2_OUT_FULL) == 0u) {
            return;
        }
        (void)nk64_inb(NK64_PS2_DATA);
    }
}

/* Send a byte to the mouse (0xD4) and consume its ACK when one arrives. */
static void ps2_mouse_write(uint8_t value)
{
    ps2_command(0xd4u);
    ps2_wait_output();
    nk64_outb(NK64_PS2_DATA, value);
    (void)ps2_read();
}

static void input_push(uint32_t type, uint32_t code, int32_t dx, int32_t dy)
{
    nk64_input_event_t *event;

    if (g_count >= (uint32_t)NK64_INPUT_RING) {
        return;                 /* drop newest instead of overwriting old input */
    }
    event = &g_events[g_tail];
    event->type = type;
    event->code = code;
    event->modifiers = g_modifiers;
    event->x = g_mouse_x;
    event->y = g_mouse_y;
    event->dx = dx;
    event->dy = dy;
    event->buttons = g_mouse_buttons;
    event->reserved = 0u;

    g_tail = (g_tail + 1u) % NK64_INPUT_RING;
    g_count++;
}

static uint32_t modifier_bit(uint32_t key)
{
    switch (key) {
    case NK64_KEY_LSHIFT:
    case NK64_KEY_RSHIFT: return NK64_MOD_SHIFT;
    case NK64_KEY_LCTRL:
    case NK64_KEY_RCTRL: return NK64_MOD_CTRL;
    case NK64_KEY_LALT:
    case NK64_KEY_RALT: return NK64_MOD_ALT;
    default: return 0u;
    }
}

void nk64_keyboard_handler(void)
{
    uint32_t spins;

    for (spins = 0u; spins < 32u; spins++) {
        uint8_t scancode;
        uint32_t key;
        uint32_t mod;

        if ((nk64_inb(NK64_PS2_STATUS) & NK64_PS2_OUT_FULL) == 0u) {
            return;
        }
        scancode = nk64_inb(NK64_PS2_DATA);
        if (scancode == 0xe0u) {
            g_extended = 1u;
            continue;
        }
        if (scancode == 0xf0u) {
            g_break = 1u;
            continue;
        }

        key = nk64_scancode_key(scancode, g_extended);
        g_extended = 0u;
        if (key == NK64_KEY_NONE) {
            g_break = 0u;
            continue;
        }

        mod = modifier_bit(key);
        if (g_break == 0u) {
            g_modifiers |= mod;
        } else {
            g_modifiers &= ~mod;
        }
        if (key >= 'a' && key <= 'z') {
            key = (uint32_t)(key - 'a' + 'A');
        }
        if (key >= 'A' && key <= 'Z' && g_caps != 0u &&
            (g_modifiers & NK64_MOD_SHIFT) == 0u) {
            key = (uint32_t)(key + ('a' - 'A'));
        } else if (key >= 'A' && key <= 'Z' && g_caps == 0u &&
                   (g_modifiers & NK64_MOD_SHIFT) != 0u) {
            key = (uint32_t)(key + ('a' - 'A'));
        }

        input_push(g_break == 0u ? NK64_INPUT_KEY_DOWN : NK64_INPUT_KEY_UP,
                   key, 0, 0);
        g_break = 0u;
    }
}

void nk64_mouse_handler(void)
{
    uint32_t spins;

    for (spins = 0u; spins < 32u; spins++) {
        uint8_t byte;
        int32_t dx;
        int32_t dy;
        uint32_t buttons;
        uint32_t changed;

        if ((nk64_inb(NK64_PS2_STATUS) & NK64_PS2_OUT_FULL) == 0u) {
            return;
        }
        byte = nk64_inb(NK64_PS2_DATA);

        if (g_mouse_index == 0u && (byte & 0x08u) == 0u) {
            continue;           /* resynchronise on the packet header bit */
        }
        g_mouse_packet[g_mouse_index++] = byte;
        if (g_mouse_index < 3u) {
            continue;
        }
        g_mouse_index = 0u;
        if (nk64_mouse_packet(g_mouse_packet, &dx, &dy, &buttons) == 0) {
            continue;
        }

        if (g_mouse_x + dx < 0) {
            g_mouse_x = 0;
        } else {
            g_mouse_x += dx;
        }
        if (g_mouse_y + dy < 0) {
            g_mouse_y = 0;
        } else {
            g_mouse_y += dy;
        }
        if (nk64_boot.fb.present != 0u) {
            if (g_mouse_x >= (int32_t)nk64_boot.fb.width) {
                g_mouse_x = (int32_t)nk64_boot.fb.width - 1;
            }
            if (g_mouse_y >= (int32_t)nk64_boot.fb.height) {
                g_mouse_y = (int32_t)nk64_boot.fb.height - 1;
            }
        }

        changed = buttons ^ g_mouse_buttons;
        g_mouse_buttons = buttons;
        input_push(NK64_INPUT_MOUSE_MOVE, 0u, dx, dy);
        if ((changed & NK64_MOUSE_LEFT) != 0u) {
            input_push((buttons & NK64_MOUSE_LEFT) != 0u
                           ? NK64_INPUT_MOUSE_DOWN : NK64_INPUT_MOUSE_UP,
                       NK64_MOUSE_LEFT, 0, 0);
        }
        if ((changed & NK64_MOUSE_RIGHT) != 0u) {
            input_push((buttons & NK64_MOUSE_RIGHT) != 0u
                           ? NK64_INPUT_MOUSE_DOWN : NK64_INPUT_MOUSE_UP,
                       NK64_MOUSE_RIGHT, 0, 0);
        }
    }
}

uint32_t nk64_input_read(nk64_input_event_t *out, uint32_t max)
{
    uint32_t n = 0u;

    if (out == 0 || max == 0u) {
        return 0u;
    }
    /* IRQ handlers append to the ring; keep the drain atomic. */
    nk64_irq_disable();
    while (n < max && g_count != 0u) {
        out[n++] = g_events[g_head];
        g_head = (g_head + 1u) % NK64_INPUT_RING;
        g_count--;
    }
    nk64_irq_enable();
    return n;
}

void nk64_input_init(void)
{
    uint8_t config;

    if (g_ready != 0u) {
        return;
    }
    g_ready = 1u;

    if (nk64_boot.fb.present != 0u) {
        g_mouse_x = (int32_t)(nk64_boot.fb.width / 2u);
        g_mouse_y = (int32_t)(nk64_boot.fb.height / 2u);
    }

    ps2_command(0xadu);                 /* disable keyboard */
    ps2_command(0xa7u);                 /* disable mouse */
    ps2_drain();

    ps2_command(0x20u);
    config = ps2_read();
    config |= 0x06u;                    /* IRQ1 + IRQ12 enabled */
    config &= (uint8_t)~0x20u;          /* mouse not disabled */
    ps2_command(0x60u);
    ps2_wait_output();
    nk64_outb(NK64_PS2_DATA, config);

    ps2_wait_output();
    nk64_outb(NK64_PS2_DATA, 0xf0u);    /* keyboard: scancode set 1 */
    ps2_wait_output();
    nk64_outb(NK64_PS2_DATA, 0x01u);
    (void)ps2_read();

    ps2_command(0xa8u);                 /* enable auxiliary device */
    ps2_mouse_write(0xf4u);             /* mouse: streaming mode */
    ps2_command(0xaeu);                 /* re-enable keyboard */

    nk64_pic_unmask(1u);
    nk64_pic_unmask(12u);
}
