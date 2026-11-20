#ifndef NULLK_ARCH_IF_H
#define NULLK_ARCH_IF_H

#include <stdint.h>

/* Implemented once per architecture/board backend. */
void nkk_arch_early_init(void);
void nkk_arch_tick_init(uint32_t hz);
void nkk_arch_enable_irq(void);
void nkk_arch_disable_irq(void);
void nkk_arch_yield(void);
void nkk_arch_puts(const char *text, uint32_t len);
void nkk_arch_idle(void);
void nkk_arch_start_first(void) __attribute__((noreturn));

#endif /* NULLK_ARCH_IF_H */
