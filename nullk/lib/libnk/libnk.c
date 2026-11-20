#include "../../include/nullk/libnk.h"
#include <stdarg.h>

#ifndef NK_ARCH_HOST

/* Cortex-M: the syscall opcode is the SVC immediate; r0-r3 carry arguments. */
#define NK_SVC(nk_nr, a0, a1, a2, a3)                                        \
    ({                                                                        \
        register uint32_t nk_r0 __asm__("r0") = (uint32_t)(a0);               \
        register uint32_t nk_r1 __asm__("r1") = (uint32_t)(a1);               \
        register uint32_t nk_r2 __asm__("r2") = (uint32_t)(a2);               \
        register uint32_t nk_r3 __asm__("r3") = (uint32_t)(a3);               \
        __asm__ volatile("svc %c[op]\n"                                        \
                         : "+r"(nk_r0)                                        \
                         : [op] "i"(nk_nr), "r"(nk_r1), "r"(nk_r2),            \
                           "r"(nk_r3)                                          \
                         : "memory");                                          \
        nk_r0;                                                                 \
    })

#define NK_CALL0(op)                  NK_SVC(op, 0, 0, 0, 0)
#define NK_CALL1(op, a0)              NK_SVC(op, (uint32_t)(uintptr_t)(a0), 0, 0, 0)
#define NK_CALL2(op, a0, a1)          NK_SVC(op, (uint32_t)(uintptr_t)(a0), (uint32_t)(uintptr_t)(a1), 0, 0)
#define NK_CALL3(op, a0, a1, a2)      NK_SVC(op, (uint32_t)(uintptr_t)(a0), (uint32_t)(uintptr_t)(a1), (uint32_t)(uintptr_t)(a2), 0)
#define NK_CALL4(op, a0, a1, a2, a3)  NK_SVC(op, (uint32_t)(uintptr_t)(a0), (uint32_t)(uintptr_t)(a1), (uint32_t)(uintptr_t)(a2), (uint32_t)(uintptr_t)(a3))

#else

/* Host tests substitute a C callback for the svc instruction. */
static nk_host_syscall_fn nk_host_sc;

void nk_host_set_syscall(nk_host_syscall_fn fn) {
    nk_host_sc = fn;
}

static uint32_t nk_host_call(uint32_t op, uint32_t a0, uint32_t a1,
                             uint32_t a2, uint32_t a3) {
    if (nk_host_sc == 0) {
        return NK_STATUS_UNSUPPORTED;
    }
    return nk_host_sc(op, a0, a1, a2, a3);
}

#define NK_SVC(op, a0, a1, a2, a3)                                           \
    nk_host_call((op), (uint32_t)(uintptr_t)(a0),                             \
                 (uint32_t)(uintptr_t)(a1), (uint32_t)(uintptr_t)(a2),        \
                 (uint32_t)(uintptr_t)(a3))
#define NK_CALL0(op)                  NK_SVC(op, 0, 0, 0, 0)
#define NK_CALL1(op, a0)              NK_SVC(op, (a0), 0, 0, 0)
#define NK_CALL2(op, a0, a1)          NK_SVC(op, (a0), (a1), 0, 0)
#define NK_CALL3(op, a0, a1, a2)      NK_SVC(op, (a0), (a1), (a2), 0)
#define NK_CALL4(op, a0, a1, a2, a3)  NK_SVC(op, (a0), (a1), (a2), (a3))

#endif

void nk_get_info(nk_info_t *out) {
    (void)NK_CALL1(NK_SC_GET_INFO, out);
}

uint32_t nk_task_self(void) {
    return NK_CALL0(NK_SC_TASK_SELF);
}

nk_status_t nk_task_create(const char *name, uint32_t priority,
                           uint32_t stack_words,
                           void (*entry)(void *arg), void *arg,
                           uint32_t *out_id) {
    nk_task_create_info_t ci;
    nk_status_t st;
    ci.out_id = 0;
    ci.name = name;
    ci.priority = priority;
    ci.stack_words = stack_words;
    ci.entry = entry;
    ci.arg = arg;
    st = (nk_status_t)NK_CALL1(NK_SC_TASK_CREATE, &ci);
    if (out_id != 0) {
        *out_id = ci.out_id;
    }
    return st;
}

nk_status_t nk_task_state(uint32_t task_id, nk_task_info_t *out) {
    return (nk_status_t)NK_CALL2(NK_SC_TASK_STATE, task_id, out);
}

void nk_task_exit(uint32_t code) {
    (void)NK_CALL1(NK_SC_TASK_EXIT, code);
    for (;;) {
        (void)NK_CALL0(NK_SC_YIELD);
    }
}

nk_status_t nk_yield(void) {
    return (nk_status_t)NK_CALL0(NK_SC_YIELD);
}

nk_status_t nk_sleep(uint32_t ticks) {
    return (nk_status_t)NK_CALL1(NK_SC_SLEEP, ticks);
}

nk_tick_t nk_ticks(void) {
    return (nk_tick_t)NK_CALL0(NK_SC_TICKS);
}

nk_status_t nk_ipc_open(uint32_t capacity, nk_handle_t *out) {
    return (nk_status_t)NK_CALL2(NK_SC_IPC_OPEN, capacity, out);
}

nk_status_t nk_ipc_close(nk_handle_t h) {
    return (nk_status_t)NK_CALL1(NK_SC_IPC_CLOSE, h);
}

nk_status_t nk_ipc_send(nk_handle_t h, const nk_msg_t *msg) {
    return (nk_status_t)NK_CALL2(NK_SC_IPC_SEND, h, msg);
}

nk_status_t nk_ipc_recv(nk_handle_t h, nk_msg_t *out) {
    return (nk_status_t)NK_CALL2(NK_SC_IPC_RECV, h, out);
}

nk_status_t nk_ipc_call(nk_handle_t h, nk_msg_t *msg, nk_msg_t *reply) {
    return (nk_status_t)NK_CALL3(NK_SC_IPC_CALL, h, msg, reply);
}

nk_status_t nk_ipc_reply(nk_handle_t h, uint32_t token, const nk_msg_t *msg) {
    return (nk_status_t)NK_CALL3(NK_SC_IPC_REPLY, h, token, msg);
}

nk_status_t nk_cap_grant(uint32_t task_id, nk_handle_t h, uint32_t rights,
                         nk_handle_t *out) {
    return (nk_status_t)NK_CALL4(NK_SC_CAP_GRANT, task_id, h, rights, out);
}

nk_status_t nk_cap_revoke(uint32_t task_id, nk_handle_t h) {
    return (nk_status_t)NK_CALL2(NK_SC_CAP_REVOKE, task_id, h);
}

nk_status_t nk_mem_alloc(nk_mem_desc_t *out) {
    return (nk_status_t)NK_CALL1(NK_SC_MEM_ALLOC, out);
}

nk_status_t nk_mem_free(uint32_t block_id) {
    return (nk_status_t)NK_CALL1(NK_SC_MEM_FREE, block_id);
}

nk_status_t nk_mem_info(nk_mem_info_t *out) {
    return (nk_status_t)NK_CALL1(NK_SC_MEM_INFO, out);
}

nk_status_t nk_irq_bind(uint32_t source, nk_handle_t h) {
    return (nk_status_t)NK_CALL2(NK_SC_IRQ_BIND, source, h);
}

nk_status_t nk_irq_unbind(uint32_t source) {
    return (nk_status_t)NK_CALL1(NK_SC_IRQ_UNBIND, source);
}

nk_status_t nk_irq_raise(uint32_t source, uint32_t payload) {
    return (nk_status_t)NK_CALL2(NK_SC_IRQ_RAISE, source, payload);
}

nk_status_t nk_log(const char *text, uint32_t len) {
    return (nk_status_t)NK_CALL2(NK_SC_LOG, text, len);
}

nk_status_t nk_debug(uint32_t op, uint32_t arg) {
    return (nk_status_t)NK_CALL2(NK_SC_DEBUG, op, arg);
}

void nk_msg_init(nk_msg_t *msg, uint32_t tag) {
    msg->tag = tag;
    msg->token = 0;
    msg->length = 0;
    msg->reserved = 0;
    for (uint32_t i = 0; i < NK_IPC_PAYLOAD_MAX; i++) {
        msg->data[i] = 0;
    }
}

nk_status_t nk_msg_set_data(nk_msg_t *msg, const void *data, uint32_t len) {
    if (len > NK_IPC_PAYLOAD_MAX) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < len; i++) {
        msg->data[i] = ((const uint8_t *)data)[i];
    }
    msg->length = len;
    return NK_STATUS_OK;
}

const uint8_t *nk_msg_data(const nk_msg_t *msg, uint32_t *len_out) {
    if (len_out != 0) {
        *len_out = msg->length;
    }
    return msg->data;
}

size_t nk_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void *nk_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *nk_memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    return dst;
}

int nk_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

int nk_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *nk_utoa(uint32_t value, char *buf, size_t buf_len, uint32_t base) {
    char tmp[16];
    size_t n = 0;
    if (buf_len == 0) {
        return buf;
    }
    if (base == 0 || base > 16) {
        base = 10;
    }
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (value != 0 && n < sizeof(tmp)) {
        uint32_t d = value % base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        value /= base;
    }
    if (n >= buf_len) {
        n = buf_len - 1;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
    return buf;
}

void nk_logf(const char *fmt, ...) {
    va_list ap;
    char buf[NK_LOG_MAX];
    size_t len = 0;

    struct sink {
        char *buf;
        size_t *len;
    } s = { buf, &len };

#define NK_PUT(c)                                                             \
    do {                                                                      \
        if (*s.len + 1 < NK_LOG_MAX) {                                        \
            s.buf[(*s.len)++] = (char)(c);                                    \
        }                                                                     \
    } while (0)

#define NK_PUTS(str)                                                          \
    do {                                                                      \
        const char *nk_p = (str);                                             \
        while (*nk_p != '\0') {                                               \
            NK_PUT(*nk_p++);                                                  \
        }                                                                     \
    } while (0)

    va_start(ap, fmt);
    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            NK_PUT(*p);
            continue;
        }
        p++;
        if (*p == '\0') {
            break;
        }
        if (*p == '%') {
            NK_PUT('%');
            continue;
        }

        uint32_t width = 0;
        if (*p == '0') {
            width = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                width = width * 10u + (uint32_t)(*p - '0');
                p++;
            }
        }
        if (*p == '\0') {
            break;
        }

        char spec = *p;
        char num[16];
        int pad = 0;
        switch (spec) {
        case 's': {
            const char *str = va_arg(ap, const char *);
            NK_PUTS(str != 0 ? str : "(null)");
            break;
        }
        case 'u':
            nk_utoa(va_arg(ap, uint32_t), num, sizeof(num), 10);
            goto emit_num;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                NK_PUT('-');
                nk_utoa((uint32_t)(-v), num, sizeof(num), 10);
            } else {
                nk_utoa((uint32_t)v, num, sizeof(num), 10);
            }
            goto emit_num;
        }
        case 'x':
            nk_utoa(va_arg(ap, uint32_t), num, sizeof(num), 16);
            goto emit_num;
        case 'p': {
            uintptr_t ptr = (uintptr_t)va_arg(ap, void *);
            NK_PUTS("0x");
            nk_utoa((uint32_t)ptr, num, sizeof(num), 16);
            goto emit_num;
        }
        emit_num:
            for (pad = (int)width - (int)nk_strlen(num);
                 pad > 0 && width <= NK_LOG_MAX; pad--) {
                NK_PUT('0');
            }
            NK_PUTS(num);
            break;
        default:
            NK_PUT('%');
            NK_PUT(spec);
            break;
        }
    }
    va_end(ap);
    buf[len] = '\0';
    (void)nk_log(buf, (uint32_t)len);

#undef NK_PUT
#undef NK_PUTS
}
