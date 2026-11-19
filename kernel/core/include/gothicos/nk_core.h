/* NullKernRT native core -- public C contract.
 *
 * This is the language-neutral core used by the C, Rust, C++ and Linux ports. It is
 * deliberately freestanding: no libc, no allocation, no threads. All state lives in a
 * fixed-size kernel structure supplied by the host, so the same object file can be linked
 * into a MicroPython firmware image, a userspace shared library, or a Linux module.
 *
 * Opcodes and value layouts come from the stable SDK ABI in <gothicos/kernel_abi.h>.
 */
#ifndef GOTHICOS_NK_CORE_H
#define GOTHICOS_NK_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "gothicos/kernel_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NK_MAX_TASKS 16
#define NK_MAILBOX_DEPTH 8
#define NK_MAX_PORTS 16
#define NK_PORT_DEPTH 8
#define NK_MAX_POOLS 8
#define NK_POOL_BLOCK_MAX 64
#define NK_POOL_BLOCKS_MAX 16
#define NK_MAX_IRQS 16
#define NK_MAX_DRIVERS 16
#define NK_MAX_ABILITIES 16
#define NK_MAX_NODES 8
#define NK_MAX_ROUTES 16
#define NK_MAX_BOOT_STAGES 16
#define NK_MAX_SUBSCRIPTIONS 8
#define NK_MAX_SOFTBUS_MESSAGES 16
#define NK_NAME_MAX 32
#define NK_STRING_MAX 128

#define NK_BOOT_PENDING 0
#define NK_BOOT_COMPLETE 1
#define NK_BOOT_FAILED 2

typedef int (*nk_step_fn)(void *kernel, int task_id, void *context);
typedef int (*nk_irq_fn)(void *kernel, int irq_id, int source, void *context);
typedef int (*nk_driver_io_fn)(void *kernel, const char *name, const char *command,
                               const gothicos_value_t *argument,
                               gothicos_value_t *result, void *context);
typedef int (*nk_ability_fn)(void *kernel, const char *name,
                             const gothicos_arguments_t *arguments,
                             gothicos_result_t *result, void *context);
typedef int (*nk_step_resolver_fn)(const char *name, nk_step_fn *step, void **context);

typedef struct nk_kernel nk_kernel_t;

typedef struct {
    uint32_t abi_version;
    uint32_t capabilities;
    uint32_t max_tasks;
    uint32_t mailbox_depth;
    uint32_t core_count;
    uint32_t first_managed_cpu;
    uint32_t task_count;
    uint32_t boot_status;
} nk_info_t;

/* Size of the opaque kernel structure. Hosts can allocate this many bytes anywhere. */
size_t nk_kernel_storage_size(void);

/* Initialise caller-provided storage. Returns the same pointer, or NULL on bad input. */
nk_kernel_t *nk_kernel_init(void *storage, size_t size, unsigned core_count,
                            unsigned first_managed_cpu);

/* Convenience entry point backed by a process-global static instance. */
nk_kernel_t *nk_kernel_create(void);
nk_kernel_t *nk_kernel_shared(void);
void nk_kernel_shutdown(nk_kernel_t *kernel);

/* Stable ABI entry point. Returns a GOTHICOS_STATUS_* code and fills result. */
int nk_invoke(nk_kernel_t *kernel, int opcode, const gothicos_arguments_t *arguments,
              gothicos_result_t *result);

/* Scheduler and deferred-work drivers. */
int nk_dispatch_one(nk_kernel_t *kernel);
int nk_run(nk_kernel_t *kernel, int max_steps);
int nk_service_irqs(nk_kernel_t *kernel, int limit);
int nk_boot(nk_kernel_t *kernel);
int nk_yield(nk_kernel_t *kernel);
int nk_task_spawn(nk_kernel_t *kernel, const char *name, nk_step_fn step, void *context);

/* Direct host helpers used by the HAL/KAL layers. */
void nk_kernel_set_step_resolver(nk_kernel_t *kernel, nk_step_resolver_fn resolver);
int nk_port_open(nk_kernel_t *kernel, const char *name, int owner, int capacity);
int nk_port_send(nk_kernel_t *kernel, const char *name, int sender,
                 const gothicos_value_t *message);
int nk_driver_register(nk_kernel_t *kernel, const char *name, nk_driver_io_fn io,
                       void *context);
int nk_ability_register(nk_kernel_t *kernel, const char *name, const char *subsystem,
                        nk_ability_fn invoke, void *context);
int nk_current_task(nk_kernel_t *kernel);
int nk_info(nk_kernel_t *kernel, nk_info_t *info);

/* Introspection helpers shared by the C and C++ layers. */
const char *nk_status_name(int status);
const char *nk_opcode_name(int opcode);
unsigned nk_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_NK_CORE_H */
