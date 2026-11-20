#include "../kern.h"
#include "arch_if.h"

void nkk_console_write(const char *text, uint32_t len) {
    if (text != 0 && len != 0) {
        nkk_arch_puts(text, len);
    }
}

const char *nkk_status_name(uint32_t status) {
    static const char *const names[] = {
        "OK", "INVALID_ARGUMENT", "NOT_FOUND", "WOULD_BLOCK",
        "QUEUE_FULL", "UNSUPPORTED", "INTERNAL_ERROR",
        "PERMISSION_DENIED", "ALREADY_EXISTS", "OUT_OF_MEMORY",
        "BUSY", "DEAD"
    };
    if (status >= sizeof(names) / sizeof(names[0])) {
        return "UNKNOWN";
    }
    return names[status];
}
