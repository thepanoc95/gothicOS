/*
 * Host test for the reduced InterfaceKit profile used by embedded images.
 * It is compiled with -DINTERFACEKIT_EMBEDDED and linked only with the portable
 * drawing objects, proving that boards without ABI v3 can use the common
 * watercolor/grained-glass widgets.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
            printf("FAIL embedded InterfaceKit: %s\n", (name));                \
        }                                                                      \
    } while (0)

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

int main(void)
{
    uint32_t pixels[24u * 16u];
    interfacekit_pixmap_t pm;
    interfacekit_style_t style = interfacekit_style_embedded();
    interfacekit_rect_t box = {1, 1, 16, 10};

    memset(pixels, 0, sizeof(pixels));
    interfacekit_pixmap_init(&pm, pixels, 24u, 16u, 24u);

    CHECK("embedded profile selected", INTERFACEKIT_PROFILE_EMBEDDED == 1);
    CHECK("core widgets retained",
          INTERFACEKIT_HAS_PANEL && INTERFACEKIT_HAS_LABEL &&
          INTERFACEKIT_HAS_BUTTON && INTERFACEKIT_HAS_PROGRESS);
    CHECK("advanced widgets removed",
          INTERFACEKIT_HAS_TOGGLE == 0 && INTERFACEKIT_HAS_SLIDER == 0);
    CHECK("embedded glass is frugal", style.grain <= 32u && style.bleed == 0u);

    interfacekit_glass_fill(&pm, box, interfacekit_rgba(60u, 110u, 170u, 96u),
                           &style);
    CHECK("embedded glass fill draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_panel(&pm, box, &style);
    CHECK("embedded panel draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_label(&pm, box, "OK", &style);
    CHECK("embedded label draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_button(&pm, box, "GO", 0u, &style);
    CHECK("embedded button draws", count_ink(&pm) > 0u);

    interfacekit_pixmap_clear(&pm);
    interfacekit_widget_progress(&pm, box, 75u, &style);
    CHECK("embedded progress draws", count_ink(&pm) > 0u);

    printf("InterfaceKit embedded: %u/%u checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
