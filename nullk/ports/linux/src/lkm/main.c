/*
 * nkabi3 - native null_kernel ABI v3 parasite LKM.
 *
 * This is not the old MicroPython/Cython port. It does not run Python, does not
 * include a second kernel, and does not reinterpret Linux syscalls. It exposes
 * the bounded kernel half of NULLK ABI v3 to a single misc device. Services
 * remain ordinary Linux userspace processes/threads; the optional patch set
 * can later consult nk64_parasite_mode() to make scheduler/VFS/exec cooperate.
 */
#include <linux/capability.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysinfo.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "nullk_linux/abi3_ioctl.h"
#include "nullk_linux/abi64_kernel.h"
#include "nullk_linux/parasite.h"

#define NK64_USB_LUNS       2u
#define NK64_USB_BLOCK_SIZE 4096u
#define NK64_USB_BLOCKS     1u
#define NK64_LOG_MAX        512u

struct nk64_task {
    struct list_head list;
    pid_t pid;
    bool managed;
};

static LIST_HEAD(nk64_tasks);
static DEFINE_MUTEX(nk64_lock);
static u32 nk64_mode = NK64_PARASITE_OBSERVE;

static u8 nk64_usb_disk[NK64_USB_LUNS][NK64_USB_BLOCK_SIZE];
static u32 nk64_usb_luns = NK64_USB_LUNS;
static u32 nk64_usb_block_size = NK64_USB_BLOCK_SIZE;
static u32 nk64_usb_blocks = NK64_USB_BLOCKS;
static bool nk64_usb_registered;

u32 nk64_parasite_mode(void)
{
    return READ_ONCE(nk64_mode);
}
EXPORT_SYMBOL_GPL(nk64_parasite_mode);

static u64 nk64_ticks_100hz(void)
{
    return (u64)jiffies * 100ULL / (u64)HZ;
}

static u64 nk64_mem_total_bytes(void)
{
    struct sysinfo si;

    si_meminfo(&si);
    return (u64)si.totalram << PAGE_SHIFT;
}

static u32 nk64_task_count_locked(void)
{
    struct nk64_task *task;
    u32 count = 0u;

    list_for_each_entry(task, &nk64_tasks, list) {
        count++;
    }
    return count;
}

static u32 nk64_managed_count_locked(void)
{
    struct nk64_task *task;
    u32 count = 0u;

    list_for_each_entry(task, &nk64_tasks, list) {
        if (task->managed) {
            count++;
        }
    }
    return count;
}

static long nk64_dispatch_call(nk64_abi3_call_t *call)
{
    char text[NK64_LOG_MAX];
    nk64_k_info_t info;
    u32 len;

    switch (call->nr) {
    case NK64_K_SC_GETINFO:
        memset(&info, 0, sizeof(info));
        info.abi_version = NK64_K_ABI_VERSION;
        info.magic = NK64_K_ABI_MAGIC;
        info.runlevel = 8u;             /* NK_RUNLEVEL_READY */
        info.tick_hz = 100u;
        info.ticks = nk64_ticks_100hz();
        info.flags = 1u | 8u | 32u;     /* tasks | memory | preempt */
        info.hhdm_offset = 0u;          /* no Limine HHDM under Linux */
        info.mem_total = nk64_mem_total_bytes();
        mutex_lock(&nk64_lock);
        info.task_count = nk64_task_count_locked();
        mutex_unlock(&nk64_lock);
        if (copy_to_user((void __user *)(unsigned long)call->args[0], &info,
                         sizeof(info)) != 0) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        return NK64_K_STATUS_OK;

    case NK64_K_SC_TICKS:
        return nk64_ticks_100hz();

    case NK64_K_SC_YIELD:
        schedule();
        return NK64_K_STATUS_OK;

    case NK64_K_SC_SLEEP: {
        u32 ms = (u32)call->args[0] * 10u;

        if (ms > 60000u) {
            ms = 60000u;
        }
        (void)msleep_interruptible(ms);
        return NK64_K_STATUS_OK;
    }

    case NK64_K_SC_LOG:
        len = (u32)call->args[2];
        if (len > (u32)sizeof(text)) {
            len = (u32)sizeof(text);
        }
        if (len != 0u && copy_from_user(text,
                                        (const void __user *)(unsigned long)call->args[1],
                                        len) != 0) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        pr_info("[nullk] %.*s\n", (int)len, text);
        return NK64_K_STATUS_OK;

    case NK64_K_SC_USB_REGISTER: {
        u32 luns = (u32)call->args[1];
        u32 block_size = (u32)call->args[2];
        u32 blocks = (u32)call->args[3];
        size_t bytes;

        if (luns == 0u || luns > NK64_USB_LUNS || block_size == 0u ||
            block_size > NK64_USB_BLOCK_SIZE || blocks == 0u ||
            blocks > NK64_USB_BLOCKS) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        bytes = (size_t)luns * block_size * blocks;
        if (copy_from_user(nk64_usb_disk,
                           (const void __user *)(unsigned long)call->args[0],
                           bytes) != 0) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        nk64_usb_luns = luns;
        nk64_usb_block_size = block_size;
        nk64_usb_blocks = blocks;
        nk64_usb_registered = true;
        return NK64_K_STATUS_OK;
    }

    case NK64_K_SC_USB_READ:
    case NK64_K_SC_USB_WRITE: {
        u32 lun = (u32)call->args[0];
        u64 lba = call->args[1];
        u32 count = (u32)call->args[2];
        void __user *buf = (void __user *)(unsigned long)call->args[3];
        u8 *disk;
        size_t bytes;

        if (!nk64_usb_registered || lun >= nk64_usb_luns || count == 0u ||
            lba >= nk64_usb_blocks || count > nk64_usb_blocks - lba) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        bytes = (size_t)count * nk64_usb_block_size;
        disk = &nk64_usb_disk[lun][(size_t)lba * nk64_usb_block_size];
        if (call->nr == NK64_K_SC_USB_READ) {
            if (copy_to_user(buf, disk, bytes) != 0) {
                return NK64_K_STATUS_INVALID_ARGUMENT;
            }
        } else if (copy_from_user(disk, buf, bytes) != 0) {
            return NK64_K_STATUS_INVALID_ARGUMENT;
        }
        return NK64_K_STATUS_OK;
    }

    default:
        return NK64_K_STATUS_UNSUPPORTED;
    }
}

static long nk64_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct nk64_task *task = file->private_data;
    nk64_abi3_call_t call;
    nk64_parasite_status_t status;
    u32 mode;

    switch (cmd) {
    case NK64_ABI3_IOC_CALL:
        if (copy_from_user(&call, (void __user *)(unsigned long)arg,
                           sizeof(call)) != 0) {
            return -EFAULT;
        }
        if (call.magic != NK64_ABI3_MAGIC ||
            call.version != NK64_ABI3_IO_VERSION ||
            call.nr >= NK64_K_SC_COUNT) {
            return -EINVAL;
        }
        call.ret = nk64_dispatch_call(&call);
        if (copy_to_user((void __user *)(unsigned long)arg, &call,
                         sizeof(call)) != 0) {
            return -EFAULT;
        }
        return 0;

    case NK64_ABI3_IOC_SET_MODE:
        if (!capable(CAP_SYS_ADMIN)) {
            return -EPERM;
        }
        if (copy_from_user(&mode, (void __user *)(unsigned long)arg,
                           sizeof(mode)) != 0) {
            return -EFAULT;
        }
        if (mode > NK64_PARASITE_MANAGED) {
            return -EINVAL;
        }
        WRITE_ONCE(nk64_mode, mode);
        pr_info("[nullk] parasite mode=%u\n", mode);
        return 0;

    case NK64_ABI3_IOC_GET_STATUS:
        memset(&status, 0, sizeof(status));
        status.magic = NK64_ABI3_MAGIC;
        status.version = NK64_ABI3_IO_VERSION;
        status.ticks = nk64_ticks_100hz();
        status.mem_total = nk64_mem_total_bytes();
        mutex_lock(&nk64_lock);
        status.mode = nk64_mode;
        status.clients = nk64_task_count_locked();
        status.managed = nk64_managed_count_locked();
        mutex_unlock(&nk64_lock);
        if (copy_to_user((void __user *)(unsigned long)arg, &status,
                         sizeof(status)) != 0) {
            return -EFAULT;
        }
        return 0;

    case NK64_ABI3_IOC_ATTACH:
        if (task == NULL) {
            return -EINVAL;
        }
        task->managed = true;
        return 0;

    case NK64_ABI3_IOC_DETACH:
        if (task == NULL) {
            return -EINVAL;
        }
        task->managed = false;
        return 0;

    default:
        return -ENOTTY;
    }
}

static int nk64_open(struct inode *inode, struct file *file)
{
    struct nk64_task *task;

    task = kmalloc(sizeof(*task), GFP_KERNEL);
    if (task == NULL) {
        return -ENOMEM;
    }
    INIT_LIST_HEAD(&task->list);
    task->pid = task_pid_vnr(current);
    task->managed = false;

    mutex_lock(&nk64_lock);
    list_add(&task->list, &nk64_tasks);
    mutex_unlock(&nk64_lock);

    file->private_data = task;
    return 0;
}

static int nk64_release(struct inode *inode, struct file *file)
{
    struct nk64_task *task = file->private_data;

    if (task != NULL) {
        mutex_lock(&nk64_lock);
        list_del(&task->list);
        mutex_unlock(&nk64_lock);
        kfree(task);
        file->private_data = NULL;
    }
    return 0;
}

static const struct file_operations nk64_fops = {
    .owner = THIS_MODULE,
    .open = nk64_open,
    .release = nk64_release,
    .unlocked_ioctl = nk64_ioctl,
};

static struct miscdevice nk64_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "nkabi3",
    .fops = &nk64_fops,
    .mode = 0600,
};

static int __init nk64_init(void)
{
    int rc;

    rc = misc_register(&nk64_misc);
    if (rc != 0) {
        pr_err("[nullk] failed to register /dev/%s: %d\n",
               nk64_misc.name, rc);
        return rc;
    }
    pr_info("[nullk] ABI v3 parasite LKM online: /dev/%s mode=%u\n",
            nk64_misc.name, nk64_mode);
    return 0;
}

static void __exit nk64_exit(void)
{
    misc_deregister(&nk64_misc);
    pr_info("[nullk] ABI v3 parasite LKM unloaded\n");
}

module_init(nk64_init);
module_exit(nk64_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("gothicOS");
MODULE_DESCRIPTION("Native null_kernel ABI v3 parasite kernel module");
