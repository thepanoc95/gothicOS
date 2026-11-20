/*
 * libnk - freestanding kernel-native library for NULLK ABI v2.
 *
 * No libc, no POSIX, no file descriptors. All functions map directly to one
 * SVC syscall or to pure in-task helpers.
 */
#ifndef NULLK_LIBNK_H
#define NULLK_LIBNK_H

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* System / task */
void nk_get_info(nk_info_t *out);
uint32_t nk_task_self(void);
nk_status_t nk_task_create(const char *name, uint32_t priority,
                           uint32_t stack_words,
                           void (*entry)(void *arg), void *arg,
                           uint32_t *out_id);
nk_status_t nk_task_state(uint32_t task_id, nk_task_info_t *out);
void nk_task_exit(uint32_t code) __attribute__((noreturn));
nk_status_t nk_yield(void);
nk_status_t nk_sleep(uint32_t ticks);
nk_tick_t nk_ticks(void);

/* IPC and capabilities */
nk_status_t nk_ipc_open(uint32_t capacity, nk_handle_t *out);
nk_status_t nk_ipc_close(nk_handle_t h);
nk_status_t nk_ipc_send(nk_handle_t h, const nk_msg_t *msg);
nk_status_t nk_ipc_recv(nk_handle_t h, nk_msg_t *out);
nk_status_t nk_ipc_call(nk_handle_t h, nk_msg_t *msg, nk_msg_t *reply);
nk_status_t nk_ipc_reply(nk_handle_t h, uint32_t token, const nk_msg_t *msg);
nk_status_t nk_cap_grant(uint32_t task_id, nk_handle_t h, uint32_t rights,
                         nk_handle_t *out);
nk_status_t nk_cap_revoke(uint32_t task_id, nk_handle_t h);

/* Fixed-block memory */
nk_status_t nk_mem_alloc(nk_mem_desc_t *out);
nk_status_t nk_mem_free(uint32_t block_id);
nk_status_t nk_mem_info(nk_mem_info_t *out);

/* Interrupts and console */
nk_status_t nk_irq_bind(uint32_t source, nk_handle_t h);
nk_status_t nk_irq_unbind(uint32_t source);
nk_status_t nk_irq_raise(uint32_t source, uint32_t payload);
nk_status_t nk_log(const char *text, uint32_t len);
nk_status_t nk_debug(uint32_t op, uint32_t arg);

/* Message helpers (pure in-task, no syscalls) */
void nk_msg_init(nk_msg_t *msg, uint32_t tag);
nk_status_t nk_msg_set_data(nk_msg_t *msg, const void *data, uint32_t len);
const uint8_t *nk_msg_data(const nk_msg_t *msg, uint32_t *len_out);

/* Freestanding string/memory helpers */
size_t nk_strlen(const char *s);
void *nk_memcpy(void *dst, const void *src, size_t n);
void *nk_memset(void *dst, int value, size_t n);
int nk_memcmp(const void *a, const void *b, size_t n);
int nk_strcmp(const char *a, const char *b);
char *nk_utoa(uint32_t value, char *buf, size_t buf_len, uint32_t base);

/* Minimal formatter: supports %s, %u, %d, %x, %p, %% and width via %0Nx. */
void nk_logf(const char *fmt, ...);

/* Host-test hook. Baremetal builds never call this directly. */
typedef uint32_t (*nk_host_syscall_fn)(uint32_t op, uint32_t a0, uint32_t a1,
                                       uint32_t a2, uint32_t a3);
void nk_host_set_syscall(nk_host_syscall_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LIBNK_H */
