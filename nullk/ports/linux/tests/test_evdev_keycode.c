/* Host checks for the framebuffer console evdev key translation. */
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>

#include "nullk_linux/evdev_keycode.h"

#define CHECK(expr) do {                                                      \
    if (!(expr)) {                                                            \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                 \
        return 1;                                                             \
    }                                                                         \
} while (0)

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
    CHECK(nk64_evdev_key_to_ascii(KEY_MAX + 1u, 0, 0) == 0u);

    printf("evdev keycode map: 16/16 checks passed\n");
    return 0;
}
