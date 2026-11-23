/* Rounded 6x10 bitmap face for the null_kernel Linux framebuffer console.
 *
 * This is deliberately independent of InterfaceKit: the Linux port must paint
 * a readable CLI on a DRM/fbdev scanout without the desktop stack. Glyphs are
 * 6 pixels wide and 10 rows tall; bit 5 of each row is the leftmost column.
 * Capitals and ascenders occupy rows 0..7 (row 7 is the baseline), the x-height
 * body is rows 2..7 and descenders use rows 8..9.
 *
 * Lowercase has its own glyphs (unlike the 3x5 InterfaceKit face), so "l" is
 * not an "I" and "s" is not a "B". Rounded corners on O, C, G, S, 0, 6, 8, 9
 * and the bowls of b/d/p/q keep the CLI calm to read at 2x-3x scale.
 */
#ifndef NULLK_LINUX_FB_FONT_H
#define NULLK_LINUX_FB_FONT_H

#include <stdint.h>

#define NKFB_FONT_W        6u
#define NKFB_FONT_H        10u
#define NKFB_FONT_ADVANCE  7u  /* glyph plus one column of letter spacing */
#define NKFB_FONT_BASELINE 7u
#define NKFB_FONT_CHARS    95u /* printable ASCII 0x20..0x7e */

/* Returns NKFB_FONT_H row bytes for `ch`; unknown characters map to '?'. */
const uint8_t *nkfb_font_glyph(uint8_t ch);

#endif /* NULLK_LINUX_FB_FONT_H */
