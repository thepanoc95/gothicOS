/* NullKernRT Linux kernel module.
 *
 * Two modes:
 *   1. Default: a thin character device used as a transport by the userspace port.
 *   2. Opt-in (`handoff=1`): a confined NullKern core runs on CPUs 1..N-1 while Linux
 *      keeps CPU 0. The module never enumerates or terminates host processes. It only
 *      starts NullKern-owned kernel threads and registers an init ELF/shell service
 *      through the platform-neutral state machine in kernel/linux/handoff.c.
 *
 * The native core and handoff state machine are freestanding C; no CPython, Cython or
 * C++ runtime ever runs in kernel space.
 */
#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/cpumask.h>
#include <linux/wait.h>

#include "include/gothicos_linux_port.h"
#include "gothicos/nk_core.h"
#include "gothicos/nk_handoff.h"

/* Compile the freestanding sources into this single module translation unit. This keeps
 * the shipped `nullkern_lkm.ko` self-contained without duplicating the core or handoff
 * logic: firmware, userspace and the LKM all run these exact source files. */
#include "../../kernel/core/c/nk_core.c"
#include "../../kernel/linux/handoff.c"

#define GK_DEVICE_NAME "nullkern"
#define GK_CLASS_NAME "nullkern"
#define GK_DEFAULT_INIT_PATH "/lib/gothicos/system/init.d.elf"
#define GK_DEFAULT_SHELL_PATH "/lib/gothicos/user/bin/sh.uts"

static dev_t gk_devno;
static struct cdev gk_cdev;
static struct class *gk_class;
static struct device *gk_device;
static DEFINE_MUTEX(gk_mutex);
static DECLARE_WAIT_QUEUE_HEAD(gk_wait);

static struct {
    unsigned int irq_count;
    unsigned int message_count;
    unsigned int schedule_hints;
    unsigned int active_nodes;
} gk_state;

/* --- opt-in handoff state ------------------------------------------------ */

static bool handoff;
module_param(handoff, bool, 0444);
MODULE_PARM_DESC(handoff, "Opt in to confined NullKern handoff (default: off)");

static unsigned int first_managed_cpu = 1;
module_param(first_managed_cpu, uint, 0444);
MODULE_PARM_DESC(first_managed_cpu, "First CPU given to NullKern; CPU 0 is never given");

static unsigned int managed_cpus;
module_param(managed_cpus, uint, 0444);
MODULE_PARM_DESC(managed_cpus, "Number of managed CPUs (0 = all CPUs above first_managed_cpu)");

static char *init_path = GK_DEFAULT_INIT_PATH;
module_param(init_path, charp, 0444);
MODULE_PARM_DESC(init_path, "NullKern init ELF loaded during handoff");

static char *shell_path = GK_DEFAULT_SHELL_PATH;
module_param(shell_path, charp, 0444);
MODULE_PARM_DESC(shell_path, "UmbraTS shell service started after init");

static DEFINE_MUTEX(gk_handoff_mutex);
static DEFINE_MUTEX(gk_core_mutex);
static nk_handoff_t g_handoff;
static nk_kernel_t *g_native;
static void *g_native_storage;
static struct task_struct *g_native_threads[64];
static unsigned int g_native_thread_count;
static unsigned int g_managed_cpu_count;
static unsigned int g_first_managed_cpu;

static void gk_log_hook(void *context, const char *message)
{
    (void)context;
    pr_info("nullkern handoff: %s\n", message);
}

static int gk_bind_cpu_hook(void *context, unsigned int cpu)
{
    (void)context;
    /* Hard safety gate: CPU 0 is Linux's, and only configured CPUs may be claimed. */
    if (cpu == 0 || cpu < g_first_managed_cpu || cpu >= nr_cpu_ids) {
        return -1;
    }
    return 0;
}

static int gk_load_elf_hook(void *context, const char *path, void **image,
                            unsigned int *size)
{
    struct file *file;
    unsigned char header[4];
    loff_t offset = 0;
    ssize_t bytes;
    loff_t file_size;
    void *buffer;

    (void)context;
    if (path == NULL || image == NULL || size == NULL) {
        return -1;
    }
    file = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_warn("nullkern handoff: cannot open init image %s\n", path);
        return -1;
    }
    bytes = kernel_read(file, header, sizeof(header), &offset);
    if (bytes != (ssize_t)sizeof(header) || header[0] != 0x7f || header[1] != 'E' ||
        header[2] != 'L' || header[3] != 'F') {
        pr_warn("nullkern handoff: %s is not an ELF image\n", path);
        fput(file);
        return -1;
    }
    file_size = i_size_read(file_inode(file));
    if (file_size <= 0 || file_size > 16 * 1024 * 1024) {
        fput(file);
        return -1;
    }
    buffer = vmalloc((unsigned long)file_size);
    if (buffer == NULL) {
        fput(file);
        return -1;
    }
    offset = 0;
    bytes = kernel_read(file, buffer, (size_t)file_size, &offset);
    fput(file);
    if (bytes != (ssize_t)file_size) {
        vfree(buffer);
        return -1;
    }
    *image = buffer;
    *size = (unsigned int)file_size;
    return 0;
}

static int gk_init_step(void *kernel, int task_id, void *context)
{
    (void)kernel;
    (void)task_id;
    (void)context;
    /* One-shot init anchor: the core marks the task stopped after this step. */
    return 0;
}

static int gk_ensure_core(void)
{
    int status = 0;
    mutex_lock(&gk_core_mutex);
    if (g_native == NULL) {
        g_native_storage = vmalloc(nk_kernel_storage_size());
        if (g_native_storage != NULL) {
            g_native = nk_kernel_init(g_native_storage, nk_kernel_storage_size(),
                                      g_managed_cpu_count, g_first_managed_cpu);
        }
        if (g_native != NULL) {
            status = nk_boot(g_native);
            if (status != GOTHICOS_STATUS_OK) {
                pr_warn("nullkern handoff: native boot failed (%d)\n", status);
            }
        } else {
            status = -ENOMEM;
            vfree(g_native_storage);
            g_native_storage = NULL;
        }
    }
    mutex_unlock(&gk_core_mutex);
    return status;
}

static int gk_start_init_hook(void *context, void *image, unsigned int size)
{
    int task_id;
    (void)context;
    (void)image;
    (void)size;
    if (gk_ensure_core() != 0) {
        return -1;
    }
    task_id = nk_task_spawn(g_native, "init", gk_init_step, NULL);
    if (task_id < 0) {
        return -1;
    }
    pr_info("nullkern handoff: init task %d registered\n", task_id);
    /* Executing a native ELF image inside the kernel needs a firmware/sandbox loader.
     * Until one is registered the image is verified and retained, not executed. */
    return 0;
}

static int gk_start_service_hook(void *context, const char *path)
{
    (void)context;
    pr_info("nullkern handoff: registered service %s\n", path == NULL ? "(none)" : path);
    return 0;
}

static const nk_handoff_hooks_t g_handoff_hooks = {
    .bind_cpu = gk_bind_cpu_hook,
    .load_elf = gk_load_elf_hook,
    .start_init = gk_start_init_hook,
    .start_service = gk_start_service_hook,
    .log = gk_log_hook,
    .context = NULL,
};

static int gk_native_thread(void *data)
{
    unsigned long cpu = (unsigned long)data;
    if (gk_ensure_core() != 0) {
        return -1;
    }
    while (!kthread_should_stop()) {
        mutex_lock(&gk_core_mutex);
        if (g_native != NULL) {
            nk_run(g_native, 64);
        }
        mutex_unlock(&gk_core_mutex);
        msleep_interruptible(10);
    }
    pr_debug("nullkern: managed CPU %lu stopping\n", cpu);
    return 0;
}

static int gk_start_managed_threads(void)
{
    unsigned int index;
    unsigned int first = g_first_managed_cpu;
    for (index = 0; index < g_managed_cpu_count &&
                    index < ARRAY_SIZE(g_native_threads); index++) {
        unsigned int cpu = first + index;
        struct task_struct *thread;
        if (gk_bind_cpu_hook(NULL, cpu) != 0) {
            return -EINVAL;
        }
        thread = kthread_create(gk_native_thread, (void *)(unsigned long)cpu,
                                "nullkern/%u", cpu);
        if (IS_ERR(thread)) {
            unsigned int created;
            for (created = 0; created < g_native_thread_count; created++) {
                kthread_stop(g_native_threads[created]);
                g_native_threads[created] = NULL;
            }
            g_native_thread_count = 0;
            return PTR_ERR(thread);
        }
        kthread_bind(thread, cpu);
        g_native_threads[g_native_thread_count++] = thread;
        wake_up_process(thread);
    }
    return 0;
}

static void gk_stop_handoff(void);

static int gk_start_handoff(unsigned int enabled, unsigned int first_cpu,
                            unsigned int cpu_count)
{
    nk_handoff_config_t config;
    int status;

    /* Restart cleanly instead of accumulating threads from an earlier handoff. */
    gk_stop_handoff();

    memset(&config, 0, sizeof(config));
    config.enabled = enabled ? 1 : 0;
    config.first_managed_cpu = first_cpu;
    config.managed_cpu_count = cpu_count;
    strncpy(config.init_path, init_path, sizeof(config.init_path) - 1);
    strncpy(config.shell_path, shell_path, sizeof(config.shell_path) - 1);

    mutex_lock(&gk_handoff_mutex);
    g_first_managed_cpu = first_cpu;
    g_managed_cpu_count = cpu_count;
    status = nk_handoff_configure(&g_handoff, &config, &g_handoff_hooks);
    if (status == GOTHICOS_STATUS_OK && enabled) {
        status = nk_handoff_start(&g_handoff);
        if (status == GOTHICOS_STATUS_OK) {
            status = gk_start_managed_threads();
        }
    }
    mutex_unlock(&gk_handoff_mutex);
    if (status != GOTHICOS_STATUS_OK) {
        pr_warn("nullkern handoff failed: %d\n", status);
    }
    return status;
}

static void gk_stop_handoff(void)
{
    unsigned int index;
    mutex_lock(&gk_handoff_mutex);
    for (index = 0; index < g_native_thread_count; index++) {
        if (g_native_threads[index] != NULL) {
            kthread_stop(g_native_threads[index]);
            g_native_threads[index] = NULL;
        }
    }
    g_native_thread_count = 0;
    if (g_handoff.init_image != NULL) {
        vfree(g_handoff.init_image);
        g_handoff.init_image = NULL;
    }
    nk_handoff_stop(&g_handoff);
    mutex_lock(&gk_core_mutex);
    if (g_native != NULL) {
        nk_kernel_shutdown(g_native);
        g_native = NULL;
    }
    if (g_native_storage != NULL) {
        vfree(g_native_storage);
        g_native_storage = NULL;
    }
    mutex_unlock(&gk_core_mutex);
    mutex_unlock(&gk_handoff_mutex);
}

/* --- character device --------------------------------------------------- */

static int gk_open(struct inode *inode, struct file *file)
{
    mutex_lock(&gk_mutex);
    gk_state.active_nodes++;
    mutex_unlock(&gk_mutex);
    return 0;
}

static int gk_release(struct inode *inode, struct file *file)
{
    mutex_lock(&gk_mutex);
    if (gk_state.active_nodes > 0) {
        gk_state.active_nodes--;
    }
    mutex_unlock(&gk_mutex);
    return 0;
}

static int gk_handoff_status(gothicos_lkm_handoff_info_t __user *destination)
{
    gothicos_lkm_handoff_info_t info;
    memset(&info, 0, sizeof(info));
    mutex_lock(&gk_handoff_mutex);
    info.state = (uint32_t)g_handoff.state;
    info.enabled = (uint32_t)g_handoff.config.enabled;
    info.first_managed_cpu = g_handoff.config.first_managed_cpu;
    info.managed_cpu_count = g_handoff.config.managed_cpu_count;
    info.bound_cpu_count = g_handoff.bound_cpu_count;
    info.started_services = g_handoff.started_services;
    info.init_image_size = g_handoff.init_image_size;
    strncpy(info.init_path, g_handoff.config.init_path, sizeof(info.init_path) - 1);
    strncpy(info.shell_path, g_handoff.config.shell_path, sizeof(info.shell_path) - 1);
    strncpy(info.error, g_handoff.error, sizeof(info.error) - 1);
    mutex_unlock(&gk_handoff_mutex);
    if (copy_to_user(destination, &info, sizeof(info))) {
        return -EFAULT;
    }
    return 0;
}

static long gk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    gothicos_lkm_request_t request;
    gothicos_lkm_info_t info;
    gothicos_lkm_handoff_request_t handoff_request;
    unsigned int first_cpu;
    unsigned int cpu_count;

    switch (cmd) {
    case GOTHICOS_LKM_IOC_GET_INFO:
        memset(&info, 0, sizeof(info));
        mutex_lock(&gk_mutex);
        info.abi_major = 1;
        info.abi_minor = 1;
        info.abi_patch = 1;
        info.capabilities = 31;
        info.irq_count = gk_state.irq_count;
        info.message_count = gk_state.message_count;
        info.schedule_hints = gk_state.schedule_hints;
        info.active_nodes = gk_state.active_nodes;
        mutex_unlock(&gk_mutex);
        if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
            return -EFAULT;
        }
        return 0;

    case GOTHICOS_LKM_IOC_RAISE_IRQ:
    case GOTHICOS_LKM_IOC_PORT_SEND:
    case GOTHICOS_LKM_IOC_SCHED_HINT:
        memset(&request, 0, sizeof(request));
        if (copy_from_user(&request, (void __user *)arg, sizeof(request))) {
            return -EFAULT;
        }
        mutex_lock(&gk_mutex);
        if (cmd == GOTHICOS_LKM_IOC_RAISE_IRQ) {
            gk_state.irq_count++;
        } else if (cmd == GOTHICOS_LKM_IOC_PORT_SEND) {
            gk_state.message_count++;
        } else {
            gk_state.schedule_hints++;
        }
        mutex_unlock(&gk_mutex);
        wake_up_interruptible(&gk_wait);
        return 0;

    case GOTHICOS_LKM_IOC_HANDOFF_STATUS:
        return gk_handoff_status((gothicos_lkm_handoff_info_t __user *)arg);

    case GOTHICOS_LKM_IOC_HANDOFF_START:
        memset(&handoff_request, 0, sizeof(handoff_request));
        if (copy_from_user(&handoff_request, (void __user *)arg,
                           sizeof(handoff_request))) {
            return -EFAULT;
        }
        if (!handoff_request.enabled || !capable(CAP_SYS_ADMIN)) {
            return -EPERM;
        }
        first_cpu = handoff_request.first_managed_cpu == 0
                        ? 1 : handoff_request.first_managed_cpu;
        cpu_count = handoff_request.managed_cpu_count == 0
                        ? (nr_cpu_ids > first_cpu ? nr_cpu_ids - first_cpu : 0)
                        : handoff_request.managed_cpu_count;
        if (cpu_count == 0) {
            return -EINVAL;
        }
        return gk_start_handoff(1, first_cpu, cpu_count);

    case GOTHICOS_LKM_IOC_HANDOFF_STOP:
        if (!capable(CAP_SYS_ADMIN)) {
            return -EPERM;
        }
        gk_stop_handoff();
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations gk_fops = {
    .owner = THIS_MODULE,
    .open = gk_open,
    .release = gk_release,
    .unlocked_ioctl = gk_ioctl,
};

static int __init gk_init(void)
{
    int result;
    unsigned int first_cpu;
    unsigned int cpu_count;

    BUILD_BUG_ON(sizeof(gothicos_lkm_request_t) != 24);
    BUILD_BUG_ON(sizeof(gothicos_lkm_info_t) != 32);
    BUILD_BUG_ON(sizeof(gothicos_lkm_handoff_request_t) != 16);
    BUILD_BUG_ON(sizeof(gothicos_lkm_handoff_info_t) != 512);

    result = alloc_chrdev_region(&gk_devno, 0, 1, GK_DEVICE_NAME);
    if (result < 0) {
        pr_err("nullkern_lkm: unable to allocate device number\n");
        return result;
    }

    cdev_init(&gk_cdev, &gk_fops);
    gk_cdev.owner = THIS_MODULE;
    result = cdev_add(&gk_cdev, gk_devno, 1);
    if (result < 0) {
        pr_err("nullkern_lkm: unable to add character device\n");
        goto unregister_region;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    gk_class = class_create(GK_CLASS_NAME);
#else
    gk_class = class_create(THIS_MODULE, GK_CLASS_NAME);
#endif
    if (IS_ERR(gk_class)) {
        result = PTR_ERR(gk_class);
        gk_class = NULL;
        pr_err("nullkern_lkm: unable to create device class\n");
        goto remove_cdev;
    }

    gk_device = device_create(gk_class, NULL, gk_devno, NULL, GK_DEVICE_NAME);
    if (IS_ERR(gk_device)) {
        result = PTR_ERR(gk_device);
        gk_device = NULL;
        pr_err("nullkern_lkm: unable to create /dev/%s\n", GK_DEVICE_NAME);
        goto destroy_class;
    }

    pr_info("nullkern_lkm: loaded /dev/%s (major %d)\n", GK_DEVICE_NAME, MAJOR(gk_devno));

    if (handoff) {
        first_cpu = first_managed_cpu == 0 ? 1 : first_managed_cpu;
        cpu_count = managed_cpus == 0
                        ? (nr_cpu_ids > first_cpu ? nr_cpu_ids - first_cpu : 0)
                        : managed_cpus;
        if (cpu_count == 0) {
            pr_warn("nullkern_lkm: no managed CPUs available; handoff not started\n");
        } else {
            (void)gk_start_handoff(1, first_cpu, cpu_count);
        }
    }
    return 0;

destroy_class:
    class_destroy(gk_class);
    gk_class = NULL;
remove_cdev:
    cdev_del(&gk_cdev);
unregister_region:
    unregister_chrdev_region(gk_devno, 1);
    return result;
}

static void __exit gk_exit(void)
{
    gk_stop_handoff();
    if (gk_device != NULL) {
        device_destroy(gk_class, gk_devno);
        gk_device = NULL;
    }
    if (gk_class != NULL) {
        class_destroy(gk_class);
        gk_class = NULL;
    }
    cdev_del(&gk_cdev);
    unregister_chrdev_region(gk_devno, 1);
    pr_info("nullkern_lkm: unloaded\n");
}

module_init(gk_init);
module_exit(gk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("gothicOS");
MODULE_DESCRIPTION("NullKernRT transport and confined opt-in microkernel handoff");
