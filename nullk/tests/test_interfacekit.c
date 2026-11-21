/*
 * Host tests for the portable INTERFACEKIT pieces: PS/2 decoders, RGBA surfaces and
 * framebuffer compositing. Hardware access stays in the AMD64 kernel driver;
 * these tests exercise the exact shared objects linked into the kernel and
 * init.elf.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nullk/abi64.h"
#include "nullk/interfacekit.h"
#include "nullk/interfacekit_widgets.h"

static uint32_t g_checks;
static uint32_t g_passed;

#define CHECK(name, expr)                                                      \
    do {                                                                       \
        g_checks++;                                                            \
        if ((expr)) {                                                          \
            g_passed++;                                                        \
        } else {                                                               \
            printf("FAIL interfacekit: %s\n", (name));                                 \
        }                                                                      \
    } while (0)

static uint32_t read_pixel(const uint8_t *base, uint32_t pitch,
                           uint32_t x, uint32_t y)
{
    uint32_t value;

    memcpy(&value, base + y * pitch + x * 4u, sizeof(value));
    return value;
}

static void test_decoders(void)
{
    uint8_t packet[3];
    int32_t dx;
    int32_t dy;
    uint32_t buttons;

    CHECK("scancode A", nk64_scancode_key(0x1eu, 0u) == 'A');
    CHECK("scancode 0", nk64_scancode_key(0x0bu, 0u) == '0');
    CHECK("scancode space", nk64_scancode_key(0x39u, 0u) == ' ');
    CHECK("scancode enter", nk64_scancode_key(0x1cu, 0u) == NK64_KEY_ENTER);
    CHECK("scancode extended up",
          nk64_scancode_key(0x48u, 1u) == NK64_KEY_UP);
    CHECK("scancode extended right ctrl",
          nk64_scancode_key(0x1du, 1u) == NK64_KEY_RCTRL);
    CHECK("scancode unknown", nk64_scancode_key(0xffu, 0u) == NK64_KEY_NONE);

    packet[0] = 0x01u;
    packet[1] = 0x10u;
    packet[2] = 0x00u;
    dx = 0;
    dy = 0;
    buttons = 0u;
    CHECK("mouse packet parse", nk64_mouse_packet(packet, &dx, &dy, &buttons));
    CHECK("mouse dx", dx == 16);
    CHECK("mouse dy", dy == 0);
    CHECK("mouse left button", buttons == (uint32_t)NK64_MOUSE_LEFT);

    packet[0] = 0x22u;
    packet[1] = 0xffu;
    packet[2] = 0x01u;
    dx = 0;
    dy = 0;
    buttons = 0u;
    CHECK("mouse signed parse", nk64_mouse_packet(packet, &dx, &dy, &buttons));
    CHECK("mouse negative dx", dx == -1);
    CHECK("mouse inverted dy", dy == 127);
    CHECK("mouse right button", buttons == (uint32_t)NK64_MOUSE_RIGHT);

    packet[0] = 0x40u;
    CHECK("mouse overflow rejected",
          nk64_mouse_packet(packet, &dx, &dy, &buttons) == 0);
    CHECK("mouse null inputs rejected",
          nk64_mouse_packet((const uint8_t *)0, &dx, &dy, &buttons) == 0);
}

static void test_pixel_formats(void)
{
    interfacekit_framebuffer_t fb;
    interfacekit_rgba_t white = interfacekit_rgba(255u, 255u, 255u, 255u);

    memset(&fb, 0, sizeof(fb));
    fb.width = 8u;
    fb.height = 4u;
    fb.pitch = 32u;
    fb.bpp = 32u;
    fb.red_size = 8u;
    fb.red_shift = 16u;
    fb.green_size = 8u;
    fb.green_shift = 8u;
    fb.blue_size = 8u;
    fb.blue_shift = 0u;

    CHECK("rgba packing", interfacekit_rgba(0x12u, 0x34u, 0x56u, 0x78u) ==
                              0x78123456u);
    CHECK("fb RGB888 pack",
          interfacekit_pixel_pack(&fb, interfacekit_rgba(0x12u, 0x34u, 0x56u, 255u)) ==
              0x00123456u);

    fb.bpp = 16u;
    fb.red_size = 5u;
    fb.red_shift = 11u;
    fb.green_size = 6u;
    fb.green_shift = 5u;
    fb.blue_size = 5u;
    fb.blue_shift = 0u;
    CHECK("fb RGB565 red",
          interfacekit_pixel_pack(&fb, interfacekit_rgba(255u, 0u, 0u, 255u)) == 0xf800u);
    CHECK("fb RGB565 white", interfacekit_pixel_pack(&fb, white) == 0xffffu);
}

static void test_pixmap(void)
{
    uint32_t pixels[16u * 8u];
    interfacekit_pixmap_t pm;
    uint32_t alpha_pixel;
    uint32_t glyph_pixels;
    uint32_t i;

    memset(pixels, 0, sizeof(pixels));
    interfacekit_pixmap_init(&pm, pixels, 16u, 8u, 16u);
    interfacekit_pixmap_clear(&pm);
    CHECK("pixmap contains", interfacekit_pixmap_contains(&pm, 2, 3));
    CHECK("pixmap rejects negative", !interfacekit_pixmap_contains(&pm, -1, 3));
    CHECK("pixmap rejects edge", !interfacekit_pixmap_contains(&pm, 16, 3));

    interfacekit_pixmap_blend(&pm, 2, 2, interfacekit_rgba(255u, 0u, 0u, 255u));
    CHECK("pixmap opaque blend",
          pixels[2u * 16u + 2u] == 0xffff0000u);
    interfacekit_pixmap_blend(&pm, 2, 2, interfacekit_rgba(0u, 0u, 0u, 0u));
    CHECK("pixmap transparent no-op",
          pixels[2u * 16u + 2u] == 0xffff0000u);
    interfacekit_pixmap_blend(&pm, 2, 2, interfacekit_rgba(0u, 255u, 0u, 128u));
    alpha_pixel = pixels[2u * 16u + 2u];
    CHECK("pixmap alpha red fades", ((alpha_pixel >> 16) & 0xffu) < 150u);
    CHECK("pixmap alpha green appears", ((alpha_pixel >> 8) & 0xffu) > 105u);
    CHECK("pixmap alpha output opaque", (alpha_pixel >> 24) == 255u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_pixmap_fill(&pm, (interfacekit_rect_t){1, 1, 4, 3},
                   interfacekit_rgba(0u, 0u, 255u, 255u));
    CHECK("pixmap fill inside", pixels[1u * 16u + 1u] == 0xff0000ffu);
    CHECK("pixmap fill far corner", pixels[3u * 16u + 4u] == 0xff0000ffu);
    CHECK("pixmap fill clip", pixels[4u * 16u + 1u] == 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_pixmap_frame(&pm, (interfacekit_rect_t){1, 1, 4, 4},
                    interfacekit_rgba(255u, 255u, 0u, 255u));
    CHECK("pixmap frame corner", pixels[1u * 16u + 1u] == 0xffffff00u);
    CHECK("pixmap frame far corner", pixels[4u * 16u + 4u] == 0xffffff00u);
    CHECK("pixmap frame hollow", pixels[2u * 16u + 2u] == 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_pixmap_text(&pm, 1, 1, "A", interfacekit_rgba(255u, 255u, 255u, 255u), 1u);
    glyph_pixels = 0u;
    for (i = 0u; i < 16u * 8u; i++) {
        if (pixels[i] != 0u) {
            glyph_pixels++;
        }
    }
    CHECK("font draws glyph", glyph_pixels >= 5u);
    CHECK("font lowercase maps to uppercase",
          memcmp(interfacekit_glyph('a'), interfacekit_glyph('A'), 3u) == 0);
    CHECK("font unknown maps to question",
          memcmp(interfacekit_glyph(0x7fu), interfacekit_glyph('?'), 3u) == 0);
}

static void test_framebuffer(void)
{
    uint8_t memory[4u * 32u];
    interfacekit_framebuffer_t fb;
    interfacekit_pixmap_t pm;
    uint32_t pm_pixels[2u * 2u];
    uint8_t before[sizeof(memory)];
    uint32_t value;

    memset(memory, 0, sizeof(memory));
    memset(&fb, 0, sizeof(fb));
    interfacekit_framebuffer_init(&fb, memory, 8u, 4u, 32u, 32u,
                                 8u, 16u, 8u, 8u, 8u, 0u);
    CHECK("framebuffer width", fb.width == 8u);
    CHECK("framebuffer pitch", fb.pitch == 32u);
    CHECK("framebuffer masks", fb.red_shift == 16u && fb.blue_size == 8u);

    interfacekit_fb_blend(&fb, 1, 1, interfacekit_rgba(0u, 0u, 255u, 255u));
    value = read_pixel(memory, 32u, 1u, 1u);
    CHECK("framebuffer opaque blue", (value & 0x00ffffffu) == 0x0000ffu);
    CHECK("framebuffer opaque alpha byte", (value >> 24) == 255u);

    interfacekit_fb_blend(&fb, -1, 1, interfacekit_rgba(255u, 0u, 0u, 255u));
    interfacekit_fb_blend(&fb, 8, 1, interfacekit_rgba(255u, 0u, 0u, 255u));
    CHECK("framebuffer clip left", read_pixel(memory, 32u, 0u, 1u) == 0u);
    CHECK("framebuffer clip right",
          read_pixel(memory, 32u, 7u, 1u) == 0u);

    memset(pm_pixels, 0, sizeof(pm_pixels));
    pm_pixels[0] = interfacekit_rgba(255u, 0u, 0u, 255u);
    pm_pixels[3] = interfacekit_rgba(0u, 255u, 0u, 128u);
    interfacekit_pixmap_init(&pm, pm_pixels, 2u, 2u, 2u);
    interfacekit_fb_blit(&fb, 4, 2, &pm, 255u);
    value = read_pixel(memory, 32u, 4u, 2u);
    CHECK("framebuffer blit opaque", (value & 0x00ffffffu) == 0xff0000u);
    value = read_pixel(memory, 32u, 5u, 3u);
    CHECK("framebuffer blit alpha", ((value >> 8) & 0xffu) > 100u);

    memcpy(before, memory, sizeof(memory));
    interfacekit_fb_cursor(&fb, 1, 1, interfacekit_rgba(255u, 255u, 255u, 255u));
    CHECK("framebuffer cursor drawn", memcmp(before, memory,
                                             sizeof(memory)) != 0);

    interfacekit_fb_fill(&fb, (interfacekit_rect_t){0, 0, 0, 4}, interfacekit_rgba(1u, 2u, 3u, 255u));
    memcpy(before, memory, sizeof(memory));
    interfacekit_fb_fill(&fb, (interfacekit_rect_t){0, 0, 8, 4}, interfacekit_rgba(0u, 0u, 0u, 0u));
    CHECK("framebuffer transparent fill no-op",
          memcmp(before, memory, sizeof(memory)) == 0);
}

static uint32_t count_ink(const interfacekit_pixmap_t *pm)
{
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0u; i < pm->stride * pm->height; i++) {
        if (pm->pixels[i] != 0u) {
            count++;
        }
    }
    return count;
}

static void test_widgets(void)
{
    uint32_t pixels[32u * 24u];
    interfacekit_pixmap_t pm;
    interfacekit_style_t style = interfacekit_style_default();
    interfacekit_style_t embedded = interfacekit_style_embedded();
    interfacekit_rect_t box = {2, 2, 20, 12};

    memset(pixels, 0, sizeof(pixels));
    interfacekit_pixmap_init(&pm, pixels, 32u, 24u, 32u);

    CHECK("full widget profile", INTERFACEKIT_PROFILE_EMBEDDED == 0);
    CHECK("common widgets always present",
          INTERFACEKIT_HAS_PANEL && INTERFACEKIT_HAS_LABEL &&
          INTERFACEKIT_HAS_BUTTON && INTERFACEKIT_HAS_PROGRESS);
    CHECK("desktop-only widgets present",
          INTERFACEKIT_HAS_TOGGLE && INTERFACEKIT_HAS_SLIDER);
    CHECK("embedded style is lighter", embedded.grain < style.grain &&
                                         embedded.bleed < style.bleed);

    interfacekit_glass_fill(&pm, box, interfacekit_rgba(80u, 120u, 180u, 120u),
                           &style);
    CHECK("grained glass fill draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_panel(&pm, box, &style);
    CHECK("glass panel draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_button(&pm, box, "OK", 1u, &style);
    CHECK("glass button draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_progress(&pm, box, 40u, &style);
    CHECK("glass progress draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_toggle(&pm, box, 1u, &style);
    CHECK("glass toggle draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_slider(&pm, box, 60u, &style);
    CHECK("glass slider draws", count_ink(&pm) > 0u);
}

int main(void)
{
    test_decoders();
    test_pixel_formats();
    test_pixmap();
    test_framebuffer();
    test_widgets();

    printf("InterfaceKit: %u/%u checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
