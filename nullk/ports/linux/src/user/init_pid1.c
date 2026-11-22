/*
 * gothicOS Linux init (PID 1 replacement).
 *
 * initramfs mounts /proc, /sys and /dev before exec'ing this binary. It loads
 * the ABI v3 backend, then hands control to the same userspace init used by
 * the AMD64 port. SecureKernel, usb64 and nksh become Linux threads sharing
 * one address space, preserving the AMD64 task model without a second kernel.
 */
#include <stdio.h>
#include <unistd.h>

#include "nullk_linux/user_shim.h"

/* apps/init64/main.c is compiled with -D_start=nk64_amd64_init_start. */
void nk64_amd64_init_start(void);

int main(void)
{
    int ctl = nk64_linux_init();

    if (ctl < 0) {
        fputs("[init64] /dev/nkabi3 unavailable; userspace ABI only\n", stderr);
    } else {
        fputs("[init64] native Linux ABI v3 control plane attached\n", stderr);
    }
    nk64_amd64_init_start();
    return 0; /* unreachable */
}
