#ifndef NULLK_KERN_H
#define NULLK_KERN_H

#include "kconfig.h"

/* On a 64-bit host the test harness passes real pointers through the syscall
 * shim; the baremetal ABI remains strictly 32-bit. */
#ifdef NK_ARCH_HOST
typedef uintptr_t nkk_arg_t;
#else
typedef uint32_t nkk_arg_t;
#endif

/*
 * TCB layout note: sp MUST remain the first field. The Cortex-M context
 * switch assembly stores/loads the process stack pointer at offset 0.
 */
typedef struct {
    uint32_t *sp;
    uint32_t *stack_base;
    uint32_t state;
    uint32_t exit_code;
    uint32_t wait_obj;
    uint32_t wait_token;
    nkk_arg_t reply_dst;
    nkk_arg_t recv_dst;
    uint32_t wake_tick;
    uint32_t slice_left;
    uint32_t parent;
    uint32_t stack_words;
    uint8_t in_use;
    uint8_t priority;
    uint8_t wait_kind;
    uint8_t reserved;
    char name[NK_TASK_NAME_MAX];
    nk_handle_t cap_handle[NK_CAP_PER_TASK];
    uint16_t cap_obj[NK_CAP_PER_TASK];
    uint8_t cap_type[NK_CAP_PER_TASK];
    uint8_t cap_rights[NK_CAP_PER_TASK];
    nk_msg_t tx;
    nk_msg_t rx;
} nkk_tcb_t;

typedef struct {
    uint16_t in_use;
    uint16_t owner;
    uint16_t capacity;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    nk_msg_t msg[NK_IPC_QUEUE_DEPTH];
} nkk_endpoint_t;

typedef struct {
    uint16_t used;
    uint16_t owner;
    uint8_t data[NK_MEM_BLOCK_SIZE];
} nkk_mem_block_t;

typedef struct {
    uint16_t bound;
    uint16_t endpoint;
    uint16_t enabled;
    uint16_t reserved;
} nkk_irq_t;

enum {
    NKK_OBJ_NONE = 0,
    NKK_OBJ_ENDPOINT = 1,
    NKK_OBJ_MEM = 2,
    NKK_OBJ_IRQ = 3
};

/* Global kernel state (statically allocated, no malloc in privileged mode). */
extern nkk_tcb_t nkk_tasks[NK_MAX_TASKS];
extern nkk_endpoint_t nkk_endpoints[NK_MAX_ENDPOINTS];
extern nkk_mem_block_t nkk_blocks[NK_MEM_BLOCK_COUNT];
extern nkk_irq_t nkk_irqs[NK_SWIRQ_COUNT];
extern nkk_tcb_t *nkk_current;
extern int nkk_current_id;
extern int nkk_next_id;
extern uint32_t nkk_tick_count;
extern uint32_t nkk_ready[NK_TASK_PRIORITIES];
extern uint32_t nkk_need_switch;
extern uint8_t nkk_runlevel;
extern char nkk_log_buffer[NK_LOG_MAX + 1];

/* Boot / core */
void nkk_core_init(void);
void nkk_boot(void);
void nkk_set_runlevel(uint32_t level);
void nkk_app_init(void);

/* Tasks */
int nkk_task_create(const char *name, uint32_t priority, uint32_t stack_words,
                    void (*entry)(void *arg), void *arg, uint32_t parent);
void nkk_task_destroy(int id);
void nkk_task_trampoline(void *arg);
int nkk_task_id(const nkk_tcb_t *t);
nkk_tcb_t *nkk_task_by_id(int id);

/* Scheduler (called by arch handlers and syscalls) */
void nkk_sched_init(void);
void nkk_sched_start(void);
void nkk_sched_ready(int id);
void nkk_sched_unready(int id);
void nkk_sched_block(uint8_t kind, uint32_t obj, uint32_t token);
void nkk_sched_wake(int id);
void nkk_sched_on_tick(void);
uint32_t *nkk_sched_pick(void);
void nkk_sched_request_switch(void);

/* Capabilities */
void nkk_cap_init(void);
nk_handle_t nkk_cap_install(int task_id, uint8_t type, uint16_t obj,
                            uint8_t rights);
nk_status_t nkk_cap_lookup(int task_id, nk_handle_t h, uint8_t type,
                           uint8_t right, uint16_t *obj_out);
nk_status_t nkk_cap_grant(int from_id, int to_id, nk_handle_t h,
                          uint32_t rights, nk_handle_t *out);
nk_status_t nkk_cap_revoke(int from_id, int to_id, nk_handle_t h);

/* IPC */
void nkk_ipc_init(void);
nk_status_t nkk_ipc_open(int owner, uint32_t cap, nk_handle_t *out);
nk_status_t nkk_ipc_close(int task_id, nk_handle_t h);
nk_status_t nkk_ipc_send(int task_id, nk_handle_t h, const nk_msg_t *msg);
nk_status_t nkk_ipc_recv(int task_id, nk_handle_t h, nk_msg_t *out);
nk_status_t nkk_ipc_call(int task_id, nk_handle_t h, const nk_msg_t *msg,
                         nk_msg_t *reply);
nk_status_t nkk_ipc_reply(int task_id, nk_handle_t h, uint32_t token,
                          const nk_msg_t *msg);
void nkk_ipc_post_irq(uint32_t source, uint32_t payload);

/* Memory */
void nkk_mem_init(void);
nk_status_t nkk_mem_alloc(int owner, nk_mem_desc_t *out);
nk_status_t nkk_mem_free(uint32_t block_id);
void nkk_mem_info(nk_mem_info_t *out);

/* IRQ */
void nkk_irq_init(void);
nk_status_t nkk_irq_bind(uint32_t source, nk_handle_t h, int task_id);
nk_status_t nkk_irq_unbind(uint32_t source, int task_id);
nk_status_t nkk_irq_raise(uint32_t source, uint32_t payload);

/* Syscalls */
uint32_t nkk_syscall(uint32_t op, nkk_arg_t a0, nkk_arg_t a1, nkk_arg_t a2,
                     nkk_arg_t a3);
void nkk_svc_handler_c(uint32_t *stack);

/* Console helpers */
void nkk_console_write(const char *text, uint32_t len);
const char *nkk_status_name(uint32_t status);

/* Boot-out hook for main() */
void nkk_app_entry_task(void *arg);

#endif /* NULLK_KERN_H */
