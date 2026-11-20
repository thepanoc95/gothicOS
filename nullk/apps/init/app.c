/*
 * Initial task set. These tasks are linked into the kernel image but run in
 * Thread mode and only talk to the kernel through libnk / SVC.
 */
#include "../../nk/kern.h"
#include "../../include/nullk/libnk.h"

static void server_task(void *arg) {
    nk_msg_t req;
    nk_msg_t rep;
    (void)arg;

    for (;;) {
        if (nk_ipc_recv(1, &req) != NK_STATUS_OK) {
            continue;
        }
        if (req.tag == NK_MSG_PING) {
            nk_msg_init(&rep, NK_MSG_PONG);
            rep.token = req.token;
            rep.length = req.length;
            for (uint32_t i = 0; i < req.length && i < NK_IPC_PAYLOAD_MAX; i++) {
                rep.data[i] = req.data[i];
            }
            (void)nk_ipc_reply(1, req.token, &rep);
            nk_logf("[app] server answered token %u\n", req.token);
        }
    }
}

static void client_task(void *arg) {
    nk_msg_t req;
    nk_msg_t rep;
    uint32_t seq = 1;
    (void)arg;

    for (;;) {
        nk_msg_init(&req, NK_MSG_PING);
        req.token = seq++;
        req.length = 4;
        req.data[0] = 'p';
        req.data[1] = 'i';
        req.data[2] = 'n';
        req.data[3] = 'g';
        if (nk_ipc_call(1, &req, &rep) == NK_STATUS_OK &&
            rep.tag == NK_MSG_PONG) {
            nk_logf("[app] client got pong %u\n", rep.token);
        }
        (void)nk_sleep(200);
    }
}

static void heartbeat_task(void *arg) {
    (void)arg;
    for (;;) {
        nk_logf("[app] heartbeat tick=%u\n", nk_ticks());
        (void)nk_sleep(300);
    }
}

void nkk_app_entry_task(void *arg) {
    nk_info_t info;
    (void)arg;
    nk_get_info(&info);
    nk_logf("[app] ABI %u.%u runlevel=%u tasks=%u\n",
            (info.abi_version >> 16) & 0xffu,
            (info.abi_version >> 8) & 0xffu,
            info.runlevel, info.task_count);
}

void nkk_app_init(void) {
    (void)nkk_task_create("server", 2, NKK_STACK_WORDS, &server_task, 0, 0);
    (void)nkk_task_create("client", 3, NKK_STACK_WORDS, &client_task, 0, 0);
    (void)nkk_task_create("beacon", 4, NKK_STACK_WORDS, &heartbeat_task, 0, 0);
    (void)nkk_task_create("intro", 5, 128, &nkk_app_entry_task, 0, 0);
}
