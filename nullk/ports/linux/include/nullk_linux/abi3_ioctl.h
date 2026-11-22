/*
 * Native Linux ABI v3 transport.
 *
 * The LKM exposes one misc device, /dev/nkabi3. Existing libnk64 wrappers keep
 * their signatures; the Linux backend marshals a NULLK ABI v3 call into these
 * ioctls. Raw ABI v3 syscall numbers are never sent to the host kernel, so
 * they cannot collide with Linux syscalls 0..28.
 */
#ifndef NULLK_LINUX_ABI3_IOCTL_H
#define NULLK_LINUX_ABI3_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u32 nk64_abi3_u32;
typedef __u64 nk64_abi3_u64;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t nk64_abi3_u32;
typedef uint64_t nk64_abi3_u64;
#endif

#define NK64_ABI3_DEVICE_NAME "/dev/nkabi3"
#define NK64_ABI3_IO_VERSION 1u
#define NK64_ABI3_MAGIC      0x4E4B334Cu /* "NK3L" in numeric byte order */

typedef struct {
    nk64_abi3_u32 magic;
    nk64_abi3_u32 version;
    nk64_abi3_u32 nr;
    nk64_abi3_u32 flags;
    nk64_abi3_u64 args[6];
    nk64_abi3_u64 ret;
} nk64_abi3_call_t;

enum {
    NK64_PARASITE_OBSERVE = 0u,
    NK64_PARASITE_COOPERATIVE = 1u,
    NK64_PARASITE_MANAGED = 2u
};

typedef struct {
    nk64_abi3_u32 magic;
    nk64_abi3_u32 version;
    nk64_abi3_u32 mode;
    nk64_abi3_u32 clients;
    nk64_abi3_u32 managed;
    nk64_abi3_u32 reserved;
    nk64_abi3_u64 ticks;
    nk64_abi3_u64 mem_total;
} nk64_parasite_status_t;

#define NK64_ABI3_IOC_CALL      _IOWR('N', 0x20, nk64_abi3_call_t)
#define NK64_ABI3_IOC_SET_MODE  _IOW('N', 0x21, nk64_abi3_u32)
#define NK64_ABI3_IOC_GET_STATUS _IOR('N', 0x22, nk64_parasite_status_t)
#define NK64_ABI3_IOC_ATTACH    _IO('N', 0x23)
#define NK64_ABI3_IOC_DETACH    _IO('N', 0x24)

#endif /* NULLK_LINUX_ABI3_IOCTL_H */
