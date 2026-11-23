/* Linux evdev keycode -> console input translation.
 *
 * Two separate bugs used to make the Linux console unusable:
 *
 * 1. KEY_A..KEY_Z are *not* contiguous in alphabetical order; they follow the
 *    QWERTY layout (KEY_A, KEY_S, KEY_D, ...). Treating them as
 *    'a' + (code - KEY_A) turns 'l' into 'i' and 's' into 'b'. The table below
 *    is explicit for that reason.
 * 2. evdev sends EV_KEY value 2 for autorepeat. Feeding those to the input
 *    queue repeats one keystroke dozens of times (the "IIIIIIII" flood). the
 *    console ignores value 2, so only real presses and releases count.
 *
 * Editing/navigation keys have no ASCII value; they become the usual ANSI
 * escape sequences so nksh can offer a real line editor without a TTY.
 */
#ifndef NULLK_LINUX_EVDEV_KEYCODE_H
#define NULLK_LINUX_EVDEV_KEYCODE_H

#include <linux/input.h>
#include <stddef.h>
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
#ifdef KEY_KPEQUAL
        [KEY_KPEQUAL]   = '=',
#endif
#ifdef KEY_KPCOMMA
        [KEY_KPCOMMA]   = ',',
#endif
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
        [KEY_KP1]        = '!',
        [KEY_KP2]        = '@',
        [KEY_KP3]        = '#',
        [KEY_KP4]        = '$',
        [KEY_KP5]        = '%',
        [KEY_KP6]        = '^',
        [KEY_KP7]        = '&',
        [KEY_KP8]        = '*',
        [KEY_KP9]        = '(',
        [KEY_KP0]        = ')',
        [KEY_KPMINUS]    = '_',
        [KEY_KPPLUS]     = '+',
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

/* Full console translation: one ASCII byte, or an ANSI escape sequence for
 * cursor/navigation keys. Returns the number of bytes written to `out`.
 * `shift`/`caps` are modifier states; the caller handles Ctrl separately so
 * Ctrl+letter still reads as a control byte. */
static inline size_t nk64_evdev_key_to_sequence(unsigned int code, int shift,
                                                int caps, uint8_t *out,
                                                size_t cap)
{
    static const struct {
        unsigned int code;
        const char *seq;
    } specials[] = {
        {KEY_UP, "\x1b[A"},
        {KEY_DOWN, "\x1b[B"},
        {KEY_RIGHT, "\x1b[C"},
        {KEY_LEFT, "\x1b[D"},
        {KEY_HOME, "\x1b[H"},
        {KEY_END, "\x1b[F"},
        {KEY_INSERT, "\x1b[2~"},
        {KEY_DELETE, "\x1b[3~"},
        {KEY_PAGEUP, "\x1b[5~"},
        {KEY_PAGEDOWN, "\x1b[6~"},
        {KEY_F1, "\x1bOP"},
        {KEY_F2, "\x1bOQ"},
        {KEY_F3, "\x1bOR"},
        {KEY_F4, "\x1bOS"},
        {KEY_F5, "\x1b[15~"},
        {KEY_F6, "\x1b[17~"},
    };
    uint8_t ch;
    size_t i;
    size_t len = 0u;

    if (out == NULL || cap == 0u) {
        return 0u;
    }
    ch = nk64_evdev_key_to_ascii(code, shift, caps);
    if (ch != 0u) {
        out[0] = ch;
        return 1u;
    }
    for (i = 0u; i < sizeof(specials) / sizeof(specials[0]); i++) {
        if (specials[i].code != code) {
            continue;
        }
        while (specials[i].seq[len] != '\0' && len < cap) {
            out[len] = (uint8_t)specials[i].seq[len];
            len++;
        }
        return len;
    }
    return 0u;
}

#endif /* NULLK_LINUX_EVDEV_KEYCODE_H */
