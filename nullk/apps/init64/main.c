/*
 * init64 - first ring 3 task of the AMD64 port.
 *
 * The kernel loads this ELF, maps a NULL pointer at the top of the user stack
 * and jumps here. All further setup (VFS walk, USB service, shell) happens in
 * userspace through ABI v3 syscalls.
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/printk.h"

/* C ABI entry point from the userspace SecureKernel C++ layer. */
void securekernel_service_entry(void);

static const char *const vfs_checks[] = {
    "@/",
    "@/data",
    "@/data/system",
    "@/data/system/limine.cfg",
    "@/data/system/null_kernel.elf"
};

static void log_line(uint32_t level, const char *text)
{
    (void)nk64_log(level, text, (uint32_t)nk_strlen(text));
}

static void report_vfs(void)
{
    for (uint32_t i = 0; i < sizeof(vfs_checks) / sizeof(vfs_checks[0]); i++) {
        nk_vfs_stat_t st;
        uint32_t rc;

        nk_memset(&st, 0, sizeof(st));
        rc = nk64_stat(vfs_checks[i], &st);
        if (rc == 0 && (st.type == NK_VFS_DIR || st.type == NK_VFS_FILE)) {
            if (st.type == NK_VFS_DIR) {
                nk_printf("init64: vfs: node %s: dir\n", vfs_checks[i]);
            } else {
                nk_printf("init64: vfs: node %s: %u bytes\n",
                          vfs_checks[i], st.size);
            }
            log_line(NK_KERN_INFO, "init64: vfs: node present");
        } else {
            nk_printf("init64: vfs: node %s missing (status %u)\n",
                      vfs_checks[i], rc);
            log_line(NK_KERN_WARN, "init64: vfs: node missing");
        }
    }
}

void _start(void)
{
    nk64_info_t info;
    uint64_t secure_id;
    uint64_t usb_id;
    uint64_t shell_id;
    uint32_t rc;

    nk_printf("\ninit64: boot: userspace init (ABI v3, ring 3)\n");
    nk_memset(&info, 0, sizeof(info));
    rc = nk64_getinfo(&info);
    if (rc != 0) {
        nk_printf("init64: getinfo failed (status %u)\n", rc);
        log_line(NK_KERN_ERR, "init64: getinfo failed");
        nk64_exit(1);
    }

    nk_printf("init64: abi: %u.%u magic=0x%x runlevel=%u tick_hz=%u "
              "tasks=%u mem=%u KiB\n",
              (info.abi_version >> 16) & 0xffu,
              (info.abi_version >> 8) & 0xffu,
              info.magic, info.runlevel, info.tick_hz, info.task_count,
              (uint32_t)(info.mem_total >> 10));

    if (info.magic != NULLK64_ABI_MAGIC ||
        info.abi_version != NULLK64_ABI_VERSION) {
        nk_printf("init64: abi mismatch; halting\n");
        log_line(NK_KERN_ERR, "init64: abi mismatch");
        nk64_exit(2);
    }

    report_vfs();

    /* All services live in this ELF image; only the kernel is in ring 0.
     * No desktop is started here: gothicOS userland opts in via `ui start`. */
    secure_id = nk64_task_create("SecureKernel", securekernel_service_entry,
                                 0, 0);
    if (secure_id == 0) {
        nk_printf("init64: securekernel: start failed\n");
        log_line(NK_KERN_ERR, "init64: securekernel: start failed");
        nk64_exit(3);
    }
    nk_printf("init64: securekernel: start ... done (task %u)\n",
              (uint32_t)secure_id);

    usb_id = nk64_task_create("usb64", usb_service_entry, 0, 0);
    if (usb_id == 0) {
        nk_printf("init64: usb64: start failed\n");
        log_line(NK_KERN_ERR, "init64: usb64: start failed");
        nk64_exit(4);
    }
    nk_printf("init64: usb64: start ... done (task %u)\n",
              (uint32_t)usb_id);

    shell_id = nk64_task_create("nksh", shell_service_entry, 0, 0);
    if (shell_id == 0) {
        nk_printf("init64: nksh: start failed\n");
        log_line(NK_KERN_ERR, "init64: nksh: start failed");
        nk64_exit(5);
    }
    nk_printf("init64: nksh: start ... done (task %u)\n",
              (uint32_t)shell_id);

    nk_printf("init64: services: online\n");
    log_line(NK_KERN_INFO, "init64: services: online");

    /* Monitor forever; never fall through out of _start. */
    for (;;) {
        (void)nk64_sleep(100);
        (void)nk64_yield();
    }
}
