/* NullKernRT Linux handoff state machine (platform neutral).
 *
 * SAFETY MODEL
 * ------------
 * This file describes a *confined, opt-in* handoff. It never enumerates or terminates
 * host processes. The Linux kernel keeps CPU 0; NullKern is only ever bound to CPUs at
 * or above `first_managed_cpu` (default 1). Nothing happens unless `enabled` is set by
 * an operator, e.g. via the `handoff=1` module parameter documented in docs.
 *
 * Platform code (the LKM, a test harness, or MicroPython) supplies hooks for the actual
 * operations; this state machine only orders and validates them.
 */
#ifndef GOTHICOS_NK_HANDOFF_H
#define GOTHICOS_NK_HANDOFF_H

#include <stddef.h>

#include "gothicos/kernel_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NK_HANDOFF_PATH_MAX 160
#define NK_HANDOFF_MAX_SERVICES 8

typedef enum {
    NK_HANDOFF_DISABLED = 0,
    NK_HANDOFF_READY,
    NK_HANDOFF_BINDING_CORES,
    NK_HANDOFF_CORE_ONLINE,
    NK_HANDOFF_ELF_LOADED,
    NK_HANDOFF_INIT_STARTED,
    NK_HANDOFF_SERVICES_STARTED,
    NK_HANDOFF_FAILED
} nk_handoff_state_t;

typedef struct {
    int enabled;                       /* must be explicitly set by the operator */
    unsigned first_managed_cpu;        /* CPU 0 is reserved for Linux */
    unsigned managed_cpu_count;        /* number of CPUs handed to NullKern */
    char init_path[NK_HANDOFF_PATH_MAX];
    char shell_path[NK_HANDOFF_PATH_MAX];
    unsigned memory_limit_kb;
    int debug_flags;
    int verbose;
    char root[NK_HANDOFF_PATH_MAX];
} nk_handoff_config_t;

typedef struct {
    int (*bind_cpu)(void *context, unsigned cpu);
    int (*load_elf)(void *context, const char *path, void **image, unsigned *size);
    int (*start_init)(void *context, void *image, unsigned size);
    int (*start_service)(void *context, const char *path);
    void (*log)(void *context, const char *message);
    void *context;
} nk_handoff_hooks_t;

typedef struct {
    nk_handoff_state_t state;
    nk_handoff_config_t config;
    nk_handoff_hooks_t hooks;
    unsigned bound_cpu_count;
    unsigned started_services;
    void *init_image;
    unsigned init_image_size;
    char error[NK_HANDOFF_PATH_MAX];
} nk_handoff_t;

/* Parse a boot option string such as:
 *   "-v -debug=0x01 -root=@flash/data -mem=200K"
 * Returns GOTHICOS_STATUS_OK or GOTHICOS_STATUS_INVALID_ARGUMENT. */
int nk_handoff_parse_options(nk_handoff_config_t *config, const char *options);

/* Install configuration and hooks, resetting the machine to READY (or DISABLED). */
int nk_handoff_configure(nk_handoff_t *handoff, const nk_handoff_config_t *config,
                         const nk_handoff_hooks_t *hooks);

/* Advance one step of the handoff. Safe to call repeatedly; completed states are no-ops. */
int nk_handoff_start(nk_handoff_t *handoff);

/* Stop owned resources. Does not terminate unrelated processes. */
int nk_handoff_stop(nk_handoff_t *handoff);

const char *nk_handoff_state_name(nk_handoff_state_t state);

/* Default paths used by both the Linux port and the MicroPython chainloader. */
#define NK_DEFAULT_INIT_PATH "@flash/data/system/init.d.elf"
#define NK_DEFAULT_SHELL_PATH "@flash/data/user/bin/sh.uts"

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_NK_HANDOFF_H */
