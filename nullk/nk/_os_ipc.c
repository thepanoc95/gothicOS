#include "kern.h"

static uint32_t nkk_next_token = 1;

static void nkk_msg_copy(nk_msg_t *dst, const nk_msg_t *src) {
    for (uint32_t i = 0; i < sizeof(nk_msg_t); i++) {
        ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];
    }
    if (dst->length > NK_IPC_PAYLOAD_MAX) {
        dst->length = NK_IPC_PAYLOAD_MAX;
    }
}

static nkk_endpoint_t *nkk_ep(uint16_t idx) {
    if (idx >= NK_MAX_ENDPOINTS || !nkk_endpoints[idx].in_use) {
        return 0;
    }
    return &nkk_endpoints[idx];
}

static int nkk_find_waiter(uint8_t kind, uint32_t obj, uint32_t token) {
    for (int id = 0; id < (int)NK_MAX_TASKS; id++) {
        nkk_tcb_t *t = &nkk_tasks[id];
        if (t->in_use && t->state == NK_TASK_BLOCKED &&
            t->wait_kind == kind && t->wait_obj == obj &&
            (kind != NKK_WAIT_REPLY || t->wait_token == token)) {
            return id;
        }
    }
    return -1;
}

static nk_status_t nkk_ep_post(uint16_t ep_idx, const nk_msg_t *msg) {
    nkk_endpoint_t *e = nkk_ep(ep_idx);
    uint16_t slot;

    if (e == 0) {
        return NK_STATUS_NOT_FOUND;
    }

    int waiter = nkk_find_waiter(NKK_WAIT_RECV, ep_idx, 0);
    if (waiter >= 0) {
        nkk_tcb_t *t = &nkk_tasks[waiter];
        nkk_msg_copy(&t->rx, msg);
        if (t->recv_dst != 0) {
            nkk_msg_copy((nk_msg_t *)(uintptr_t)t->recv_dst, msg);
            t->recv_dst = 0;
        }
        t->wait_kind = NKK_WAIT_NONE;
        t->wait_obj = 0;
        t->wait_token = 0;
        nkk_sched_wake(waiter);
        return NK_STATUS_OK;
    }

    if (e->count >= e->capacity) {
        return NK_STATUS_QUEUE_FULL;
    }
    slot = (uint16_t)((e->head + e->count) % e->capacity);
    nkk_msg_copy(&e->msg[slot], msg);
    e->count++;
    return NK_STATUS_OK;
}

static nk_status_t nkk_ep_get(uint16_t ep_idx, nk_msg_t *out) {
    nkk_endpoint_t *e = nkk_ep(ep_idx);
    if (e == 0 || e->count == 0) {
        return NK_STATUS_NOT_FOUND;
    }
    nkk_msg_copy(out, &e->msg[e->head]);
    e->head = (uint16_t)((e->head + 1) % e->capacity);
    e->count--;
    return NK_STATUS_OK;
}

void nkk_ipc_init(void) {
    nkk_endpoint_t *k = &nkk_endpoints[0];
    for (uint32_t i = 0; i < sizeof(*k); i++) {
        ((uint8_t *)k)[i] = 0;
    }
    k->in_use = 1;
    k->owner = (uint16_t)-1;
    k->capacity = NK_IPC_QUEUE_DEPTH;
    nkk_next_token = 1;
}

nk_status_t nkk_ipc_open(int owner, uint32_t cap, nk_handle_t *out) {
    uint16_t idx;
    nk_handle_t h;

    if (out == 0 || cap == 0 || cap > NK_IPC_QUEUE_DEPTH) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    for (idx = 0; idx < NK_MAX_ENDPOINTS; idx++) {
        if (!nkk_endpoints[idx].in_use) {
            break;
        }
    }
    if (idx >= NK_MAX_ENDPOINTS) {
        return NK_STATUS_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < sizeof(nkk_endpoint_t); i++) {
        ((uint8_t *)&nkk_endpoints[idx])[i] = 0;
    }
    nkk_endpoints[idx].in_use = 1;
    nkk_endpoints[idx].owner = (uint16_t)owner;
    nkk_endpoints[idx].capacity = (uint16_t)cap;

    h = nkk_cap_install(owner, NKK_OBJ_ENDPOINT, idx, NK_RIGHT_ALL);
    if (h == 0) {
        nkk_endpoints[idx].in_use = 0;
        return NK_STATUS_OUT_OF_MEMORY;
    }
    *out = h;
    return NK_STATUS_OK;
}

nk_status_t nkk_ipc_close(int task_id, nk_handle_t h) {
    uint16_t idx;
    nkk_endpoint_t *e;
    nk_status_t st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT,
                                    NK_RIGHT_MANAGE, &idx);
    if (st != NK_STATUS_OK) {
        return st;
    }
    e = nkk_ep(idx);
    if (e == 0) {
        return NK_STATUS_NOT_FOUND;
    }
    for (int id = 0; id < (int)NK_MAX_TASKS; id++) {
        nkk_tcb_t *t = &nkk_tasks[id];
        if (t->in_use && t->state == NK_TASK_BLOCKED &&
            t->wait_obj == idx &&
            (t->wait_kind == NKK_WAIT_RECV || t->wait_kind == NKK_WAIT_SEND ||
             t->wait_kind == NKK_WAIT_REPLY)) {
            t->rx.tag = 0;
            t->rx.length = 0;
            t->recv_dst = 0;
            t->wait_kind = NKK_WAIT_NONE;
            nkk_sched_wake(id);
        }
    }
    e->in_use = 0;
    return NK_STATUS_OK;
}

nk_status_t nkk_ipc_send(int task_id, nk_handle_t h, const nk_msg_t *msg) {
    uint16_t idx;
    nk_status_t st;
    nk_msg_t local;
    nkk_tcb_t *t;

    if (msg == 0 || msg->length > NK_IPC_PAYLOAD_MAX) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT, NK_RIGHT_WRITE, &idx);
    if (st != NK_STATUS_OK) {
        return st;
    }
    nkk_msg_copy(&local, msg);
    st = nkk_ep_post(idx, &local);
    if (st == NK_STATUS_OK) {
        return NK_STATUS_OK;
    }
    if (st != NK_STATUS_QUEUE_FULL) {
        return st;
    }

    t = nkk_task_by_id(task_id);
    if (t == 0) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    nkk_msg_copy(&t->tx, &local);
    nkk_sched_block(NKK_WAIT_SEND, idx, 0);
    return NK_STATUS_OK;
}

static void nkk_release_sender(int id, uint16_t ep_idx) {
    nkk_tcb_t *t = nkk_task_by_id(id);
    if (t == 0) {
        return;
    }
    t->wait_kind = NKK_WAIT_NONE;
    t->wait_obj = 0;
    if (t->wait_token != 0) {
        /* This sender used CALL: keep it blocked, now waiting for the reply. */
        t->wait_kind = NKK_WAIT_REPLY;
        t->wait_obj = ep_idx;
        return;
    }
    nkk_sched_wake(id);
}

nk_status_t nkk_ipc_recv(int task_id, nk_handle_t h, nk_msg_t *out) {
    uint16_t idx;
    nk_status_t st;

    if (out == 0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT, NK_RIGHT_READ, &idx);
    if (st != NK_STATUS_OK) {
        return st;
    }

    int sender = nkk_find_waiter(NKK_WAIT_SEND, idx, 0);
    if (sender >= 0) {
        nkk_tcb_t *s = &nkk_tasks[sender];
        nkk_msg_copy(out, &s->tx);
        nkk_release_sender(sender, idx);
        return NK_STATUS_OK;
    }

    st = nkk_ep_get(idx, out);
    if (st == NK_STATUS_OK) {
        return NK_STATUS_OK;
    }

    nkk_tcb_t *t = nkk_task_by_id(task_id);
    if (t == 0) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    t->recv_dst = (nkk_arg_t)(uintptr_t)out;
    nkk_sched_block(NKK_WAIT_RECV, idx, 0);
    return NK_STATUS_OK;
}

nk_status_t nkk_ipc_call(int task_id, nk_handle_t h, const nk_msg_t *msg,
                         nk_msg_t *reply) {
    uint16_t idx;
    nk_status_t st;
    nk_msg_t local;
    nkk_tcb_t *t;

    if (msg == 0 || reply == 0 || msg->length > NK_IPC_PAYLOAD_MAX) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT, NK_RIGHT_WRITE, &idx);
    if (st != NK_STATUS_OK) {
        return st;
    }

    t = nkk_task_by_id(task_id);
    if (t == 0) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    nkk_msg_copy(&local, msg);
    if (local.token == 0) {
        local.token = nkk_next_token++;
        if (nkk_next_token == 0) {
            nkk_next_token = 1;
        }
    }
    t->reply_dst = (nkk_arg_t)(uintptr_t)reply;

    st = nkk_ep_post(idx, &local);
    if (st == NK_STATUS_QUEUE_FULL) {
        nkk_msg_copy(&t->tx, &local);
        nkk_sched_block(NKK_WAIT_SEND, idx, local.token);
        return NK_STATUS_OK;
    }
    if (st != NK_STATUS_OK) {
        return st;
    }

    nkk_sched_block(NKK_WAIT_REPLY, idx, local.token);
    return NK_STATUS_OK;
}

nk_status_t nkk_ipc_reply(int task_id, nk_handle_t h, uint32_t token,
                          const nk_msg_t *msg) {
    uint16_t idx;
    nk_status_t st;
    nk_msg_t local;

    if (msg == 0 || token == 0 || msg->length > NK_IPC_PAYLOAD_MAX) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    st = nkk_cap_lookup(task_id, h, NKK_OBJ_ENDPOINT, NK_RIGHT_WRITE, &idx);
    if (st != NK_STATUS_OK) {
        return st;
    }
    nkk_msg_copy(&local, msg);

    int waiter = nkk_find_waiter(NKK_WAIT_REPLY, idx, token);
    if (waiter < 0) {
        return NK_STATUS_NOT_FOUND;
    }
    nkk_tcb_t *t = &nkk_tasks[waiter];
    if (t->reply_dst != 0) {
        nkk_msg_copy((nk_msg_t *)(uintptr_t)t->reply_dst, &local);
    }
    t->wait_kind = NKK_WAIT_NONE;
    t->wait_obj = 0;
    t->wait_token = 0;
    t->reply_dst = 0;
    nkk_sched_wake(waiter);
    return NK_STATUS_OK;
}

void nkk_ipc_post_irq(uint32_t source, uint32_t payload) {
    nk_msg_t m;
    m.tag = NK_MSG_IRQ;
    m.token = source;
    m.length = 4;
    m.reserved = 0;
    for (uint32_t i = 0; i < 4; i++) {
        m.data[i] = (uint8_t)(payload >> (i * 8u));
    }
    if (nkk_irqs[source].bound) {
        (void)nkk_ep_post(nkk_irqs[source].endpoint, &m);
    }
}
