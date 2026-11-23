/* Standalone Linux framebuffer console for nksh.
 *
 * This console uses ABI v3 FB_INFO/FB_MAP directly. It does not depend on
 * InterfaceKit, the desktop, or Linux fbcon/VT, so nksh remains visible and
 * interactive on a DRM framebuffer even when tty0 has nowhere to paint.
 *
 * The symbols are weak in the syscall shim so host ABI-layout checks can link
 * without this implementation. The real definitions live in
 * src/user/fb_console.c and are included only in the userspace ELF.
 */
#ifndef NULLK_LINUX_FB_CONSOLE_H
#define NULLK_LINUX_FB_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* Requests accepted by nk64_fb_console_gui(); mirrors the shell's `gui`
 * subcommands. */
enum {
    NKFB_GUI_DEMO = 0,     /* paint the widget engine showcase */
    NKFB_GUI_CLOSE = 1,    /* return to the plain console grid */
    NKFB_GUI_PANEL_ON = 2, /* show the right-hand dashboard */
    NKFB_GUI_PANEL_OFF = 3,
    NKFB_GUI_REFRESH = 4
};

void nk64_fb_console_init(void) __attribute__((weak));
int nk64_fb_console_active(void) __attribute__((weak));
size_t nk64_fb_console_input_get(uint8_t *out, size_t cap)
    __attribute__((weak));
void nk64_fb_console_output_push(const uint8_t *data, size_t len)
    __attribute__((weak));
void nk64_fb_console_clear(void) __attribute__((weak));
/* Returns 0 when the framebuffer console handled the request, -1 when the
 * framebuffer/console is unavailable. */
int nk64_fb_console_gui(int command) __attribute__((weak));

#endif /* NULLK_LINUX_FB_CONSOLE_H */
