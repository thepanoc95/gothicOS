#include "kern.h"
#include "arch_if.h"

static nk_status_t nkk_copy_in(void *dst, const void *src, uint32_t len) {
    if (dst == 0 || src == 0 || len > NK_LOG_MAX * 2u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < len; i++) {
        ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];
    }
    return NK_STATUS_OK;
}

static nk_status_t nkk_copy_out(void *dst, const void *src, uint32_t len) {
    return nkk_copy_in(dst, src, len);
}

static void nkk_fill_task_info(int id, nk_task_info_t *out) {
    nkk_tcb_t *t = nkk_task_by_id(id);
    for (uint32_t i = 0; i < NK_TASK_NAME_MAX; i++) {
        out->name[i] = 0;
    }
    if (t == 0 || !t->in_use) {
        out->id = 0;
        out->state = 0;
        out->priority = 0;
        out->parent = 0;
        out->slice_left = 0;
        out->reserved = 0;
        return;
    }
    out->id = (uint32_t)id;
    out->state = t->state;
    out->priority = t->priority;
    out->parent = t->parent;
    out->slice_left = t->slice_left;
    out->reserved = 0;
    for (uint32_t i = 0; i + 1 < NK_TASK_NAME_MAX && t->name[i] != 0; i++) {
        out->name[i] = t->name[i];
    }
}

uint32_t nkk_syscall(uint32_t op, nkk_arg_t a0, nkk_arg_t a1, nkk_arg_t a2,
                     nkk_arg_t a3) {
    int id = nkk_current_id;
    nk_status_t st;

    switch (op) {
    case NK_SC_GET_INFO: {
        nk_info_t info;
        uint32_t tasks = 0;
        uint32_t eps = 0;
        if (a0 == 0) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        for (uint32_t i = 0; i < NK_MAX_TASKS; i++) {
            if (nkk_tasks[i].in_use && nkk_tasks[i].state != NK_TASK_DEAD) {
                tasks++;
            }
        }
        for (uint32_t i = 0; i < NK_MAX_ENDPOINTS; i++) {
            if (nkk_endpoints[i].in_use) {
                eps++;
            }
        }
        info.abi_version = NULLK_ABI_VERSION;
        info.magic = NULLK_ABI_MAGIC;
        info.features = NK_FEATURE_TASKS | NK_FEATURE_IPC | NK_FEATURE_CAPS |
                        NK_FEATURE_MEMORY | NK_FEATURE_IRQ | NK_FEATURE_PREEMPT;
        info.runlevel = nkk_runlevel;
        info.tick_hz = NKK_TICK_HZ;
        info.ticks = nkk_tick_count;
        info.max_tasks = NK_MAX_TASKS - 1u;
        info.task_count = tasks;
        info.max_endpoints = NK_MAX_ENDPOINTS;
        info.endpoint_count = eps;
        info.msg_payload_max = NK_IPC_PAYLOAD_MAX;
        for (uint32_t i = 0; i < 5; i++) {
            info.reserved[i] = 0;
        }
        st = nkk_copy_out((void *)(uintptr_t)a0, &info, sizeof(info));
        return st;
    }

    case NK_SC_TASK_SELF:
        return (uint32_t)id;

    case NK_SC_TASK_CREATE: {
        nk_task_create_info_t ci;
        int new_id;
        if (a0 == 0 || nkk_copy_in(&ci, (const void *)(uintptr_t)a0,
                                   sizeof(ci)) != NK_STATUS_OK) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        new_id = nkk_task_create(ci.name, ci.priority, ci.stack_words,
                                 ci.entry, ci.arg, (uint32_t)id);
        if (new_id < 0) {
            return NK_STATUS_OUT_OF_MEMORY;
        }
        ci.out_id = (uint32_t)new_id;
        return nkk_copy_out((void *)(uintptr_t)a0, &ci, sizeof(ci));
    }

    case NK_SC_TASK_EXIT: {
        (void)a0;
        nkk_task_destroy(id);
        nkk_sched_unready(id);
        nkk_sched_request_switch();
        return NK_STATUS_OK;
    }

    case NK_SC_TASK_STATE: {
        nk_task_info_t ti;
        if (a1 == 0) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        nkk_fill_task_info((int)a0, &ti);
        return nkk_copy_out((void *)(uintptr_t)a1, &ti, sizeof(ti));
    }

    case NK_SC_YIELD:
        nkk_sched_request_switch();
        return NK_STATUS_OK;

    case NK_SC_SLEEP: {
        uint32_t ticks = a0;
        nkk_tcb_t *t = nkk_current;
        if (t == 0 || id < 0) {
            return NK_STATUS_INTERNAL_ERROR;
        }
        t->wake_tick = nkk_tick_count + (ticks == 0 ? 1u : ticks);
        nkk_sched_block(NKK_WAIT_SLEEP, 0, 0);
        return NK_STATUS_OK;
    }

    case NK_SC_TICKS:
        return nkk_tick_count;

    case NK_SC_IPC_OPEN:
        return nkk_ipc_open(id, a0, (nk_handle_t *)(uintptr_t)a1);

    case NK_SC_IPC_CLOSE:
        return nkk_ipc_close(id, (nk_handle_t)a0);

    case NK_SC_IPC_SEND:
        return nkk_ipc_send(id, (nk_handle_t)a0,
                            (const nk_msg_t *)(uintptr_t)a1);

    case NK_SC_IPC_RECV:
        return nkk_ipc_recv(id, (nk_handle_t)a0, (nk_msg_t *)(uintptr_t)a1);

    case NK_SC_IPC_CALL:
        return nkk_ipc_call(id, (nk_handle_t)a0,
                            (const nk_msg_t *)(uintptr_t)a1,
                            (nk_msg_t *)(uintptr_t)a2);

    case NK_SC_IPC_REPLY:
        return nkk_ipc_reply(id, (nk_handle_t)a0, a1,
                             (const nk_msg_t *)(uintptr_t)a2);

    case NK_SC_CAP_GRANT: {
        nk_handle_t out = 0;
        st = nkk_cap_grant(id, (int)a0, (nk_handle_t)a1, a2, &out);
        if (st == NK_STATUS_OK && a3 != 0) {
            st = nkk_copy_out((void *)(uintptr_t)a3, &out, sizeof(out));
        }
        return st;
    }

    case NK_SC_CAP_REVOKE:
        return nkk_cap_revoke(id, (int)a0, (nk_handle_t)a1);

    case NK_SC_MEM_ALLOC:
        return nkk_mem_alloc(id, (nk_mem_desc_t *)(uintptr_t)a0);

    case NK_SC_MEM_FREE:
        return nkk_mem_free(a0);

    case NK_SC_MEM_INFO:
        if (a0 == 0) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        nkk_mem_info((nk_mem_info_t *)(uintptr_t)a0);
        return NK_STATUS_OK;

    case NK_SC_IRQ_BIND:
        return nkk_irq_bind(a0, (nk_handle_t)a1, id);

    case NK_SC_IRQ_UNBIND:
        return nkk_irq_unbind(a0, id);

    case NK_SC_IRQ_RAISE:
        return nkk_irq_raise(a0, a1);

    case NK_SC_LOG: {
        uint32_t len = a1;
        if (a0 == 0) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        if (len > NK_LOG_MAX) {
            len = NK_LOG_MAX;
        }
        st = nkk_copy_in(nkk_log_buffer, (const void *)(uintptr_t)a0, len);
        if (st != NK_STATUS_OK) {
            return st;
        }
        nkk_log_buffer[len] = '\0';
        nkk_console_write(nkk_log_buffer, len);
        return NK_STATUS_OK;
    }

    case NK_SC_DEBUG:
        /* Reserved for future trace/telemetry commands. */
        (void)a0;
        (void)a1;
        return NK_STATUS_UNSUPPORTED;

    default:
        return NK_STATUS_UNSUPPORTED;
    }
}

void nkk_svc_handler_c(uint32_t *stack) {
    const uint8_t *pc = (const uint8_t *)(uintptr_t)stack[6];
    uint32_t op = (uint32_t)pc[-2]; /* imm8 of the 16-bit Thumb SVC */
    stack[0] = nkk_syscall(op, stack[0], stack[1], stack[2], stack[3]);
}
