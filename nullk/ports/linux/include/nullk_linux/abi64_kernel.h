/*
 * Kernel-side mirror of nullk/include/nullk/abi64.h.
 *
 * Linux headers do not provide <stdint.h>, so the LKM cannot include the
 * canonical ABI header directly. These structs are byte-for-byte mirrors and
 * tests/test_abi_layout.c asserts their sizes and offsets against the canonical
 * header. Do not change this file without changing that test.
 */
#ifndef NULLK_LINUX_ABI64_KERNEL_H
#define NULLK_LINUX_ABI64_KERNEL_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef __u16 nk64_k_u16;
typedef __u32 nk64_k_u32;
typedef __u64 nk64_k_u64;
typedef __s32 nk64_k_s32;
typedef __u8 nk64_k_u8;
#else
#include <stdint.h>
typedef uint16_t nk64_k_u16;
typedef uint32_t nk64_k_u32;
typedef uint64_t nk64_k_u64;
typedef int32_t nk64_k_s32;
typedef uint8_t nk64_k_u8;
#endif

#define NK64_K_ABI_VERSION 0x00030000u
#define NK64_K_ABI_MAGIC   0x4E4B5633u

#define NK64_K_SC_GETINFO   9u
#define NK64_K_SC_SLEEP     7u
#define NK64_K_SC_YIELD     6u
#define NK64_K_SC_TICKS     8u
#define NK64_K_SC_LOG       18u
#define NK64_K_SC_USB_READ  22u
#define NK64_K_SC_USB_WRITE 23u
#define NK64_K_SC_USB_REGISTER 24u
#define NK64_K_SC_COUNT     29u

enum {
    NK64_K_STATUS_OK = 0u,
    NK64_K_STATUS_INVALID_ARGUMENT = 1u,
    NK64_K_STATUS_NOT_FOUND = 2u,
    NK64_K_STATUS_UNSUPPORTED = 5u,
    NK64_K_STATUS_INTERNAL_ERROR = 6u,
    NK64_K_STATUS_PERMISSION_DENIED = 7u
};

typedef struct {
    nk64_k_u32 abi_version;
    nk64_k_u32 magic;
    nk64_k_u32 runlevel;
    nk64_k_u32 tick_hz;
    nk64_k_u64 ticks;
    nk64_k_u32 task_count;
    nk64_k_u32 flags;
    nk64_k_u64 hhdm_offset;
    nk64_k_u64 mem_total;
} nk64_k_info_t;

typedef struct {
    nk64_k_u64 phys;
    nk64_k_u32 width;
    nk64_k_u32 height;
    nk64_k_u32 pitch;
    nk64_k_u16 bpp;
    nk64_k_u8 red_size;
    nk64_k_u8 red_shift;
    nk64_k_u8 green_size;
    nk64_k_u8 green_shift;
    nk64_k_u8 blue_size;
    nk64_k_u8 blue_shift;
    nk64_k_u8 present;
    nk64_k_u8 reserved[3];
} nk64_k_fbinfo_t;

typedef struct {
    nk64_k_u32 type;
    nk64_k_u32 code;
    nk64_k_u32 modifiers;
    nk64_k_s32 x;
    nk64_k_s32 y;
    nk64_k_s32 dx;
    nk64_k_s32 dy;
    nk64_k_u32 buttons;
    nk64_k_u32 reserved;
} nk64_k_input_event_t;

#endif /* NULLK_LINUX_ABI64_KERNEL_H */
