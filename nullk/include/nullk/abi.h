/*
 * NULLK ABI v2 - baremicrokernel syscall contract.
 *
 * This header is freestanding and deliberately non-POSIX. It contains only
 * fixed-width types, fixed-size structs and integer constants.
 */
#ifndef NULLK_ABI_H
#define NULLK_ABI_H

#include <stdint.h>
#include <stddef.h>

#define NULLK_ABI_VERSION       0x00020000u
#define NULLK_ABI_MAGIC         0x4E4B5632u   /* "NKV2" */

#define NK_TASK_NAME_MAX        16u
#define NK_IPC_PAYLOAD_MAX      48u
#define NK_MSG_SIZE             64u
#define NK_LOG_MAX              128u
#define NK_MEM_BLOCK_SIZE       64u
#define NK_MAX_TASKS            16u
#define NK_MAX_ENDPOINTS        16u
#define NK_IPC_QUEUE_DEPTH      8u
#define NK_MEM_BLOCK_COUNT      64u
#define NK_CAP_PER_TASK         16u
#define NK_SWIRQ_COUNT          8u
#define NK_TASK_PRIORITIES      8u
#define NK_TASK_QUANTUM_TICKS   5u

typedef uint16_t nk_handle_t;
typedef uint32_t nk_status_t;
typedef uint32_t nk_tick_t;

enum {
    NK_STATUS_OK = 0,
    NK_STATUS_INVALID_ARGUMENT = 1,
    NK_STATUS_NOT_FOUND = 2,
    NK_STATUS_WOULD_BLOCK = 3,
    NK_STATUS_QUEUE_FULL = 4,
    NK_STATUS_UNSUPPORTED = 5,
    NK_STATUS_INTERNAL_ERROR = 6,
    NK_STATUS_PERMISSION_DENIED = 7,
    NK_STATUS_ALREADY_EXISTS = 8,
    NK_STATUS_OUT_OF_MEMORY = 9,
    NK_STATUS_BUSY = 10,
    NK_STATUS_DEAD = 11
};

enum {
    NK_FEATURE_TASKS   = 1u << 0,
    NK_FEATURE_IPC     = 1u << 1,
    NK_FEATURE_CAPS    = 1u << 2,
    NK_FEATURE_MEMORY  = 1u << 3,
    NK_FEATURE_IRQ     = 1u << 4,
    NK_FEATURE_PREEMPT = 1u << 5
};

typedef enum {
    NK_TASK_READY = 1,
    NK_TASK_RUNNING = 2,
    NK_TASK_BLOCKED = 3,
    NK_TASK_SUSPENDED = 4,
    NK_TASK_DEAD = 5
} nk_task_state_t;

enum {
    NK_RIGHT_READ   = 1u,
    NK_RIGHT_WRITE  = 2u,
    NK_RIGHT_GRANT  = 4u,
    NK_RIGHT_MANAGE = 8u,
    NK_RIGHT_ALL    = 15u
};

enum {
    NK_MSG_DATA   = 1u,
    NK_MSG_PING   = 2u,
    NK_MSG_PONG   = 3u,
    NK_MSG_IRQ    = 4u,
    NK_MSG_EVENT  = 5u
};

enum {
    NK_RUNLEVEL_RESET   = 0,
    NK_RUNLEVEL_PLATFORM = 1,
    NK_RUNLEVEL_CORE    = 2,
    NK_RUNLEVEL_MEMORY  = 3,
    NK_RUNLEVEL_IPC     = 4,
    NK_RUNLEVEL_IRQ     = 5,
    NK_RUNLEVEL_DRIVERS = 6,
    NK_RUNLEVEL_TASKS   = 7,
    NK_RUNLEVEL_READY   = 8
};

enum {
    NK_SC_GET_INFO = 0,
    NK_SC_TASK_SELF = 1,
    NK_SC_TASK_CREATE = 2,
    NK_SC_TASK_EXIT = 3,
    NK_SC_TASK_STATE = 4,
    NK_SC_YIELD = 5,
    NK_SC_SLEEP = 6,
    NK_SC_TICKS = 7,
    NK_SC_IPC_OPEN = 8,
    NK_SC_IPC_CLOSE = 9,
    NK_SC_IPC_SEND = 10,
    NK_SC_IPC_RECV = 11,
    NK_SC_IPC_CALL = 12,
    NK_SC_IPC_REPLY = 13,
    NK_SC_CAP_GRANT = 14,
    NK_SC_CAP_REVOKE = 15,
    NK_SC_MEM_ALLOC = 16,
    NK_SC_MEM_FREE = 17,
    NK_SC_MEM_INFO = 18,
    NK_SC_IRQ_BIND = 19,
    NK_SC_IRQ_UNBIND = 20,
    NK_SC_IRQ_RAISE = 21,
    NK_SC_LOG = 22,
    NK_SC_DEBUG = 23,
    NK_SC_COUNT = 24
};

typedef struct {
    uint32_t tag;
    uint32_t token;
    uint32_t length;
    uint32_t reserved;
    uint8_t data[NK_IPC_PAYLOAD_MAX];
} nk_msg_t;

typedef struct {
    uint32_t abi_version;
    uint32_t magic;
    uint32_t features;
    uint32_t runlevel;
    uint32_t tick_hz;
    uint32_t ticks;
    uint32_t max_tasks;
    uint32_t task_count;
    uint32_t max_endpoints;
    uint32_t endpoint_count;
    uint32_t msg_payload_max;
    uint32_t reserved[5];
} nk_info_t;

typedef struct {
    uint32_t out_id;
    const char *name;
    uint32_t priority;
    uint32_t stack_words;
    void (*entry)(void *arg);
    void *arg;
} nk_task_create_info_t;

typedef struct {
    uint32_t id;
    uint32_t state;
    uint32_t priority;
    uint32_t parent;
    uint32_t slice_left;
    uint32_t reserved;
    char name[NK_TASK_NAME_MAX];
} nk_task_info_t;

typedef struct {
    uint32_t block_id;
    void *ptr;
} nk_mem_desc_t;

typedef struct {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t used;
    uint32_t reserved;
} nk_mem_info_t;

_Static_assert(sizeof(nk_msg_t) == NK_MSG_SIZE, "nk_msg_t must be 64 bytes");
_Static_assert(sizeof(nk_handle_t) == 2, "nk_handle_t must be 16-bit");
_Static_assert(sizeof(nk_info_t) == 64, "nk_info_t layout changed");

#endif /* NULLK_ABI_H */
