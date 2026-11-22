/*
 * Host-runnable checks for the native Linux port. No kernel headers or Linux
 * source tree are needed: this verifies the ABI contract shared with the LKM
 * and the userspace path translator.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libnk64.h"
#include "nullk/abi64.h"
#include "nullk_linux/abi3_ioctl.h"
#include "nullk_linux/abi64_kernel.h"
#include "nullk_linux/user_shim.h"

static unsigned int checks;
static unsigned int failures;

#define CHECK(expr)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(expr)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
        }                                                                      \
    } while (0)

#define CHECK_OFF(a, b, field)                                                 \
    CHECK(offsetof(a, field) == offsetof(b, field))

int main(void)
{
    char out[64];

    CHECK(NULLK64_ABI_VERSION == NK64_K_ABI_VERSION);
    CHECK(NULLK64_ABI_MAGIC == NK64_K_ABI_MAGIC);
    CHECK(NK64_SC_COUNT == NK64_K_SC_COUNT);
    CHECK(NK64_SC_GETINFO == NK64_K_SC_GETINFO);
    CHECK(NK64_SC_USB_REGISTER == NK64_K_SC_USB_REGISTER);

    CHECK(sizeof(nk64_info_t) == sizeof(nk64_k_info_t));
    CHECK_OFF(nk64_info_t, nk64_k_info_t, abi_version);
    CHECK_OFF(nk64_info_t, nk64_k_info_t, magic);
    CHECK_OFF(nk64_info_t, nk64_k_info_t, ticks);
    CHECK_OFF(nk64_info_t, nk64_k_info_t, flags);
    CHECK_OFF(nk64_info_t, nk64_k_info_t, mem_total);

    CHECK(sizeof(nk64_fbinfo_t) == sizeof(nk64_k_fbinfo_t));
    CHECK_OFF(nk64_fbinfo_t, nk64_k_fbinfo_t, phys);
    CHECK_OFF(nk64_fbinfo_t, nk64_k_fbinfo_t, width);
    CHECK_OFF(nk64_fbinfo_t, nk64_k_fbinfo_t, pitch);
    CHECK_OFF(nk64_fbinfo_t, nk64_k_fbinfo_t, bpp);
    CHECK_OFF(nk64_fbinfo_t, nk64_k_fbinfo_t, present);

    CHECK(sizeof(nk64_input_event_t) == sizeof(nk64_k_input_event_t));
    CHECK_OFF(nk64_input_event_t, nk64_k_input_event_t, type);
    CHECK_OFF(nk64_input_event_t, nk64_k_input_event_t, x);
    CHECK_OFF(nk64_input_event_t, nk64_k_input_event_t, buttons);

    CHECK(NK64_ABI3_IO_VERSION == 1u);
    CHECK(sizeof(nk64_abi3_call_t) == 72u);
    CHECK(sizeof(nk64_parasite_status_t) == 40u);

    memset(out, 0, sizeof(out));
    CHECK(nk64_linux_map_path("@/data/user/bin/README.txt", out,
                              sizeof(out)) == NK_STATUS_OK);
    CHECK(strcmp(out, "/@/data/user/bin/README.txt") == 0);

    memset(out, 0, sizeof(out));
    CHECK(nk64_linux_map_path("@", out, sizeof(out)) == NK_STATUS_OK);
    CHECK(strcmp(out, "/@") == 0);

    memset(out, 0, sizeof(out));
    CHECK(nk64_linux_map_path("@/../etc/passwd", out,
                              sizeof(out)) == NK_STATUS_INVALID_ARGUMENT);

    (void)setenv("NULLK64_ROOT", "/tmp/nkroot", 1);
    memset(out, 0, sizeof(out));
    CHECK(nk64_linux_map_path("@/data", out, sizeof(out)) == NK_STATUS_OK);
    CHECK(strcmp(out, "/tmp/nkroot/data") == 0);
    (void)unsetenv("NULLK64_ROOT");

    {
        static const char root[] = "/tmp/nk64_linux_port_test";
        static const char payload[] = "nk64";
        char path[80];
        char buf[16];
        uint64_t handle;
        int fd;

        (void)mkdir(root, 0700);
        (void)snprintf(path, sizeof(path), "%s/file", root);
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        CHECK(fd >= 0);
        if (fd >= 0) {
            CHECK(write(fd, payload, sizeof(payload) - 1u) ==
                  (ssize_t)(sizeof(payload) - 1u));
            (void)close(fd);
        }
        (void)setenv("NULLK64_ROOT", root, 1);
        handle = nk64_open("@/file", NK64_OPEN_READ);
        CHECK(handle >= 2u);
        memset(buf, 0, sizeof(buf));
        CHECK(nk64_read((uint32_t)handle, buf, sizeof(buf)) ==
              (int64_t)(sizeof(payload) - 1u));
        CHECK(strcmp(buf, payload) == 0);
        CHECK(nk64_close(handle) == NK_STATUS_OK);
        (void)unsetenv("NULLK64_ROOT");
        (void)unlink(path);
        (void)rmdir(root);
    }

    printf("nullk linux port: %u/%u checks passed\n", checks - failures,
           checks);
    return failures == 0u ? 0 : 1;
}
