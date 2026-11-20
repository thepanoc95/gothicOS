#include "kern.h"

static const char *const runlevel_names[] = {
    "reset", "platform", "core", "memory", "ipc",
    "irq", "drivers", "tasks", "ready"
};

/* Local bounded length helper; libnk is not linked into privileged code. */
static uint32_t nkk_cstr_len(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0' && n < NK_LOG_MAX) {
        n++;
    }
    return n;
}

void nkk_set_runlevel(uint32_t level) {
    nkk_runlevel = (uint8_t)level;
    if (level <= NK_RUNLEVEL_READY) {
        nkk_console_write("[boot] ", 7);
        nkk_console_write(runlevel_names[level], nkk_cstr_len(runlevel_names[level]));
        nkk_console_write("\n", 1);
    }
}
