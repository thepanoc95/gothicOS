/* Thin Linux character device for the Cython NULLKern userspace port.
 *
 * This module intentionally contains no CPython or Cython runtime. It exposes a small
 * set of kernel-side counters and wakeups that the Cython host can drive through ioctl.
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>

#include "include/gothicos_linux_port.h"

#define GK_DEVICE_NAME "nullkern"
#define GK_CLASS_NAME "nullkern"

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

static long gk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    gothicos_lkm_request_t request;
    gothicos_lkm_info_t info;

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

    BUILD_BUG_ON(sizeof(gothicos_lkm_request_t) != 24);
    BUILD_BUG_ON(sizeof(gothicos_lkm_info_t) != 32);

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
MODULE_DESCRIPTION("Thin Linux kernel transport for the Cython NULLKern port");
