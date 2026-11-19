/* Confined handoff state machine for NullKern's Linux port. See header for the safety
 * model: opt-in only, CPUs 1+ only, no host process termination. */
#include "gothicos/nk_handoff.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

static void nk_copy(char *destination, size_t capacity, const char *source)
{
    size_t index = 0;
    if (destination == NULL || capacity == 0) {
        return;
    }
    if (source != NULL) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

static void nk_copy_n(char *destination, size_t capacity, const char *source, size_t length)
{
    size_t index = 0;
    if (destination == NULL || capacity == 0) {
        return;
    }
    while (index + 1 < capacity && index < length && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int nk_option_is(const char *start, size_t length, const char *prefix,
                        const char **value, size_t *value_length)
{
    size_t prefix_length = 0;
    while (prefix[prefix_length] != '\0') {
        prefix_length++;
    }
    if (length < prefix_length) {
        return 0;
    }
    for (size_t index = 0; index < prefix_length; index++) {
        if (start[index] != prefix[index]) {
            return 0;
        }
    }
    *value = start + prefix_length;
    *value_length = length - prefix_length;
    return 1;
}

static int nk_hex_digit(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int nk_parse_flags(const char *text, int *value)
{
    unsigned parsed = 0;
    int base = 10;
    const char *digits = text;
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        digits = text + 2;
        if (*digits == '\0') {
            return 0;
        }
    }
    while (*digits != '\0') {
        int digit = base == 16 ? nk_hex_digit(*digits)
                               : (*digits >= '0' && *digits <= '9' ? *digits - '0' : -1);
        if (digit < 0 || digit >= base) {
            return 0;
        }
        parsed = parsed * (unsigned)base + (unsigned)digit;
        digits++;
    }
    *value = (int)parsed;
    return 1;
}

static int nk_parse_memory(const char *text, unsigned *kilobytes)
{
    unsigned value = 0;
    size_t index = 0;
    while (text != NULL && text[index] >= '0' && text[index] <= '9') {
        value = value * 10U + (unsigned)(text[index] - '0');
        index++;
    }
    if (text == NULL || index == 0) {
        return 0;
    }
    if (text[index] == 'M' || text[index] == 'm') {
        value *= 1024U;
        index++;
    } else if (text[index] == 'K' || text[index] == 'k') {
        index++;
    } else if (text[index] != '\0') {
        return 0;
    }
    if (text[index] != '\0') {
        return 0;
    }
    *kilobytes = value;
    return 1;
}

static void nk_append_relative(char *destination, size_t capacity, const char *root,
                               const char *relative)
{
    size_t at = 0;
    nk_copy(destination, capacity, root);
    while (at < capacity && destination[at] != '\0') {
        at++;
    }
    if (at > 0 && at + 1 < capacity && destination[at - 1] != '/') {
        destination[at++] = '/';
        destination[at] = '\0';
    }
    nk_copy(destination + at, capacity - at, relative);
}

int nk_handoff_parse_options(nk_handoff_config_t *config, const char *options)
{
    const char *cursor = options;
    if (config == NULL || options == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    while (*cursor != '\0') {
        const char *start;
        const char *value = NULL;
        size_t value_length = 0;
        size_t length;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        length = (size_t)(cursor - start);
        if (length == 2 && start[0] == '-' && start[1] == 'v') {
            config->verbose = 1;
        } else if (nk_option_is(start, length, "-debug=", &value, &value_length)) {
            char buffer[32];
            nk_copy_n(buffer, sizeof(buffer), value, value_length);
            if (!nk_parse_flags(buffer, &config->debug_flags)) {
                return GOTHICOS_STATUS_INVALID_ARGUMENT;
            }
        } else if (nk_option_is(start, length, "-root=", &value, &value_length)) {
            nk_copy_n(config->root, sizeof(config->root), value, value_length);
            nk_append_relative(config->init_path, sizeof(config->init_path),
                               config->root, "system/init.d.elf");
            nk_append_relative(config->shell_path, sizeof(config->shell_path),
                               config->root, "user/bin/sh.uts");
        } else if (nk_option_is(start, length, "-mem=", &value, &value_length)) {
            char buffer[32];
            nk_copy_n(buffer, sizeof(buffer), value, value_length);
            if (!nk_parse_memory(buffer, &config->memory_limit_kb)) {
                return GOTHICOS_STATUS_INVALID_ARGUMENT;
            }
        } else {
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
    }
    return GOTHICOS_STATUS_OK;
}

int nk_handoff_configure(nk_handoff_t *handoff, const nk_handoff_config_t *config,
                         const nk_handoff_hooks_t *hooks)
{
    if (handoff == NULL || config == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    handoff->config = *config;
    if (handoff->config.init_path[0] == '\0') {
        nk_copy(handoff->config.init_path, sizeof(handoff->config.init_path),
                NK_DEFAULT_INIT_PATH);
    }
    if (handoff->config.shell_path[0] == '\0') {
        nk_copy(handoff->config.shell_path, sizeof(handoff->config.shell_path),
                NK_DEFAULT_SHELL_PATH);
    }
    if (hooks != NULL) {
        handoff->hooks = *hooks;
    } else {
        nk_handoff_hooks_t empty;
        size_t index;
        for (index = 0; index < sizeof(empty); index++) {
            ((unsigned char *)&empty)[index] = 0;
        }
        handoff->hooks = empty;
    }
    handoff->state = handoff->config.enabled ? NK_HANDOFF_READY : NK_HANDOFF_DISABLED;
    handoff->bound_cpu_count = 0;
    handoff->started_services = 0;
    handoff->init_image = NULL;
    handoff->init_image_size = 0;
    handoff->error[0] = '\0';
    return GOTHICOS_STATUS_OK;
}

static int nk_handoff_fail(nk_handoff_t *handoff, const char *message)
{
    handoff->state = NK_HANDOFF_FAILED;
    nk_copy(handoff->error, sizeof(handoff->error), message);
    if (handoff->hooks.log != NULL) {
        handoff->hooks.log(handoff->hooks.context, message);
    }
    return GOTHICOS_STATUS_INTERNAL_ERROR;
}

int nk_handoff_start(nk_handoff_t *handoff)
{
    unsigned cpu;
    if (handoff == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (!handoff->config.enabled) {
        return nk_handoff_fail(handoff, "handoff is disabled (set handoff=1 explicitly)");
    }
    if (handoff->state == NK_HANDOFF_FAILED) {
        return GOTHICOS_STATUS_INTERNAL_ERROR;
    }
    if (handoff->state >= NK_HANDOFF_SERVICES_STARTED) {
        return GOTHICOS_STATUS_OK;
    }
    if (handoff->state == NK_HANDOFF_READY) {
        handoff->state = NK_HANDOFF_BINDING_CORES;
        if (handoff->hooks.bind_cpu == NULL) {
            return nk_handoff_fail(handoff, "no bind_cpu hook installed");
        }
    }
    if (handoff->state == NK_HANDOFF_BINDING_CORES) {
        /* CPU 0 is never handed over. Guard against a misconfigured range. */
        if (handoff->config.first_managed_cpu == 0) {
            return nk_handoff_fail(handoff, "CPU 0 is reserved for Linux");
        }
        for (cpu = handoff->config.first_managed_cpu;
             cpu < handoff->config.first_managed_cpu + handoff->config.managed_cpu_count;
             cpu++) {
            if (handoff->hooks.bind_cpu(handoff->hooks.context, cpu) != 0) {
                return nk_handoff_fail(handoff, "unable to bind a managed CPU");
            }
            handoff->bound_cpu_count++;
        }
        handoff->state = NK_HANDOFF_CORE_ONLINE;
    }
    if (handoff->state == NK_HANDOFF_CORE_ONLINE) {
        if (handoff->hooks.load_elf == NULL || handoff->hooks.start_init == NULL) {
            return nk_handoff_fail(handoff, "missing ELF/init hooks");
        }
        if (handoff->hooks.load_elf(handoff->hooks.context, handoff->config.init_path,
                                    &handoff->init_image, &handoff->init_image_size) != 0) {
            return nk_handoff_fail(handoff, "unable to load init ELF");
        }
        handoff->state = NK_HANDOFF_ELF_LOADED;
    }
    if (handoff->state == NK_HANDOFF_ELF_LOADED) {
        if (handoff->hooks.start_init(handoff->hooks.context, handoff->init_image,
                                      handoff->init_image_size) != 0) {
            return nk_handoff_fail(handoff, "unable to start init");
        }
        handoff->state = NK_HANDOFF_INIT_STARTED;
    }
    if (handoff->state == NK_HANDOFF_INIT_STARTED) {
        if (handoff->hooks.start_service != NULL &&
            handoff->config.shell_path[0] != '\0') {
            if (handoff->hooks.start_service(handoff->hooks.context,
                                             handoff->config.shell_path) != 0) {
                return nk_handoff_fail(handoff, "unable to start shell service");
            }
            handoff->started_services = 1;
        }
        handoff->state = NK_HANDOFF_SERVICES_STARTED;
    }
    return GOTHICOS_STATUS_OK;
}

int nk_handoff_stop(nk_handoff_t *handoff)
{
    if (handoff == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    handoff->init_image = NULL;
    handoff->init_image_size = 0;
    handoff->bound_cpu_count = 0;
    handoff->started_services = 0;
    handoff->state = handoff->config.enabled ? NK_HANDOFF_READY : NK_HANDOFF_DISABLED;
    return GOTHICOS_STATUS_OK;
}

const char *nk_handoff_state_name(nk_handoff_state_t state)
{
    switch (state) {
    case NK_HANDOFF_DISABLED: return "disabled";
    case NK_HANDOFF_READY: return "ready";
    case NK_HANDOFF_BINDING_CORES: return "binding-cores";
    case NK_HANDOFF_CORE_ONLINE: return "core-online";
    case NK_HANDOFF_ELF_LOADED: return "elf-loaded";
    case NK_HANDOFF_INIT_STARTED: return "init-started";
    case NK_HANDOFF_SERVICES_STARTED: return "services-started";
    case NK_HANDOFF_FAILED: return "failed";
    default: return "unknown";
    }
}
