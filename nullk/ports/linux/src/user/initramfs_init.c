/*
 * Minimal static PID 1 for the gothicOS Linux initramfs.
 *
 * The generated archive deliberately contains no shell and no insmod binary:
 * this binary mounts the basic pseudo-filesystems, loads the kernel modules
 * listed in /lib/nullk/kernel-modules (i915 and its dependencies on real
 * Intel hardware), loads /lib/nullk/nkabi3.ko when possible, prepares the @/
 * payload and execs the real ABI v3 init. It is also installed as /lib/init so
 * existing Limine entries using init=/lib/init keep working.
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
#define MODULE_ROOT "/lib/modules/6.6.162-nk"
#define MODULE_MANIFEST "/lib/nullk/kernel-modules"
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

static int load_module(const char *path, const char *args)
{
    struct stat st;
    unsigned char *image;
    ssize_t got;
    int fd;
    long rc;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[init64] %s absent; skipping\n", path);
        return -1;
    }
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x1000000) {
        fprintf(stderr, "[init64] %s has invalid size\n", path);
        close(fd);
        return -1;
    }
    image = malloc((size_t)st.st_size);
    if (image == NULL) {
        close(fd);
        return -1;
    }
    got = read(fd, image, (size_t)st.st_size);
    close(fd);
    if (got != (ssize_t)st.st_size) {
        fprintf(stderr, "[init64] failed to read %s\n", path);
        free(image);
        return -1;
    }

    rc = syscall(SYS_init_module, image, (size_t)st.st_size,
                 args != NULL ? args : "");
    if (rc != 0) {
        fprintf(stderr, "[init64] init_module %s failed: %s\n", path,
                strerror(errno));
        free(image);
        return -1;
    }
    free(image);
    fprintf(stderr, "[init64] loaded %s\n", path);
    return 0;
}

static void load_kernel_modules(void)
{
    char line[512];
    FILE *fp;

    fp = fopen(MODULE_MANIFEST, "r");
    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char name[256];
        char args[256];
        char path[sizeof(MODULE_ROOT) + sizeof(name) + 2];
        char *comment = strchr(line, '#');

        if (comment != NULL) {
            *comment = '\0';
        }
        args[0] = '\0';
        if (sscanf(line, "%255s %255[^\n]", name, args) < 1) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", MODULE_ROOT, name);
        (void)load_module(path, args);
    }
    fclose(fp);
}

static void wait_for_display(void)
{
    unsigned int i;

    for (i = 0; i < 20; ++i) {
        if (access("/dev/fb0", F_OK) == 0 ||
            access("/dev/dri/card0", F_OK) == 0) {
            return;
        }
        usleep(50000);
    }
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

    load_kernel_modules();
    wait_for_display();

    if (load_module(MODULE_PATH, "") == 0) {
        fputs("[init64] native Linux ABI v3 control plane attached\n",
              stderr);
    }
    (void)setenv("NULLK64_ROOT", NULLK_ROOT, 1);

    fputs("[init64] gothicOS Linux userspace init starting\n", stderr);
    execl(PAYLOAD_ELF, "nullk-linux.elf", (char *)NULL);
    fprintf(stderr, "[init64] exec %s failed: %s\n", PAYLOAD_ELF,
            strerror(errno));
    return 1;
}
