#include "../nk/arch_if.h"

static uint32_t yield_count;
static char output[4096];
static uint32_t output_len;

void nkk_host_reset(void) {
    yield_count = 0;
    output_len = 0;
    output[0] = '\0';
}

uint32_t nkk_host_yields(void) {
    return yield_count;
}

const char *nkk_host_output(void) {
    return output;
}

void nkk_arch_early_init(void) {
}

void nkk_arch_tick_init(uint32_t hz) {
    (void)hz;
}

void nkk_arch_enable_irq(void) {
}

void nkk_arch_disable_irq(void) {
}

void nkk_arch_yield(void) {
    yield_count++;
}

void nkk_arch_puts(const char *text, uint32_t len) {
    for (uint32_t i = 0; i < len && output_len + 1 < sizeof(output); i++) {
        output[output_len++] = text[i];
    }
    output[output_len] = '\0';
}

void nkk_arch_idle(void) {
}

void nkk_arch_start_first(void) {
    /* Host tests never run the real scheduler loop. */
    for (;;) {
        nkk_arch_idle();
    }
}
