/* Linux evdev keycode -> ASCII translation for the standalone framebuffer
 * console.
 *
 * KEY_A..KEY_Z are *not* contiguous in alphabetical order: they follow the
 * QWERTY layout (KEY_A, KEY_S, KEY_D, ...). Treating them as 'a' + (code - KEY_A)
 * turns 'l' into 'i' and 's' into 'b'. Keep this table next to the console so
 * both the implementation and the host test agree on the mapping.
 */
#ifndef NULLK_LINUX_EVDEV_KEYCODE_H
#define NULLK_LINUX_EVDEV_KEYCODE_H

#include <linux/input.h>
#include <stdint.h>

static inline uint8_t nk64_evdev_key_to_ascii(unsigned int code, int shift,
                                              int caps)
{
    static const uint8_t plain[KEY_MAX + 1u] = {
        [KEY_ESC]       = 0x1bu,
        [KEY_1]         = '1',
        [KEY_2]         = '2',
        [KEY_3]         = '3',
        [KEY_4]         = '4',
        [KEY_5]         = '5',
        [KEY_6]         = '6',
        [KEY_7]         = '7',
        [KEY_8]         = '8',
        [KEY_9]         = '9',
        [KEY_0]         = '0',
        [KEY_MINUS]     = '-',
        [KEY_EQUAL]     = '=',
        [KEY_BACKSPACE] = '\b',
        [KEY_TAB]       = '\t',
        [KEY_Q]         = 'q',
        [KEY_W]         = 'w',
        [KEY_E]         = 'e',
        [KEY_R]         = 'r',
        [KEY_T]         = 't',
        [KEY_Y]         = 'y',
        [KEY_U]         = 'u',
        [KEY_I]         = 'i',
        [KEY_O]         = 'o',
        [KEY_P]         = 'p',
        [KEY_LEFTBRACE]  = '[',
        [KEY_RIGHTBRACE] = ']',
        [KEY_ENTER]     = '\n',
        [KEY_A]         = 'a',
        [KEY_S]         = 's',
        [KEY_D]         = 'd',
        [KEY_F]         = 'f',
        [KEY_G]         = 'g',
        [KEY_H]         = 'h',
        [KEY_J]         = 'j',
        [KEY_K]         = 'k',
        [KEY_L]         = 'l',
        [KEY_SEMICOLON] = ';',
        [KEY_APOSTROPHE] = '\'',
        [KEY_GRAVE]     = '`',
        [KEY_BACKSLASH] = '\\',
        [KEY_Z]         = 'z',
        [KEY_X]         = 'x',
        [KEY_C]         = 'c',
        [KEY_V]         = 'v',
        [KEY_B]         = 'b',
        [KEY_N]         = 'n',
        [KEY_M]         = 'm',
        [KEY_COMMA]     = ',',
        [KEY_DOT]       = '.',
        [KEY_SLASH]     = '/',
        [KEY_SPACE]     = ' ',
        [KEY_KP1]       = '1',
        [KEY_KP2]       = '2',
        [KEY_KP3]       = '3',
        [KEY_KP4]       = '4',
        [KEY_KP5]       = '5',
        [KEY_KP6]       = '6',
        [KEY_KP7]       = '7',
        [KEY_KP8]       = '8',
        [KEY_KP9]       = '9',
        [KEY_KP0]       = '0',
        [KEY_KPENTER]   = '\n',
        [KEY_KPMINUS]   = '-',
        [KEY_KPPLUS]    = '+',
        [KEY_KPDOT]     = '.',
        [KEY_KPSLASH]   = '/',
        [KEY_KPASTERISK] = '*',
#ifdef KEY_102ND
        [KEY_102ND]     = '\\',
#endif
    };
    static const uint8_t shifted[KEY_MAX + 1u] = {
        [KEY_1]          = '!',
        [KEY_2]          = '@',
        [KEY_3]          = '#',
        [KEY_4]          = '$',
        [KEY_5]          = '%',
        [KEY_6]          = '^',
        [KEY_7]          = '&',
        [KEY_8]          = '*',
        [KEY_9]          = '(',
        [KEY_0]          = ')',
        [KEY_MINUS]      = '_',
        [KEY_EQUAL]      = '+',
        [KEY_LEFTBRACE]  = '{',
        [KEY_RIGHTBRACE] = '}',
        [KEY_SEMICOLON]  = ':',
        [KEY_APOSTROPHE] = '"',
        [KEY_GRAVE]      = '~',
        [KEY_BACKSLASH]  = '|',
        [KEY_COMMA]      = '<',
        [KEY_DOT]        = '>',
        [KEY_SLASH]      = '?',
#ifdef KEY_102ND
        [KEY_102ND]      = '|',
#endif
    };
    uint8_t c;

    if (code > KEY_MAX) {
        return 0u;
    }
    c = plain[code];
    if (c >= (uint8_t)'a' && c <= (uint8_t)'z') {
        if ((shift ^ caps) != 0) {
            c = (uint8_t)(c - (uint8_t)'a' + (uint8_t)'A');
        }
        return c;
    }
    if (shift != 0 && shifted[code] != 0u) {
        return shifted[code];
    }
    return c;
}

#endif /* NULLK_LINUX_EVDEV_KEYCODE_H */
