/*
 * Minimal static PID 1 for the gothicOS Linux initramfs.
 *
 * The generated archive deliberately contains no shell and no insmod binary:
 * this binary mounts the basic pseudo-filesystems, loads /lib/nullk/nkabi3.ko
 * when possible, prepares the @/ payload and execs the real ABI v3 init. It is
 * also installed as /lib/init so existing Limine entries using init=/lib/init
 * keep working.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define INITRD_ROOT "/"
#define NULLK_ROOT  "/@"
#define MODULE_PATH "/lib/nullk/nkabi3.ko"
#define PAYLOAD_ELF "/lib/nullk/nullk-linux.elf"

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[init64] mkdir %s failed: %s\n", path,
                strerror(errno));
    }
}

static void mount_quiet(const char *type, const char *target)
{
    if (mount(type, target, type, 0u, NULL) != 0) {
        fprintf(stderr, "[init64] mount %s on %s failed: %s\n", type, target,
                strerror(errno));
    }
}

static int copy_file(const char *src, const char *dst)
{
    char buf[4096];
    ssize_t got;
    int in = -1;
    int out = -1;
    int rc = -1;

    in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) {
        return -1;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return -1;
    }
    while ((got = read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;

        while (written < got) {
            ssize_t n = write(out, buf + written, (size_t)(got - written));

            if (n <= 0) {
                goto done;
            }
            written += n;
        }
    }
    rc = (got < 0) ? -1 : 0;

done:
    if (close(out) != 0) {
        rc = -1;
    }
    (void)close(in);
    return rc;
}

static void load_module(void)
{
    struct stat st;
    unsigned char *image;
    ssize_t got;
    int fd;
    long rc;

    fd = open(MODULE_PATH, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[init64] %s absent; userspace-only mode\n",
                MODULE_PATH);
        return;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x100000) {
        fprintf(stderr, "[init64] %s has invalid size\n", MODULE_PATH);
        close(fd);
        return;
    }
    image = malloc((size_t)st.st_size);
    if (image == NULL) {
        close(fd);
        return;
    }
    got = read(fd, image, (size_t)st.st_size);
    close(fd);
    if (got != (ssize_t)st.st_size) {
        fprintf(stderr, "[init64] failed to read %s\n", MODULE_PATH);
        free(image);
        return;
    }

    rc = syscall(SYS_init_module, image, (size_t)st.st_size, "");
    free(image);
    if (rc != 0) {
        fprintf(stderr, "[init64] init_module failed: %s; "
                        "continuing in userspace-only mode\n",
                strerror(errno));
        return;
    }
    fputs("[init64] native Linux ABI v3 control plane attached\n", stderr);
}

int main(void)
{
    mount_quiet("proc", "/proc");
    mount_quiet("sysfs", "/sys");
    mount_quiet("devtmpfs", "/dev");

    ensure_dir("/@");
    ensure_dir("/@/data");
    ensure_dir("/@/data/system");
    ensure_dir("/@/data/user");
    ensure_dir("/@/data/user/bin");

    if (copy_file("/lib/nullk/payload/limine.cfg",
                  "/@/data/system/limine.cfg") != 0 ||
        copy_file("/lib/nullk/payload/null_kernel.elf",
                  "/@/data/system/null_kernel.elf") != 0) {
        fputs("[init64] payload staging incomplete\n", stderr);
    }

    load_module();
    (void)setenv("NULLK64_ROOT", NULLK_ROOT, 1);

    fputs("[init64] gothicOS Linux userspace init starting\n", stderr);
    execl(PAYLOAD_ELF, "nullk-linux.elf", (char *)NULL);
    fprintf(stderr, "[init64] exec %s failed: %s\n", PAYLOAD_ELF,
            strerror(errno));
    return 1;
}
