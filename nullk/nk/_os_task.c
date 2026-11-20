#include "kern.h"
#include "arch_if.h"

/* Stacks are part of the static kernel image; no dynamic allocation. */
static uint32_t nkk_stacks[NK_MAX_TASKS][NKK_STACK_WORDS]
    __attribute__((aligned(8)));

static void nkk_copy_name(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int nkk_task_id(const nkk_tcb_t *t) {
    if (t == 0) {
        return -1;
    }
    return (int)(t - nkk_tasks);
}

nkk_tcb_t *nkk_task_by_id(int id) {
    if (id < 0 || id >= (int)NK_MAX_TASKS) {
        return 0;
    }
    return &nkk_tasks[id];
}

int nkk_task_create(const char *name, uint32_t priority, uint32_t stack_words,
                    void (*entry)(void *arg), void *arg, uint32_t parent) {
    int id;
    nkk_tcb_t *t;
    uint32_t *frame;
    uint32_t words;

    if (priority >= NK_TASK_PRIORITIES || entry == 0) {
        return -1;
    }
    words = stack_words;
    if (words < 64u) {
        words = NKK_STACK_WORDS;
    }
    if (words > NKK_STACK_WORDS) {
        words = NKK_STACK_WORDS;
    }

    for (id = 0; id < (int)NK_MAX_TASKS; id++) {
        if (!nkk_tasks[id].in_use) {
            break;
        }
    }
    if (id >= (int)NK_MAX_TASKS) {
        return -1;
    }

    t = &nkk_tasks[id];
    for (uint32_t i = 0; i < sizeof(*t); i++) {
        ((uint8_t *)t)[i] = 0;
    }

    t->in_use = 1;
    t->state = NK_TASK_READY;
    t->priority = (uint8_t)priority;
    t->parent = parent;
    t->stack_words = words;
    t->slice_left = NK_TASK_QUANTUM_TICKS;
    t->stack_base = nkk_stacks[id];
    nkk_copy_name(t->name, name, NK_TASK_NAME_MAX);

    /* Initial Cortex-M exception frame: r4-r11 slot, then r0,r1,r2,r3,
     * r12, lr, pc, xPSR. PendSV pops r4-r11; exception return pops the rest. */
    frame = &nkk_stacks[id][NKK_STACK_WORDS - 16u];
    frame[8] = (uint32_t)(uintptr_t)arg;   /* r0 */
    frame[9] = 0;               /* r1 */
    frame[10] = 0;              /* r2 */
    frame[11] = 0;              /* r3 */
    frame[12] = 0;              /* r12 */
    frame[13] = (uint32_t)(uintptr_t)&nkk_task_trampoline; /* lr */
    frame[14] = (uint32_t)(uintptr_t)entry;                /* pc */
    frame[15] = 0x01000000u;    /* xPSR: Thumb bit */
    t->sp = frame;

    /* Every task starts with one capability: the kernel endpoint. */
    t->cap_handle[1] = 1;
    t->cap_obj[1] = 0;
    t->cap_type[1] = NKK_OBJ_ENDPOINT;
    t->cap_rights[1] = (uint8_t)(NK_RIGHT_READ | NK_RIGHT_WRITE | NK_RIGHT_GRANT);

    nkk_sched_ready(id);
    return id;
}

void nkk_task_destroy(int id) {
    nkk_tcb_t *t = nkk_task_by_id(id);
    if (t == 0 || !t->in_use) {
        return;
    }
    nkk_sched_unready(id);
    t->state = NK_TASK_DEAD;
    t->wait_kind = NKK_WAIT_NONE;
    for (uint32_t i = 0; i < NK_CAP_PER_TASK; i++) {
        t->cap_handle[i] = 0;
        t->cap_obj[i] = 0;
        t->cap_type[i] = NKK_OBJ_NONE;
        t->cap_rights[i] = 0;
    }
}

void nkk_task_trampoline(void *arg) {
    (void)arg;
    /* A normal return from a task entry is an exit with code 0. */
    (void)nkk_syscall(NK_SC_TASK_EXIT, 0, 0, 0, 0);
    for (;;) {
        nkk_arch_yield();
    }
}
