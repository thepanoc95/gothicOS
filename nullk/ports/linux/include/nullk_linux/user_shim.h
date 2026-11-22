#ifndef NULLK_LINUX_USER_SHIM_H
#define NULLK_LINUX_USER_SHIM_H

#include <stddef.h>
#include <stdint.h>

#include "nullk/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open /dev/nkabi3, attach this process, and install the ABI v3 backend.
 * Returns the control fd or -1 when the LKM is not loaded; the userspace half
 * of the ABI still works. */
int nk64_linux_init(void);
int nk64_linux_control_fd(void);

/* Map "@/path", "@", "/" or a bare path into the initramfs root at /@. */
nk_status_t nk64_linux_map_path(const char *path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LINUX_USER_SHIM_H */
