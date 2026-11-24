/*
 * Tiny AMD64 kernel core - fixed-size task scheduler.
 *
 * All task metadata, kernel stacks and the user-stack staging window are
 * static: there is no allocator in ring 0. Task 0 is the idle/kernel task.
 * ELF images are mapped by nk64_load_user_elf(); this file adds the heap,
 * sandbox and user stack, then publishes a ready TCB to the arch trampoline.
 *
 * Userspace entry convention (shared with the userspace agent): applications
 * export `void _start(void)`. The initial stack contains a single NULL qword
 * at rsp (argv terminator placeholder); no argc/argv marshalling is required.
 *
 * ARCH EXPECTATION: context.S must export
 *   void nk64_context_switch(uint64_t *old_rsp, uint64_t new_rsp);
 *   void nk64_tss_set_rsp0(uint64_t rsp0);
 * These are called from the timer tick / yield paths only.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/abi.h"
#include "nullk/elf64.h"
#include "nullk/printk.h"

#define NK64_QUANTUM            ((uint32_t)NK_TASK_QUANTUM_TICKS)
#define NK64_KSTACK_BYTES       (NK64_STACK_PAGES * 4096u)
#define NK64_USTACK_BYTES       (NK64_USER_STACK_PAGES * 4096u)
#define NK64_HEAP_BYTES         (16u * 4096u)       /* 64 KiB per task */
#define NK64_SANDBOX_BYTES      (16u * 4096u)       /* 64 KiB backed window */
#define NK64_TASK_TEMP_VA       0xffffffff41000000ULL

/* Implemented by arch/amd64/context.S (see header comment). */
extern void nk64_context_switch(uint64_t *old_rsp, uint64_t new_rsp);
extern void nk64_tss_set_rsp0(uint64_t rsp0);
extern uint64_t nk64_syscall_user_rsp;
extern uint64_t nk64_current_kstack_top;

/* Set by the PIT tick, consumed on the next syscall exit. Switching stacks
 * inside an IRQ handler would corrupt the interrupt frame. */
volatile uint32_t nk64_need_switch;

/* Implemented in elfload.c. */
nk_status_t nk64_load_user_elf(const uint8_t *elf, uint32_t size,
                               uint64_t cr3);

static nk64_tcb_t g_tasks[NK64_MAX_TASKS];
static uint8_t g_kstacks[NK64_MAX_TASKS][NK64_KSTACK_BYTES];
static uint8_t g_used[NK64_MAX_TASKS];
static uint64_t g_user_rsp[NK64_MAX_TASKS];
static int g_current;
static int g_in_switch;
static int g_launch_id;
static uint64_t g_kernel_cr3;

/* ------------------------------------------------------------------ */
/* Fresh-task launch trampolines.                                      */
/*                                                                     */
/* nk64_context_switch() pops r15,r14,r13,r12,rbx,rbp and then rets.  A  */
/* never-before-run task therefore needs a synthetic frame on its kernel*/
/* stack whose return address is one of these trampolines. g_launch_id  */
/* identifies the task being entered.                                  */
/* ------------------------------------------------------------------ */

static uint64_t nk64_stack_top(int slot);
static void nk64_switch_epilogue(void);
static void nk64_launch_user(void) __attribute__((noreturn));
static void nk64_launch_kernel(void) __attribute__((noreturn));

/*
 * Finish a context switch on the incoming task's stack. Resumed tasks reach
 * this from nk64_switch_now(); fresh tasks must call it from the launch
 * trampoline because they never return through the outgoing task's frame.
 */
static void nk64_switch_epilogue(void)
{
    uint64_t cr3 = g_tasks[g_current].cr3;

    /* The assembly context switch preserves registers and kernel stacks only.
     * User tasks can own separate CR3s (exec creates one), so a resumed task
     * must reinstall its page tables before returning to ring 3. Kernel
     * mappings are shared in the upper half, making this safe on every
     * privileged stack. */
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    nk64_syscall_user_rsp = g_tasks[g_current].user_rsp;
    nk64_current_kstack_top = g_tasks[g_current].syscall_stack;
    g_in_switch = 0;
}

static void nk64_launch_user(void)
{
    int id = g_launch_id;

    nk64_switch_epilogue();
    nk64_usermode_jump(g_user_rsp[id], g_tasks[id].rip, g_tasks[id].cr3);
}

static void nk64_launch_kernel(void)
{
    int id = g_launch_id;
    void (*entry)(void) = (void (*)(void))(uintptr_t)g_tasks[id].rip;

    nk64_switch_epilogue();
    entry();
    for (;;) {
        nk64_hlt();
    }
}

static void nk64_build_switch_frame(int slot, uint64_t trampoline)
{
    uint64_t *frame = (uint64_t *)(uintptr_t)(nk64_stack_top(slot) - 64u);
    uint32_t i;

    /* r15, r14, r13, r12, rbx, rbp */
    for (i = 0u; i < 6u; i++) {
        frame[i] = 0u;
    }
    frame[6] = trampoline;             /* 'ret' target */
    g_tasks[slot].rsp = (uint64_t)(uintptr_t)frame;
}

/* ------------------------------------------------------------------ */
/* Local helpers.                                                      */
/* ------------------------------------------------------------------ */

static void nk64_zero(void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;

    while (len-- != 0u) {
        *p++ = 0u;
    }
}

static void nk64_name_copy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0u;

    if (cap == 0u) {
        return;
    }
    if (src != (const char *)0) {
        while (i + 1u < cap && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static uint64_t nk64_read_cr3_local(void)
{
    uint64_t cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static int nk64_find_slot(void)
{
    uint32_t i;

    for (i = 1u; i < NK64_MAX_TASKS; i++) {
        if (g_used[i] == 0u) {
            return (int)i;
        }
    }
    return -1;
}

static uint64_t nk64_stack_top(int slot)
{
    return (uint64_t)(uintptr_t)&g_kstacks[slot][NK64_KSTACK_BYTES];
}

/* Pick the next READY task, never the currently running one. */
static int nk64_next_ready(void)
{
    uint32_t n;

    for (n = 1u; n <= NK64_MAX_TASKS; n++) {
        uint32_t idx = (uint32_t)(((uint32_t)g_current + n) %
                                  NK64_MAX_TASKS);

        if (g_used[idx] != 0u && g_tasks[idx].state == NK_TASK_READY) {
            return (int)idx;
        }
    }
    return -1;
}

/*
 * Perform the actual round-robin switch. The reentrancy guard prevents a
 * nested timer interrupt from clobbering a switch already in progress.
 */
static void nk64_switch_now(void)
{
    int next;
    int prev;

    if (g_in_switch != 0) {
        return;
    }
    next = nk64_next_ready();
    if (next < 0) {
        return;
    }

    g_in_switch = 1;
    prev = g_current;
    g_tasks[prev].state = NK_TASK_READY;
    g_tasks[next].state = NK_TASK_RUNNING;
    g_tasks[next].slice_left = NK64_QUANTUM;
    g_current = next;

    /* User tasks need a privileged stack for interrupts and syscalls. */
    nk64_tss_set_rsp0(g_tasks[next].syscall_stack);

    /* If this task has never run, its synthetic frame lands in a launch
     * trampoline which performs the actual ring-3 / kernel entry. */
    g_launch_id = next;

    /* The SYSCALL fast path uses one entry stack per task. Preserve the
     * interrupted user RSP and install the incoming task's stack before the
     * switch; restore our own globals when this task is scheduled again. */
    g_tasks[prev].user_rsp = nk64_syscall_user_rsp;
    nk64_current_kstack_top = g_tasks[next].syscall_stack;
    nk64_context_switch(&g_tasks[prev].rsp, g_tasks[next].rsp);
    nk64_switch_epilogue();
}

/* ------------------------------------------------------------------ */
/* Public task API.                                                    */
/* ------------------------------------------------------------------ */

void nk64_task_init(void)
{
    nk64_zero(g_tasks, (uint32_t)sizeof(g_tasks));
    nk64_zero(g_kstacks, (uint32_t)sizeof(g_kstacks));
    nk64_zero(g_used, (uint32_t)sizeof(g_used));
    g_current = 0;
    g_in_switch = 0;
    g_kernel_cr3 = nk64_read_cr3_local();

    /* Reserve slot 0 for the idle/kernel context. */
    g_used[0] = 1u;
    g_tasks[0].id = 0u;
    g_tasks[0].state = NK_TASK_RUNNING;
    g_tasks[0].slice_left = NK64_QUANTUM;
    g_tasks[0].cr3 = g_kernel_cr3;
    g_tasks[0].rsp = nk64_stack_top(0);
    g_tasks[0].rflags = 0x202u;
    g_tasks[0].syscall_stack = nk64_stack_top(0);
    nk64_current_kstack_top = nk64_stack_top(0);
    nk64_syscall_user_rsp = 0u;
    nk64_need_switch = 0u;
    nk64_name_copy(g_tasks[0].name, "idle", NK64_NAME_MAX);
}

int nk64_task_create_elf(const uint8_t *elf, uint32_t size, const char *name)
{
    nk_elf64_info_t info;
    uint64_t cr3;
    uint64_t span;
    uint64_t pages;
    uint64_t stack_phys[NK64_USER_STACK_PAGES];
    uint64_t stack_base;
    uint64_t *sp;
    uint64_t i;
    int slot;

    if (elf == (const uint8_t *)0 || size == 0u) {
        return -1;
    }
    if (nk_elf64_probe(elf, size, &info) != 0 ||
        info.machine != NK_ELF64_EM_X86_64 ||
        info.type != NK_ELF64_ET_EXEC ||
        info.vaddr_lo < NK64_USER_BASE ||
        info.vaddr_hi > NK64_USER_STACK_TOP) {
        return -1;
    }
    span = info.vaddr_hi - info.vaddr_lo;
    if (span == 0u || span > 0x00800000ULL) {
        return -1;
    }
    pages = (span + 4095u) / 4096u;
    if (pages == 0u || pages > 2048u) {
        return -1;
    }

    slot = nk64_find_slot();
    if (slot < 0) {
        return -1;
    }
    cr3 = nk64_vmm_create_space();
    if (cr3 == 0u) {
        pr_warn("task: create_space failed\n");
        return -1;
    }
    if (nk64_load_user_elf(elf, size, cr3) != NK_STATUS_OK) {
        pr_warn("task: load_user_elf failed\n");
        return -1;
    }

    /* Heap and sandbox use the VMM helper (it chooses user flags itself). */
    if (nk64_vmm_alloc_user_pages(cr3, NK64_USER_HEAP, NK64_HEAP_BYTES) == 0u ||
        nk64_vmm_alloc_user_pages(cr3, NK64_SANDBOX_BASE,
                                  NK64_SANDBOX_BYTES) == 0u) {
        pr_warn("task: heap/sandbox alloc failed\n");
        return -1;
    }

    /* Stage the user stack in a kernel window, then alias it into user space
     * with NX set. We must know the physical frames to do that, so we do not
     * use nk64_vmm_alloc_user_pages() for this particular region. */
    stack_base = NK64_USER_STACK_TOP - NK64_USTACK_BYTES;
    for (i = 0u; i < NK64_USER_STACK_PAGES; i++) {
        uint64_t phys = nk64_pmm_alloc_frame();

        if (phys == 0u) {
            uint64_t j;

            for (j = 0u; j < i; j++) {
                nk64_pmm_free_frame(stack_phys[j]);
            }
            pr_warn("task: stack alloc failed\n");
            return -1;
        }
        stack_phys[i] = phys;
        nk64_vmm_map(g_kernel_cr3, NK64_TASK_TEMP_VA + i * 4096u, phys,
                     4096u, NK64_PAGE_KERNEL);
    }

    sp = (uint64_t *)(uintptr_t)(NK64_TASK_TEMP_VA + NK64_USTACK_BYTES);
    sp[-1] = 0u;   /* NULL argv terminator for `void _start(void)` */

    for (i = 0u; i < NK64_USER_STACK_PAGES; i++) {
        nk64_vmm_map(cr3, stack_base + i * 4096u, stack_phys[i], 4096u,
                     NK64_PAGE_PRESENT | NK64_PAGE_RW | NK64_PAGE_USER |
                     NK64_PAGE_NX);
    }

    g_used[slot] = 1u;
    g_tasks[slot].id = (uint32_t)slot;
    g_tasks[slot].cr3 = cr3;
    g_tasks[slot].rip = info.entry;
    /* The user stack top holds the NULL qword written above. tcb.rsp is the
     * kernel switch frame used by the scheduler; the user RSP is separate. */
    g_user_rsp[slot] = NK64_USER_STACK_TOP - 8u;
    nk64_build_switch_frame(slot, (uint64_t)(uintptr_t)nk64_launch_user);
    g_tasks[slot].rflags = 0x202u;
    g_tasks[slot].syscall_stack = nk64_stack_top(slot);
    g_tasks[slot].state = NK_TASK_READY;
    g_tasks[slot].slice_left = NK64_QUANTUM;
    nk64_name_copy(g_tasks[slot].name, name, NK64_NAME_MAX);

    return slot;
}

int nk64_task_create_kernel(void (*entry)(void), const char *name)
{
    int slot;

    if (entry == (void (*)(void))0) {
        return -1;
    }
    slot = nk64_find_slot();
    if (slot < 0) {
        return -1;
    }

    g_used[slot] = 1u;
    g_tasks[slot].id = (uint32_t)slot;
    g_tasks[slot].cr3 = g_kernel_cr3;
    g_tasks[slot].rip = (uint64_t)(uintptr_t)entry;
    nk64_build_switch_frame(slot, (uint64_t)(uintptr_t)nk64_launch_kernel);
    g_tasks[slot].rflags = 0x202u;
    g_tasks[slot].syscall_stack = nk64_stack_top(slot);
    g_tasks[slot].state = NK_TASK_READY;
    g_tasks[slot].slice_left = NK64_QUANTUM;
    nk64_name_copy(g_tasks[slot].name, name, NK64_NAME_MAX);

    /* Kernel tasks share CR3; they are registered for the scheduler but must
     * be entered with a kernel-mode trampoline. The fallback shell runs
     * inline in nk64_main(), so no such trampoline is needed at boot. */
    return slot;
}

/*
 * Create a second user task inside an existing user address space (used by
 * ABI v3 TASK_CREATE from init64). Each task gets its own stack at a distinct
 * virtual base so tasks sharing one CR3 do not overwrite each other.
 */
int nk64_task_create_user(void (*entry)(void), const char *name)
{
    uint64_t stack_phys[NK64_USER_STACK_PAGES];
    uint64_t cr3;
    uint64_t stack_base;
    uint64_t *sp;
    uint64_t i;
    int slot;

    if (entry == (void (*)(void))0 || g_current <= 0) {
        return -1;
    }
    cr3 = g_tasks[g_current].cr3;
    if (cr3 == 0u || cr3 == g_kernel_cr3) {
        return -1;
    }
    slot = nk64_find_slot();
    if (slot < 0) {
        return -1;
    }

    stack_base = NK64_USER_STACK_TOP - (uint64_t)slot * 0x20000u -
                 NK64_USTACK_BYTES;
    if (stack_base <= NK64_SANDBOX_BASE + NK64_SANDBOX_SIZE) {
        return -1;
    }

    for (i = 0u; i < NK64_USER_STACK_PAGES; i++) {
        uint64_t phys = nk64_pmm_alloc_frame();

        if (phys == 0u) {
            uint64_t j;

            for (j = 0u; j < i; j++) {
                nk64_pmm_free_frame(stack_phys[j]);
            }
            pr_warn("task: stack alloc failed\n");
            return -1;
        }
        stack_phys[i] = phys;
        nk64_vmm_map(g_kernel_cr3, NK64_TASK_TEMP_VA + i * 4096u, phys,
                     4096u, NK64_PAGE_KERNEL);
    }

    sp = (uint64_t *)(uintptr_t)(NK64_TASK_TEMP_VA + NK64_USTACK_BYTES);
    sp[-1] = 0u;

    for (i = 0u; i < NK64_USER_STACK_PAGES; i++) {
        nk64_vmm_map(cr3, stack_base + i * 4096u, stack_phys[i], 4096u,
                     NK64_PAGE_PRESENT | NK64_PAGE_RW | NK64_PAGE_USER |
                     NK64_PAGE_NX);
    }

    g_used[slot] = 1u;
    g_tasks[slot].id = (uint32_t)slot;
    g_tasks[slot].cr3 = cr3;
    g_tasks[slot].rip = (uint64_t)(uintptr_t)entry;
    g_user_rsp[slot] = stack_base + NK64_USTACK_BYTES - 8u;
    nk64_build_switch_frame(slot, (uint64_t)(uintptr_t)nk64_launch_user);
    g_tasks[slot].rflags = 0x202u;
    g_tasks[slot].syscall_stack = nk64_stack_top(slot);
    g_tasks[slot].state = NK_TASK_READY;
    g_tasks[slot].slice_left = NK64_QUANTUM;
    nk64_name_copy(g_tasks[slot].name, name, NK64_NAME_MAX);
    return slot;
}

void nk64_task_start_first(void)
{
    int first = -1;
    uint32_t i;

    for (i = 1u; i < NK64_MAX_TASKS; i++) {
        if (g_used[i] != 0u && g_tasks[i].state == NK_TASK_READY) {
            first = (int)i;
            break;
        }
    }
    if (first < 0) {
        nk64_panic("no runnable userspace task");
    }

    g_current = first;
    g_tasks[first].state = NK_TASK_RUNNING;
    g_tasks[first].slice_left = NK64_QUANTUM;
    nk64_tss_set_rsp0(g_tasks[first].syscall_stack);
    nk64_current_kstack_top = g_tasks[first].syscall_stack;
    nk64_syscall_user_rsp = g_user_rsp[first];
    g_launch_id = first;
    nk64_usermode_jump(g_user_rsp[first], g_tasks[first].rip,
                       g_tasks[first].cr3);
}

void nk64_task_yield(void)
{
    nk64_switch_now();
}

/* Called from the syscall exit path, never from an interrupt handler. */
void nk64_yield_if_needed(void)
{
    if (nk64_need_switch == 0u) {
        return;
    }
    nk64_need_switch = 0u;
    nk64_switch_now();
}

void nk64_sched_tick(void)
{
    if (g_in_switch != 0 || g_current < 0) {
        return;
    }
    if (g_tasks[g_current].slice_left != 0u) {
        g_tasks[g_current].slice_left--;
    }
    if (g_tasks[g_current].slice_left == 0u) {
        g_tasks[g_current].slice_left = NK64_QUANTUM;
        /* Defer: the PIT stub still owns this kernel stack. */
        nk64_need_switch = 1u;
    }
}

int nk64_task_alive(int id)
{
    if (id < 0 || (uint32_t)id >= NK64_MAX_TASKS) {
        return 0;
    }
    if (g_used[id] == 0u || g_tasks[id].state == NK_TASK_DEAD) {
        return 0;
    }
    return 1;
}

/* Extra accessors used by the syscall layer (declared extern there). */
int nk64_task_current(void)
{
    return g_current;
}



uint64_t nk64_task_current_cr3(void)
{
    if (g_current < 0) {
        return 0u;
    }
    return g_tasks[g_current].cr3;
}

void nk64_task_exit_current(void)
{
    if (g_current <= 0) {
        return;
    }
    g_tasks[g_current].state = NK_TASK_DEAD;
    g_tasks[g_current].slice_left = 0u;
    nk64_switch_now();
    for (;;) {
        nk64_hlt();
    }
}
