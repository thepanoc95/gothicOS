#include "kern.h"
#include "arch_if.h"

nkk_tcb_t nkk_tasks[NK_MAX_TASKS];
nkk_endpoint_t nkk_endpoints[NK_MAX_ENDPOINTS];
nkk_mem_block_t nkk_blocks[NK_MEM_BLOCK_COUNT];
nkk_irq_t nkk_irqs[NK_SWIRQ_COUNT];
nkk_tcb_t *nkk_current;
int nkk_current_id = -1;
int nkk_next_id = -1;
uint32_t nkk_tick_count;
uint32_t nkk_ready[NK_TASK_PRIORITIES];
uint32_t nkk_need_switch;
uint8_t nkk_runlevel;
char nkk_log_buffer[NK_LOG_MAX + 1];

void nkk_core_init(void) {
    uint32_t i;

    for (i = 0; i < NK_MAX_TASKS; i++) {
        nkk_tasks[i].in_use = 0;
    }
    for (i = 0; i < NK_MAX_ENDPOINTS; i++) {
        nkk_endpoints[i].in_use = 0;
    }
    for (i = 0; i < NK_MEM_BLOCK_COUNT; i++) {
        nkk_blocks[i].used = 0;
    }
    for (i = 0; i < NK_SWIRQ_COUNT; i++) {
        nkk_irqs[i].bound = 0;
        nkk_irqs[i].enabled = 0;
    }
    for (i = 0; i < NK_TASK_PRIORITIES; i++) {
        nkk_ready[i] = 0;
    }

    nkk_tick_count = 0;
    nkk_current = 0;
    nkk_current_id = -1;
    nkk_next_id = -1;
    nkk_need_switch = 0;
    nkk_runlevel = NK_RUNLEVEL_RESET;

    nkk_cap_init();
    nkk_mem_init();
    nkk_ipc_init();
    nkk_irq_init();
    nkk_sched_init();
}

void nkk_boot(void) {
    nkk_core_init();

    /* The console driver needs its clocks, resets and pins ready before the
     * first log line; QEMU tolerated the old order, RP2040 does not. */
    nkk_arch_early_init();
    nkk_console_write("null_kernel baremetal / NULLK ABI v2\n", 37);

    nkk_set_runlevel(NK_RUNLEVEL_PLATFORM);
    nkk_set_runlevel(NK_RUNLEVEL_CORE);
    nkk_set_runlevel(NK_RUNLEVEL_MEMORY);
    nkk_set_runlevel(NK_RUNLEVEL_IPC);
    nkk_set_runlevel(NK_RUNLEVEL_IRQ);
    nkk_arch_tick_init(NKK_TICK_HZ);
    nkk_set_runlevel(NK_RUNLEVEL_DRIVERS);
    nkk_set_runlevel(NK_RUNLEVEL_TASKS);
    nkk_app_init();
    nkk_set_runlevel(NK_RUNLEVEL_READY);

    nkk_arch_start_first();
}
