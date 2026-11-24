/*
 * null_kernel printk - freestanding formatted kernel logging.
 *
 * No libc, no FILE*, no heap. Output is routed through a sink installed by
 * the platform layer so the same code works on Cortex-M, AMD64 and the host
 * test harness.
 */
#ifndef NULLK_PRINTK_H
#define NULLK_PRINTK_H

#include <stdarg.h>
#include <stdint.h>

/* Severity values ordered from most to least urgent. */
enum {
    NK_KERN_EMERG = 0,
    NK_KERN_ALERT = 1,
    NK_KERN_CRIT = 2,
    NK_KERN_ERR = 3,
    NK_KERN_WARN = 4,
    NK_KERN_NOTICE = 5,
    NK_KERN_INFO = 6,
    NK_KERN_DEBUG = 7,
    NK_KERN_LEVEL_COUNT = 8
};

/* Receives already-formatted text, including the trailing newline. */
typedef void (*nk_print_sink_t)(const char *text, uint32_t len);

void nk_printk_init(nk_print_sink_t sink);
nk_print_sink_t nk_printk_sink(void);
void nk_printk_set_level(int level);
int nk_printk_level(void);

/* printk() returns the number of bytes emitted (excluding the NUL). */
int nk_printk(int level, const char *fmt, ...);

/* Bounded formatters; always NUL-terminate when cap > 0. */
int nk_vsnprintf(char *buf, uint32_t cap, const char *fmt, va_list ap);
int nk_snprintf(char *buf, uint32_t cap, const char *fmt, ...);

/* Convenience macros used throughout the AMD64 port. */
#define pr_emerg(...) nk_printk(NK_KERN_EMERG, __VA_ARGS__)
#define pr_alert(...) nk_printk(NK_KERN_ALERT, __VA_ARGS__)
#define pr_crit(...)  nk_printk(NK_KERN_CRIT, __VA_ARGS__)
#define pr_err(...)   nk_printk(NK_KERN_ERR, __VA_ARGS__)
#define pr_warn(...)  nk_printk(NK_KERN_WARN, __VA_ARGS__)
#define pr_notice(...) nk_printk(NK_KERN_NOTICE, __VA_ARGS__)
#define pr_info(...)  nk_printk(NK_KERN_INFO, __VA_ARGS__)
#define pr_debug(...) nk_printk(NK_KERN_DEBUG, __VA_ARGS__)

#endif /* NULLK_PRINTK_H */
