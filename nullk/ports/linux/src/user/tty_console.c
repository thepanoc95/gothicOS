/*
 * Linux terminal console for PID 1.
 *
 * nksh already speaks plain bytes on fd 0/1, so the console belongs to the
 * kernel's VT or serial tty rather than to a private TUI painted on the DRM
 * scanout. Configure the terminal for the shell's own line editor: no kernel
 * echo, no canonical buffering and no ISIG, so Ctrl+C/Ctrl+L reach nksh and
 * arrow keys are delivered as escape sequences.
 *
 * Failure is deliberately non-fatal: when booted with init=/init the kernel
 * already gives PID 1 fd 0/1/2 on /dev/console.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "nullk_linux/tty_console.h"

/* /dev/console follows the kernel's console= argument (tty0 or ttyS0). The
 * others are fallbacks for systems where /dev/console cannot be opened. */
static const char *const g_tty_paths[] = {
    "/dev/console",
    "/dev/tty0",
    "/dev/ttyS0"
};

int nk64_tty_console_init(void)
{
    struct termios tio;
    size_t i;
    int fd = -1;

    for (i = 0u; i < sizeof(g_tty_paths) / sizeof(g_tty_paths[0]); i++) {
        fd = open(g_tty_paths[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            break;
        }
    }
    if (fd < 0) {
        return -1;
    }

    if (isatty(fd) != 0) {
        if (tcgetattr(fd, &tio) == 0) {
            tio.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG | IEXTEN);
            tio.c_iflag &= (tcflag_t)~(IXON | IXOFF | INLCR | IGNCR);
            tio.c_cc[VMIN] = 1;
            tio.c_cc[VTIME] = 0;
            (void)tcsetattr(fd, TCSANOW, &tio);
        }
        /* PID 1 is already a session leader, so setsid() returns EPERM there.
         * Other callers need it before TIOCSCTTY can claim the terminal. */
        (void)setsid();
        (void)ioctl(fd, TIOCSCTTY, 0);
    }

    if ((fd != STDIN_FILENO && dup2(fd, STDIN_FILENO) < 0) ||
        (fd != STDOUT_FILENO && dup2(fd, STDOUT_FILENO) < 0) ||
        (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) < 0)) {
        if (fd > STDERR_FILENO) {
            close(fd);
        }
        return -1;
    }
    if (fd > STDERR_FILENO) {
        close(fd);
    }
    return 0;
}
