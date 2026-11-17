/* Shared userspace/kernel ABI for the thin nullkern Linux char device.
 *
 * The Cython port in linux_port.pyx includes this header. The kernel module
 * copies the same 24-byte request and 32-byte response layouts.
 */
#ifndef GOTHICOS_LINUX_PORT_H
#define GOTHICOS_LINUX_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GOTHICOS_LKM_IOC_MAGIC 0x4B
#define GOTHICOS_LKM_IOC_GET_INFO 0x80204B01U
#define GOTHICOS_LKM_IOC_RAISE_IRQ 0x40184B02U
#define GOTHICOS_LKM_IOC_PORT_SEND 0x40184B03U
#define GOTHICOS_LKM_IOC_SCHED_HINT 0x40184B04U

#define GOTHICOS_LKM_OP_RAISE_IRQ 1
#define GOTHICOS_LKM_OP_PORT_SEND 2
#define GOTHICOS_LKM_OP_SCHED_HINT 3

typedef struct gothicos_lkm_request {
    int32_t opcode;
    int32_t arg0;
    int32_t arg1;
    int32_t reserved;
    uint64_t payload;
} gothicos_lkm_request_t;

typedef struct gothicos_lkm_info {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t abi_patch;
    uint32_t capabilities;
    uint32_t irq_count;
    uint32_t message_count;
    uint32_t schedule_hints;
    uint32_t active_nodes;
} gothicos_lkm_info_t;

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_LINUX_PORT_H */
