/*
 * Pure PS/2 decoders. No ports, no globals, no platform headers: the AMD64
 * input driver and the host tests consume exactly the same code.
 *
 * Keyboard scancodes are the legacy set 1 bytes emitted by a real or emulated
 * 8042 by default. Prefix handling (0xE0 extended, 0xF0 break) lives in the
 * driver; this table only maps one final byte to an ABI key code.
 */
#include <stdint.h>

#include "nullk/abi64.h"

uint32_t nk64_scancode_key(uint8_t scancode, uint32_t extended)
{
    if (extended != 0u) {
        switch (scancode) {
        case 0x48u: return NK64_KEY_UP;
        case 0x4bu: return NK64_KEY_LEFT;
        case 0x4du: return NK64_KEY_RIGHT;
        case 0x50u: return NK64_KEY_DOWN;
        case 0x47u: return NK64_KEY_HOME;
        case 0x4fu: return NK64_KEY_END;
        case 0x49u: return NK64_KEY_PAGEUP;
        case 0x51u: return NK64_KEY_PAGEDOWN;
        case 0x52u: return NK64_KEY_INSERT;
        case 0x53u: return NK64_KEY_DELETE;
        case 0x1cu: return NK64_KEY_ENTER;
        case 0x1du: return NK64_KEY_RCTRL;
        case 0x38u: return NK64_KEY_RALT;
        default: return NK64_KEY_NONE;
        }
    }

    switch (scancode) {
    /* Letters */
    case 0x1eu: return 'A';
    case 0x30u: return 'B';
    case 0x2eu: return 'C';
    case 0x20u: return 'D';
    case 0x12u: return 'E';
    case 0x21u: return 'F';
    case 0x22u: return 'G';
    case 0x23u: return 'H';
    case 0x17u: return 'I';
    case 0x24u: return 'J';
    case 0x25u: return 'K';
    case 0x26u: return 'L';
    case 0x32u: return 'M';
    case 0x31u: return 'N';
    case 0x18u: return 'O';
    case 0x19u: return 'P';
    case 0x10u: return 'Q';
    case 0x13u: return 'R';
    case 0x1fu: return 'S';
    case 0x14u: return 'T';
    case 0x16u: return 'U';
    case 0x2fu: return 'V';
    case 0x11u: return 'W';
    case 0x2du: return 'X';
    case 0x15u: return 'Y';
    case 0x2cu: return 'Z';

    /* Digits and punctuation */
    case 0x02u: return '1';
    case 0x03u: return '2';
    case 0x04u: return '3';
    case 0x05u: return '4';
    case 0x06u: return '5';
    case 0x07u: return '6';
    case 0x08u: return '7';
    case 0x09u: return '8';
    case 0x0au: return '9';
    case 0x0bu: return '0';
    case 0x0cu: return '-';
    case 0x0du: return '=';
    case 0x1bu: return '[';
    case 0x2bu: return ']';
    case 0x27u: return ';';
    case 0x28u: return '\'';
    case 0x29u: return '`';
    case 0x33u: return ',';
    case 0x34u: return '.';
    case 0x35u: return '/';
    case 0x56u: return '\\';

    /* Controls */
    case 0x0eu: return NK64_KEY_BACKSPACE;
    case 0x0fu: return NK64_KEY_TAB;
    case 0x1cu: return NK64_KEY_ENTER;
    case 0x2au: return NK64_KEY_LSHIFT;
    case 0x36u: return NK64_KEY_RSHIFT;
    case 0x1du: return NK64_KEY_LCTRL;
    case 0x38u: return NK64_KEY_LALT;
    case 0x39u: return ' ';
    case 0x3au: return NK64_KEY_CAPSLOCK;
    case 0x01u: return NK64_KEY_ESCAPE;
    case 0x47u: return NK64_KEY_HOME;
    case 0x4fu: return NK64_KEY_END;
    case 0x49u: return NK64_KEY_PAGEUP;
    case 0x51u: return NK64_KEY_PAGEDOWN;
    case 0x4bu: return NK64_KEY_LEFT;
    case 0x4du: return NK64_KEY_RIGHT;
    case 0x48u: return NK64_KEY_UP;
    case 0x50u: return NK64_KEY_DOWN;
    case 0x52u: return NK64_KEY_INSERT;
    case 0x53u: return NK64_KEY_DELETE;
    default: return NK64_KEY_NONE;
    }
}

int nk64_mouse_packet(const uint8_t bytes[3], int32_t *dx, int32_t *dy,
                      uint32_t *buttons)
{
    uint8_t flags;
    int32_t mx;
    int32_t my;
    uint32_t mask;

    if (bytes == 0 || dx == 0 || dy == 0 || buttons == 0) {
        return 0;
    }
    flags = bytes[0];
    if ((flags & 0xc0u) != 0u) {
        /* Overflow or a synchronisation byte: the packet is unusable. */
        return 0;
    }

    mx = (int8_t)bytes[1];
    my = (int8_t)bytes[2];
    if ((flags & 0x10u) != 0u) {
        mx = (int32_t)(int8_t)(bytes[1] | 0x80u);
    }
    if ((flags & 0x20u) != 0u) {
        my = (int32_t)(int8_t)(bytes[2] | 0x80u);
    }

    mask = 0u;
    if ((flags & 0x01u) != 0u) {
        mask |= (uint32_t)NK64_MOUSE_LEFT;
    }
    if ((flags & 0x02u) != 0u) {
        mask |= (uint32_t)NK64_MOUSE_RIGHT;
    }
    if ((flags & 0x04u) != 0u) {
        mask |= (uint32_t)NK64_MOUSE_MIDDLE;
    }

    *dx = mx;
    *dy = -my;              /* PS/2 positive Y is upward; UI Y grows down. */
    *buttons = mask;
    return 1;
}
