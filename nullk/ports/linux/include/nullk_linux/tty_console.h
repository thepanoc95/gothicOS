#ifndef NULLK_LINUX_TTY_CONSOLE_H
#define NULLK_LINUX_TTY_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the kernel's VT/serial console as fd 0/1/2 (and as the controlling
 * terminal) for nksh. Returns 0 on success, -1 when no terminal is available;
 * the inherited /dev/console descriptors remain usable in that case. */
int nk64_tty_console_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LINUX_TTY_CONSOLE_H */
