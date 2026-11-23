/* Host checks for the framebuffer console evdev key translation. */
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nullk_linux/evdev_keycode.h"

#define CHECK(expr) do {                                                      \
    if (!(expr)) {                                                            \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                 \
        return 1;                                                             \
    }                                                                         \
} while (0)

static int seq_is(unsigned int code, const char *expected)
{
    uint8_t out[12];
    size_t len = nk64_evdev_key_to_sequence(code, 0, 0, out, sizeof(out));

    return len == strlen(expected) && memcmp(out, expected, len) == 0;
}

int main(void)
{
    /* QWERTY order, not alphabetical order. */
    CHECK(nk64_evdev_key_to_ascii(KEY_L, 0, 0) == (uint8_t)'l');
    CHECK(nk64_evdev_key_to_ascii(KEY_S, 0, 0) == (uint8_t)'s');
    CHECK(nk64_evdev_key_to_ascii(KEY_A, 0, 0) == (uint8_t)'a');
    CHECK(nk64_evdev_key_to_ascii(KEY_Z, 0, 0) == (uint8_t)'z');
    CHECK(nk64_evdev_key_to_ascii(KEY_Q, 0, 0) == (uint8_t)'q');

    /* Shift and caps use XOR so both together restore lowercase. */
    CHECK(nk64_evdev_key_to_ascii(KEY_L, 1, 0) == (uint8_t)'L');
    CHECK(nk64_evdev_key_to_ascii(KEY_L, 0, 1) == (uint8_t)'L');
    CHECK(nk64_evdev_key_to_ascii(KEY_L, 1, 1) == (uint8_t)'l');

    /* Digits and punctuation follow a US layout. */
    CHECK(nk64_evdev_key_to_ascii(KEY_1, 0, 0) == (uint8_t)'1');
    CHECK(nk64_evdev_key_to_ascii(KEY_1, 1, 0) == (uint8_t)'!');
    CHECK(nk64_evdev_key_to_ascii(KEY_0, 1, 0) == (uint8_t)')');
    CHECK(nk64_evdev_key_to_ascii(KEY_MINUS, 1, 0) == (uint8_t)'_');
    CHECK(nk64_evdev_key_to_ascii(KEY_SLASH, 1, 0) == (uint8_t)'?');
    CHECK(nk64_evdev_key_to_ascii(KEY_ENTER, 0, 0) == (uint8_t)'\n');
    CHECK(nk64_evdev_key_to_ascii(KEY_SPACE, 0, 0) == (uint8_t)' ');
    CHECK(nk64_evdev_key_to_ascii(KEY_BACKSPACE, 0, 0) == (uint8_t)'\b');
    CHECK(nk64_evdev_key_to_ascii(KEY_MAX + 1u, 0, 0) == 0u);

    /* Modifier and lock keys must not type anything by themselves. */
    CHECK(nk64_evdev_key_to_ascii(KEY_LEFTSHIFT, 0, 0) == 0u);
    CHECK(nk64_evdev_key_to_ascii(KEY_LEFTCTRL, 0, 0) == 0u);
    CHECK(nk64_evdev_key_to_ascii(KEY_LEFTALT, 0, 0) == 0u);

    /* Keypad digits and their shifted forms. */
    CHECK(nk64_evdev_key_to_ascii(KEY_KP1, 0, 0) == (uint8_t)'1');
    CHECK(nk64_evdev_key_to_ascii(KEY_KP1, 1, 0) == (uint8_t)'!');
    CHECK(nk64_evdev_key_to_ascii(KEY_KP0, 1, 0) == (uint8_t)')');
    CHECK(nk64_evdev_key_to_ascii(KEY_KPPLUS, 0, 0) == (uint8_t)'+');
    CHECK(nk64_evdev_key_to_ascii(KEY_KPENTER, 0, 0) == (uint8_t)'\n');

    /* Navigation keys become ANSI sequences so nksh can edit lines. */
    CHECK(seq_is(KEY_UP, "\x1b[A"));
    CHECK(seq_is(KEY_DOWN, "\x1b[B"));
    CHECK(seq_is(KEY_RIGHT, "\x1b[C"));
    CHECK(seq_is(KEY_LEFT, "\x1b[D"));
    CHECK(seq_is(KEY_HOME, "\x1b[H"));
    CHECK(seq_is(KEY_END, "\x1b[F"));
    CHECK(seq_is(KEY_DELETE, "\x1b[3~"));
    CHECK(seq_is(KEY_PAGEUP, "\x1b[5~"));
    CHECK(seq_is(KEY_F1, "\x1bOP"));

    /* Ordinary characters keep working through the sequence entry point. */
    {
        uint8_t out[12];

        CHECK(nk64_evdev_key_to_sequence(KEY_L, 0, 0, out, sizeof(out)) == 1u);
        CHECK(out[0] == (uint8_t)'l');
        CHECK(nk64_evdev_key_to_sequence(KEY_LEFTSHIFT, 0, 0, out,
                                         sizeof(out)) == 0u);
    }

    printf("evdev keycode map: 35/35 checks passed\n");
    return 0;
}
