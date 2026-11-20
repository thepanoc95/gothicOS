#include "kern.h"
#include "arch_if.h"

#define NKK_IDLE_ID ((int)(NK_MAX_TASKS - 1u))
static uint32_t nkk_cursor[NK_TASK_PRIORITIES];
static uint32_t nkk_idle_stack[NKK_STACK_WORDS] __attribute__((aligned(8)));

static uint32_t nkk_bit(int id) {
    return 1u << (uint32_t)id;
}

static void nkk_make_frame(nkk_tcb_t *t, uint32_t *stack,
                           void (*entry)(void *arg), void *arg) {
    uint32_t *frame = &stack[NKK_STACK_WORDS - 16u];
    frame[8] = (uint32_t)(uintptr_t)arg;
    frame[9] = 0;
    frame[10] = 0;
    frame[11] = 0;
    frame[12] = 0;
    frame[13] = (uint32_t)(uintptr_t)&nkk_task_trampoline;
    frame[14] = (uint32_t)(uintptr_t)entry;
    frame[15] = 0x01000000u;
    t->stack_base = stack;
    t->stack_words = NKK_STACK_WORDS;
    t->sp = frame;
}

static void nkk_idle_loop(void *arg) {
    (void)arg;
    for (;;) {
        nkk_arch_idle();
    }
}

void nkk_sched_init(void) {
    nkk_tcb_t *idle = &nkk_tasks[NKK_IDLE_ID];
    for (uint32_t p = 0; p < NK_TASK_PRIORITIES; p++) {
        nkk_cursor[p] = 0;
    }

    /* Reserve the last slot for the idle task; it is never in a ready mask. */
    for (uint32_t i = 0; i < sizeof(*idle); i++) {
        ((uint8_t *)idle)[i] = 0;
    }
    idle->in_use = 1;
    idle->state = NK_TASK_READY;
    idle->priority = (uint8_t)(NK_TASK_PRIORITIES - 1u);
    idle->slice_left = NK_TASK_QUANTUM_TICKS;
    idle->name[0] = 'i';
    idle->name[1] = 'd';
    idle->name[2] = 'l';
    idle->name[3] = 'e';
    idle->name[4] = '\0';
    nkk_make_frame(idle, nkk_idle_stack, &nkk_idle_loop, 0);
}

void nkk_sched_ready(int id) {
    nkk_tcb_t *t;
    if (id < 0 || id >= NKK_IDLE_ID) {
        return;
    }
    t = nkk_task_by_id(id);
    if (t == 0 || !t->in_use || t->state == NK_TASK_DEAD) {
        return;
    }
    if (t->state != NK_TASK_RUNNING) {
        t->state = NK_TASK_READY;
    }
    t->slice_left = NK_TASK_QUANTUM_TICKS;
    nkk_ready[t->priority] |= nkk_bit(id);
}

void nkk_sched_unready(int id) {
    nkk_tcb_t *t = nkk_task_by_id(id);
    if (t != 0 && t->priority < NK_TASK_PRIORITIES) {
        nkk_ready[t->priority] &= ~nkk_bit(id);
    }
}

void nkk_sched_request_switch(void) {
    nkk_need_switch = 1;
    nkk_arch_yield();
}

void nkk_sched_block(uint8_t kind, uint32_t obj, uint32_t token) {
    nkk_tcb_t *t = nkk_current;
    int id = nkk_current_id;
    if (t == 0 || id < 0) {
        return;
    }
    nkk_sched_unready(id);
    t->state = NK_TASK_BLOCKED;
    t->wait_kind = kind;
    t->wait_obj = obj;
    t->wait_token = token;
    t->slice_left = 0;
    nkk_sched_request_switch();
}

void nkk_sched_wake(int id) {
    nkk_tcb_t *t = nkk_task_by_id(id);
    if (t == 0 || !t->in_use || t->state != NK_TASK_BLOCKED) {
        return;
    }
    t->state = NK_TASK_READY;
    t->wait_kind = NKK_WAIT_NONE;
    t->wait_obj = 0;
    t->wait_token = 0;
    t->wake_tick = 0;
    nkk_sched_ready(id);

    if (nkk_current != 0 && t->priority < nkk_current->priority) {
        nkk_sched_request_switch();
    }
}

void nkk_sched_on_tick(void) {
    int id;

    nkk_tick_count++;

    for (id = 0; id < NKK_IDLE_ID; id++) {
        nkk_tcb_t *t = &nkk_tasks[id];
        if (t->in_use && t->state == NK_TASK_BLOCKED &&
            t->wait_kind == NKK_WAIT_SLEEP && t->wake_tick <= nkk_tick_count) {
            nkk_sched_wake(id);
        }
    }

    if (nkk_current != 0 && nkk_current->slice_left > 0) {
        nkk_current->slice_left--;
    }

    if (nkk_current != 0) {
        uint32_t p;
        int top = -1;
        for (p = 0; p < NK_TASK_PRIORITIES; p++) {
            if (nkk_ready[p] != 0) {
                /* Lowest set bit wins for highest-priority selection. */
                uint32_t mask = nkk_ready[p];
                for (id = 0; id < NKK_IDLE_ID; id++) {
                    if (mask & nkk_bit(id)) {
                        top = id;
                        break;
                    }
                }
                break;
            }
        }
        if (top >= 0 && nkk_current_id != NKK_IDLE_ID) {
            nkk_tcb_t *top_t = nkk_task_by_id(top);
            if (top_t->priority < nkk_current->priority ||
                nkk_current->slice_left == 0) {
                nkk_sched_request_switch();
            }
        } else if (top >= 0 && nkk_current_id == NKK_IDLE_ID) {
            nkk_sched_request_switch();
        }
    }
}

uint32_t *nkk_sched_pick(void) {
    uint32_t p;
    int chosen = -1;
    nkk_tcb_t *t;

    if (nkk_current != 0 && nkk_current->state == NK_TASK_RUNNING) {
        nkk_current->state = NK_TASK_READY;
    }

    for (p = 0; p < NK_TASK_PRIORITIES && chosen < 0; p++) {
        uint32_t mask = nkk_ready[p];
        uint32_t start = nkk_cursor[p] % (uint32_t)NK_MAX_TASKS;
        uint32_t k;
        if (mask == 0) {
            continue;
        }
        for (k = 0; k < NK_MAX_TASKS; k++) {
            int id = (int)((start + k) % (uint32_t)NK_MAX_TASKS);
            if (id >= NKK_IDLE_ID) {
                continue;
            }
            if (mask & nkk_bit(id)) {
                chosen = id;
                nkk_cursor[p] = (uint32_t)(id + 1);
                break;
            }
        }
    }

    if (chosen < 0) {
        chosen = NKK_IDLE_ID;
    }

    t = nkk_task_by_id(chosen);
    t->state = NK_TASK_RUNNING;
    t->slice_left = NK_TASK_QUANTUM_TICKS;
    nkk_next_id = chosen;
    nkk_current = t;
    nkk_current_id = chosen;
    nkk_need_switch = 0;
    return t->sp;
}

void nkk_sched_start(void) {
    nkk_next_id = -1;
    nkk_sched_request_switch();
}
