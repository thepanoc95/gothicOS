/*
 * null_kernel printk - freestanding formatted kernel logging.
 *
 * This file is the architecture-independent formatter/logger shared by the
 * ARM/Cortex-M, AMD64 and hosted test builds. It uses no libc: formatting is
 * done into a caller supplied buffer and output goes through a sink installed
 * by the platform layer.
 */
#include "nullk/printk.h"

#include <stddef.h>

/* Kernel log lines are always emitted as a single bounded write. */
#define NK_PRINTK_LINE_MAX 512u

/* Formatter flags (internal). */
enum {
    NK_PF_ZERO  = 1u << 0,
    NK_PF_LEFT  = 1u << 1,
    NK_PF_PLUS  = 1u << 2,
    NK_PF_SPACE = 1u << 3
};

typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
} nk_pf_out_t;

/* Default sink: deliberately does nothing until the platform installs one. */
static void nk_printk_null_sink(const char *text, uint32_t len)
{
    (void)text;
    (void)len;
}

static nk_print_sink_t g_sink = nk_printk_null_sink;
static int g_level = NK_KERN_INFO;

/* Boot and service lines share one null_kernel owner prefix. */
static const char *nk_printk_prefix(int severity)
{
    switch (severity) {
    case NK_KERN_EMERG:
        return "null_kernel: panic: ";
    case NK_KERN_ALERT:
        return "null_kernel: alert: ";
    case NK_KERN_CRIT:
        return "null_kernel: fatal: ";
    case NK_KERN_ERR:
        return "null_kernel: error: ";
    case NK_KERN_WARN:
        return "null_kernel: warning: ";
    case NK_KERN_NOTICE:
        return "null_kernel: notice: ";
    default:
        return "null_kernel: ";
    }
}

void nk_printk_init(nk_print_sink_t sink)
{
    g_sink = (sink != (nk_print_sink_t)0) ? sink : nk_printk_null_sink;
}

nk_print_sink_t nk_printk_sink(void)
{
    return g_sink;
}

void nk_printk_set_level(int level)
{
    if (level < NK_KERN_EMERG) {
        level = NK_KERN_EMERG;
    }
    if (level > NK_KERN_DEBUG) {
        level = NK_KERN_DEBUG;
    }
    g_level = level;
}

int nk_printk_level(void)
{
    return g_level;
}

/* Write one character, counting the full (untruncated) length. */
static void nk_pf_putc(nk_pf_out_t *o, char c)
{
    if ((o->len + 1u) < o->cap) {
        o->buf[o->len] = c;
    }
    o->len++;
}

static void nk_pf_pad(nk_pf_out_t *o, char c, uint32_t count)
{
    uint32_t i;

    for (i = 0u; i < count; i++) {
        nk_pf_putc(o, c);
    }
}

static uint32_t nk_pf_strlen(const char *s)
{
    uint32_t n = 0u;

    while ((s != (const char *)0) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

/* Emit an unsigned integer with an optional literal prefix ("-" / "0x").
 * Zero padding is inserted after the prefix, matching "%08x" / "%#010x"
 * behaviour; space padding follows the standard right/left alignment. */
static void nk_pf_uint(nk_pf_out_t *o, uint64_t value, uint32_t base,
                       int upper, uint32_t width, uint32_t flags,
                       const char *prefix)
{
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    uint32_t ndigits = 0u;
    uint32_t prefix_len = nk_pf_strlen(prefix);
    uint32_t total;
    uint32_t pad;

    if (base == 0u) {
        base = 10u;
    }
    do {
        tmp[ndigits++] = digits[(uint32_t)(value % (uint64_t)base)];
        value /= (uint64_t)base;
    } while ((value != 0u) && (ndigits < (uint32_t)sizeof(tmp)));

    total = ndigits + prefix_len;
    pad = (width > total) ? (width - total) : 0u;

    if ((flags & NK_PF_LEFT) != 0u) {
        if (prefix_len != 0u) {
            uint32_t i;
            for (i = 0u; i < prefix_len; i++) {
                nk_pf_putc(o, prefix[i]);
            }
        }
        while (ndigits > 0u) {
            nk_pf_putc(o, tmp[--ndigits]);
        }
        nk_pf_pad(o, ' ', pad);
        return;
    }

    if ((flags & NK_PF_ZERO) != 0u) {
        if (prefix_len != 0u) {
            uint32_t i;
            for (i = 0u; i < prefix_len; i++) {
                nk_pf_putc(o, prefix[i]);
            }
        }
        nk_pf_pad(o, '0', pad);
    } else {
        nk_pf_pad(o, ' ', pad);
        if (prefix_len != 0u) {
            uint32_t i;
            for (i = 0u; i < prefix_len; i++) {
                nk_pf_putc(o, prefix[i]);
            }
        }
    }
    while (ndigits > 0u) {
        nk_pf_putc(o, tmp[--ndigits]);
    }
}

int nk_vsnprintf(char *buf, uint32_t cap, const char *fmt, va_list ap)
{
    nk_pf_out_t out;
    const char *p;

    out.buf = buf;
    out.cap = cap;
    out.len = 0u;

    if (cap > 0u) {
        buf[0] = '\0';
    }
    if (fmt == (const char *)0) {
        return 0;
    }

    p = fmt;
    while (*p != '\0') {
        const char *spec_start;
        uint32_t flags = 0u;
        uint32_t width = 0u;
        int length = 0; /* 0 = int, 1 = long, 2 = long long, <0 = short */
        char spec;

        if (*p != '%') {
            nk_pf_putc(&out, *p++);
            continue;
        }

        spec_start = p;
        p++;
        if (*p == '\0') {
            nk_pf_putc(&out, '%');
            break;
        }
        if (*p == '%') {
            nk_pf_putc(&out, '%');
            p++;
            continue;
        }

        /* Flags. '#' is accepted but has no effect on the supported forms. */
        for (;;) {
            if (*p == '0') {
                flags |= NK_PF_ZERO;
                p++;
            } else if (*p == '-') {
                flags |= NK_PF_LEFT;
                p++;
            } else if (*p == '+') {
                flags |= NK_PF_PLUS;
                p++;
            } else if (*p == ' ') {
                flags |= NK_PF_SPACE;
                p++;
            } else if (*p == '#') {
                p++;
            } else {
                break;
            }
        }

        /* Minimum field width (clamped so a malformed format cannot loop). */
        while ((*p >= '0') && (*p <= '9')) {
            uint32_t digit = (uint32_t)(*p - '0');

            if (width < 4096u) {
                width = (width * 10u) + digit;
            }
            p++;
        }
        if (width > 4096u) {
            width = 4096u;
        }

        /* Precision is parsed and ignored: this formatter only implements
         * minimum widths, which is all the kernel logging path needs. */
        if (*p == '.') {
            p++;
            while ((*p >= '0') && (*p <= '9')) {
                p++;
            }
        }

        for (;;) {
            if (*p == 'l') {
                length++;
                p++;
            } else if (*p == 'h') {
                length--;
                p++;
            } else {
                break;
            }
        }
        if ((*p == 'z') || (*p == 'j') || (*p == 't')) {
            length = 2;
            p++;
        }

        spec = *p;
        switch (spec) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            uint32_t slen;
            uint32_t pad;

            if (s == (const char *)0) {
                s = "(null)";
            }
            slen = nk_pf_strlen(s);
            pad = (width > slen) ? (width - slen) : 0u;
            if ((flags & NK_PF_LEFT) == 0u) {
                nk_pf_pad(&out, ' ', pad);
            }
            while (*s != '\0') {
                nk_pf_putc(&out, *s++);
            }
            if ((flags & NK_PF_LEFT) != 0u) {
                nk_pf_pad(&out, ' ', pad);
            }
            p++;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            uint32_t pad = (width > 1u) ? (width - 1u) : 0u;

            if ((flags & NK_PF_LEFT) == 0u) {
                nk_pf_pad(&out, ' ', pad);
            }
            nk_pf_putc(&out, c);
            if ((flags & NK_PF_LEFT) != 0u) {
                nk_pf_pad(&out, ' ', pad);
            }
            p++;
            break;
        }
        case 'd':
        case 'i': {
            int64_t value;
            uint64_t magnitude;
            const char *prefix = "";

            if (length >= 2) {
                value = (int64_t)va_arg(ap, long long);
            } else if (length == 1) {
                value = (int64_t)va_arg(ap, long);
            } else {
                value = (int64_t)va_arg(ap, int);
            }
            if (value < 0) {
                /* Negate without overflowing on INT64_MIN. */
                magnitude = (uint64_t)(-(value + 1)) + 1u;
                prefix = "-";
            } else {
                magnitude = (uint64_t)value;
                if ((flags & NK_PF_PLUS) != 0u) {
                    prefix = "+";
                } else if ((flags & NK_PF_SPACE) != 0u) {
                    prefix = " ";
                }
            }
            nk_pf_uint(&out, magnitude, 10u, 0, width, flags, prefix);
            p++;
            break;
        }
        case 'u': {
            uint64_t value;

            if (length >= 2) {
                value = (uint64_t)va_arg(ap, unsigned long long);
            } else if (length == 1) {
                value = (uint64_t)va_arg(ap, unsigned long);
            } else {
                value = (uint64_t)va_arg(ap, unsigned int);
            }
            nk_pf_uint(&out, value, 10u, 0, width, flags, "");
            p++;
            break;
        }
        case 'x':
        case 'X': {
            uint64_t value;

            if (length >= 2) {
                value = (uint64_t)va_arg(ap, unsigned long long);
            } else if (length == 1) {
                value = (uint64_t)va_arg(ap, unsigned long);
            } else {
                value = (uint64_t)va_arg(ap, unsigned int);
            }
            nk_pf_uint(&out, value, 16u, (spec == 'X'), width, flags, "");
            p++;
            break;
        }
        case 'o': {
            uint64_t value;

            if (length >= 2) {
                value = (uint64_t)va_arg(ap, unsigned long long);
            } else if (length == 1) {
                value = (uint64_t)va_arg(ap, unsigned long);
            } else {
                value = (uint64_t)va_arg(ap, unsigned int);
            }
            nk_pf_uint(&out, value, 8u, 0, width, flags, "");
            p++;
            break;
        }
        case 'p': {
            uintptr_t value = (uintptr_t)va_arg(ap, void *);
            uint32_t digit_width = (width > 2u) ? (width - 2u) : 0u;

            nk_pf_uint(&out, (uint64_t)value, 16u, 0, digit_width, flags,
                       "0x");
            p++;
            break;
        }
        default:
            /* Unknown conversion: re-emit the whole specifier verbatim. */
            {
                const char *q = spec_start;

                while ((q <= p) && (*q != '\0')) {
                    nk_pf_putc(&out, *q++);
                }
                if (*p != '\0') {
                    p++;
                }
            }
            break;
        }
    }

    if (cap > 0u) {
        uint32_t end = out.len;

        if (end >= cap) {
            end = cap - 1u;
        }
        buf[end] = '\0';
    }
    return (int)out.len;
}

int nk_snprintf(char *buf, uint32_t cap, const char *fmt, ...)
{
    va_list ap;
    int result;

    va_start(ap, fmt);
    result = nk_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return result;
}

int nk_printk(int level, const char *fmt, ...)
{
    char line[NK_PRINTK_LINE_MAX];
    const char *prefix;
    uint32_t pos = 0u;
    int body_len;
    int severity = level;
    va_list ap;

    /* Smaller numbers are more urgent. */
    if (level > g_level) {
        return 0;
    }
    if (severity < NK_KERN_EMERG) {
        severity = NK_KERN_EMERG;
    }
    if (severity > NK_KERN_DEBUG) {
        severity = NK_KERN_DEBUG;
    }

    prefix = nk_printk_prefix(severity);
    while (*prefix != '\0' && pos < (NK_PRINTK_LINE_MAX - 2u)) {
        line[pos++] = *prefix++;
    }

    va_start(ap, fmt);
    body_len = nk_vsnprintf(line + pos, NK_PRINTK_LINE_MAX - pos, fmt, ap);
    va_end(ap);

    if (body_len < 0) {
        body_len = 0;
    }
    pos += (uint32_t)body_len;
    if (pos > (NK_PRINTK_LINE_MAX - 2u)) {
        pos = NK_PRINTK_LINE_MAX - 2u;
    }
    /* Callers may already end a line with '\n'; never emit a blank line. */
    if (pos == 0u || line[pos - 1u] != '\n') {
        line[pos++] = '\n';
    }
    line[pos] = '\0';

    g_sink(line, pos);
    return (int)pos;
}
