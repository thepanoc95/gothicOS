#include <stdio.h>
#include <string.h>

#include "kern.h"
#include "nullk/libnk.h"

extern void nkk_host_reset(void);
extern uint32_t nkk_host_yields(void);
extern const char *nkk_host_output(void);

static int failures;
static int checks;

#define ARG(p) ((nkk_arg_t)(uintptr_t)(p))

#define CHECK(expr)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(expr)) {                                                       \
            failures++;                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);           \
        }                                                                    \
    } while (0)

static void dummy_entry(void *arg) {
    (void)arg;
}

static void set_current(int id) {
    nkk_current = nkk_task_by_id(id);
    nkk_current_id = id;
}

static nk_status_t open_endpoint_as(int id, uint32_t cap, nk_handle_t *h) {
    set_current(id);
    return (nk_status_t)nkk_syscall(NK_SC_IPC_OPEN, cap, ARG(h),
                                    0, 0);
}

static nk_status_t grant_as(int id, int target, nk_handle_t h, nk_handle_t *out) {
    set_current(id);
    return (nk_status_t)nkk_syscall(NK_SC_CAP_GRANT, (uint32_t)target, h,
                                    NK_RIGHT_ALL, ARG(out));
}

static void test_abi_layout(void) {
    CHECK(NULLK_ABI_VERSION == 0x00020000u);
    CHECK(NULLK_ABI_MAGIC == 0x4E4B5632u);
    CHECK(sizeof(nk_msg_t) == 64u);
    CHECK(sizeof(nk_info_t) == 64u);
    CHECK(NK_SC_COUNT == 24u);
}

static void test_scheduler(void) {
    int a;
    int b;

    nkk_core_init();
    a = nkk_task_create("a", 2, 128, dummy_entry, 0, 0);
    b = nkk_task_create("b", 2, 128, dummy_entry, 0, 0);
    CHECK(a == 0);
    CHECK(b == 1);

    (void)nkk_sched_pick();
    CHECK(nkk_current_id == a);
    CHECK(nkk_tasks[a].state == NK_TASK_RUNNING);

    nkk_current = 0;
    nkk_current_id = -1;
    (void)nkk_sched_pick();
    CHECK(nkk_current_id == b);

    nkk_core_init();
    a = nkk_task_create("low", 5, 128, dummy_entry, 0, 0);
    b = nkk_task_create("high", 1, 128, dummy_entry, 0, 0);
    (void)nkk_sched_pick();
    CHECK(nkk_current_id == b);
    (void)a;
}

static void test_sleep_wake(void) {
    int id;
    nkk_host_reset();

    nkk_core_init();
    id = nkk_task_create("sleeper", 3, 128, dummy_entry, 0, 0);
    set_current(id);

    CHECK(nkk_syscall(NK_SC_SLEEP, 5, 0, 0, 0) == NK_STATUS_OK);
    CHECK(nkk_tasks[id].state == NK_TASK_BLOCKED);
    CHECK(nkk_tasks[id].wait_kind == 4u); /* NKK_WAIT_SLEEP */
    CHECK(nkk_host_yields() == 1);

    for (uint32_t i = 0; i < 4; i++) {
        nkk_sched_on_tick();
    }
    CHECK(nkk_tasks[id].state == NK_TASK_BLOCKED);
    nkk_sched_on_tick();
    CHECK(nkk_tasks[id].state == NK_TASK_READY);
}

static void test_ipc_queue_and_blocking(void) {
    int a;
    int b;
    nk_handle_t h;
    nk_handle_t hb;
    nk_msg_t msg;
    nk_msg_t out;

    nkk_core_init();
    a = nkk_task_create("sender", 2, 128, dummy_entry, 0, 0);
    b = nkk_task_create("reader", 3, 128, dummy_entry, 0, 0);
    CHECK(open_endpoint_as(a, 2, &h) == NK_STATUS_OK);
    CHECK(h == 2);
    CHECK(grant_as(a, b, h, &hb) == NK_STATUS_OK);
    CHECK(hb == 2);

    memset(&msg, 0, sizeof(msg));
    msg.tag = NK_MSG_DATA;
    msg.length = 4;
    memcpy(msg.data, "ping", 4);

    set_current(b);
    memset(&out, 0, sizeof(out));
    CHECK(nkk_syscall(NK_SC_IPC_RECV, hb, ARG(&out), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_tasks[b].state == NK_TASK_BLOCKED);

    set_current(a);
    CHECK(nkk_syscall(NK_SC_IPC_SEND, h, ARG(&msg), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_tasks[b].state == NK_TASK_READY);
    CHECK(nkk_tasks[b].rx.tag == NK_MSG_DATA);
    CHECK(nkk_tasks[b].rx.length == 4);

    /* Fill the queue, then block a second sender. */
    set_current(a);
    CHECK(nkk_syscall(NK_SC_IPC_SEND, h, ARG(&msg), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_syscall(NK_SC_IPC_SEND, h, ARG(&msg), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_syscall(NK_SC_IPC_SEND, h, ARG(&msg), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_tasks[a].state == NK_TASK_BLOCKED);

    set_current(b);
    memset(&out, 0, sizeof(out));
    CHECK(nkk_syscall(NK_SC_IPC_RECV, hb, ARG(&out), 0, 0) ==
          NK_STATUS_OK);
    CHECK(out.tag == NK_MSG_DATA);
    memset(&out, 0, sizeof(out));
    CHECK(nkk_syscall(NK_SC_IPC_RECV, hb, ARG(&out), 0, 0) ==
          NK_STATUS_OK);
    CHECK(out.tag == NK_MSG_DATA);
    memset(&out, 0, sizeof(out));
    CHECK(nkk_syscall(NK_SC_IPC_RECV, hb, ARG(&out), 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_tasks[a].state == NK_TASK_READY);
}

static void test_call_reply(void) {
    int caller;
    int server;
    nk_handle_t h;
    nk_handle_t sh;
    nk_msg_t req;
    nk_msg_t reply;
    nk_msg_t got;

    nkk_core_init();
    caller = nkk_task_create("caller", 2, 128, dummy_entry, 0, 0);
    server = nkk_task_create("server", 3, 128, dummy_entry, 0, 0);
    CHECK(open_endpoint_as(caller, 4, &h) == NK_STATUS_OK);
    CHECK(grant_as(caller, server, h, &sh) == NK_STATUS_OK);

    memset(&req, 0, sizeof(req));
    req.tag = NK_MSG_PING;
    req.length = 4;
    memcpy(req.data, "ping", 4);
    memset(&reply, 0, sizeof(reply));

    set_current(caller);
    CHECK(nkk_syscall(NK_SC_IPC_CALL, h, ARG(&req),
                      ARG(&reply), 0) == NK_STATUS_OK);
    CHECK(nkk_tasks[caller].state == NK_TASK_BLOCKED);
    CHECK(nkk_tasks[caller].wait_kind == 3u); /* NKK_WAIT_REPLY */

    set_current(server);
    memset(&got, 0, sizeof(got));
    CHECK(nkk_syscall(NK_SC_IPC_RECV, sh, ARG(&got), 0, 0) ==
          NK_STATUS_OK);
    CHECK(got.tag == NK_MSG_PING);
    CHECK(got.token != 0);

    memset(&reply, 0, sizeof(reply));
    reply.tag = NK_MSG_PONG;
    reply.token = got.token;
    reply.length = 4;
    memcpy(reply.data, "pong", 4);
    CHECK(nkk_syscall(NK_SC_IPC_REPLY, sh, got.token,
                      ARG(&reply), 0) == NK_STATUS_OK);
    CHECK(nkk_tasks[caller].state == NK_TASK_READY);
    CHECK(reply.tag == NK_MSG_PONG);
}

static void test_capabilities(void) {
    int a;
    int b;
    nk_handle_t h;
    uint16_t obj = 0;

    nkk_core_init();
    a = nkk_task_create("a", 2, 128, dummy_entry, 0, 0);
    b = nkk_task_create("b", 2, 128, dummy_entry, 0, 0);
    CHECK(open_endpoint_as(a, 2, &h) == NK_STATUS_OK);

    CHECK(nkk_cap_lookup(a, h, NKK_OBJ_ENDPOINT, NK_RIGHT_READ, &obj) ==
          NK_STATUS_OK);
    CHECK(nkk_cap_lookup(b, h, NKK_OBJ_ENDPOINT, NK_RIGHT_READ, &obj) ==
          NK_STATUS_NOT_FOUND);

    set_current(a);
    CHECK(nkk_syscall(NK_SC_CAP_GRANT, (uint32_t)b, h, NK_RIGHT_READ,
                      ARG(&h)) == NK_STATUS_OK);
    CHECK(nkk_cap_lookup(b, h, NKK_OBJ_ENDPOINT, NK_RIGHT_READ, &obj) ==
          NK_STATUS_OK);
    CHECK(nkk_cap_lookup(b, h, NKK_OBJ_ENDPOINT, NK_RIGHT_WRITE, &obj) ==
          NK_STATUS_PERMISSION_DENIED);

    set_current(a);
    CHECK(nkk_syscall(NK_SC_CAP_REVOKE, (uint32_t)b, h, 0, 0) == NK_STATUS_OK);
    CHECK(nkk_cap_lookup(b, h, NKK_OBJ_ENDPOINT, NK_RIGHT_READ, &obj) ==
          NK_STATUS_NOT_FOUND);
}

static void test_memory_and_info(void) {
    nk_mem_desc_t d1;
    nk_mem_desc_t d2;
    nk_mem_info_t mi;
    nk_info_t info;

    nkk_core_init();
    CHECK(nkk_syscall(NK_SC_MEM_ALLOC, ARG(&d1), 0, 0, 0) ==
          NK_STATUS_OK);
    CHECK(nkk_syscall(NK_SC_MEM_ALLOC, ARG(&d2), 0, 0, 0) ==
          NK_STATUS_OK);
    CHECK(d1.block_id != d2.block_id);
    CHECK(d1.ptr != 0);

    nkk_mem_info(&mi);
    CHECK(mi.used == 2);
    CHECK(mi.block_size == NK_MEM_BLOCK_SIZE);

    CHECK(nkk_syscall(NK_SC_MEM_FREE, d1.block_id, 0, 0, 0) == NK_STATUS_OK);
    nkk_mem_info(&mi);
    CHECK(mi.used == 1);

    nkk_set_runlevel(NK_RUNLEVEL_READY);
    memset(&info, 0, sizeof(info));
    CHECK(nkk_syscall(NK_SC_GET_INFO, ARG(&info), 0, 0, 0) ==
          NK_STATUS_OK);
    CHECK(info.abi_version == NULLK_ABI_VERSION);
    CHECK(info.magic == NULLK_ABI_MAGIC);
    CHECK((info.features & NK_FEATURE_PREEMPT) != 0);
    CHECK(info.runlevel == NK_RUNLEVEL_READY);
    CHECK(info.msg_payload_max == NK_IPC_PAYLOAD_MAX);
}

static uint32_t host_op;
static uint32_t host_a0;

static uint32_t host_sc(uint32_t op, uint32_t a0, uint32_t a1, uint32_t a2,
                        uint32_t a3) {
    (void)a1;
    (void)a2;
    (void)a3;
    host_op = op;
    host_a0 = a0;
    return 42;
}

static void test_libnk(void) {
    nk_msg_t msg;
    char buf[16];

    nk_host_set_syscall(&host_sc);
    CHECK(nk_ticks() == 42);
    CHECK(host_op == NK_SC_TICKS);
    CHECK(nk_log("hello", 5) == 42);
    CHECK(host_op == NK_SC_LOG);
    CHECK(host_a0 == (uint32_t)(uintptr_t)"hello");

    nk_msg_init(&msg, NK_MSG_PING);
    CHECK(msg.tag == NK_MSG_PING);
    CHECK(msg.length == 0);
    CHECK(nk_msg_set_data(&msg, "abc", 3) == NK_STATUS_OK);
    CHECK(msg.length == 3);
    CHECK(nk_memcmp(msg.data, "abc", 3) == 0);
    CHECK(nk_msg_set_data(&msg, "too long", NK_IPC_PAYLOAD_MAX + 1) ==
          NK_STATUS_INVALID_ARGUMENT);

    CHECK(nk_strcmp("nullk", "nullk") == 0);
    CHECK(nk_utoa(255, buf, sizeof(buf), 16) == buf);
    CHECK(nk_strcmp(buf, "ff") == 0);
}

int main(void) {
    test_abi_layout();
    test_scheduler();
    test_sleep_wake();
    test_ipc_queue_and_blocking();
    test_call_reply();
    test_capabilities();
    test_memory_and_info();
    test_libnk();

    printf("nullk host tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
