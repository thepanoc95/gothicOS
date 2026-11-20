#ifndef NULLK_KCONFIG_H
#define NULLK_KCONFIG_H

#include "../include/nullk/abi.h"

/* Fixed kernel configuration. Everything is statically allocated. */
#define NKK_TICK_HZ             100u
#define NKK_STACK_WORDS         256u
#define NKK_LOG_BUF             128u
#define NKK_WAIT_NONE           0u
#define NKK_WAIT_RECV           1u
#define NKK_WAIT_SEND           2u
#define NKK_WAIT_REPLY          3u
#define NKK_WAIT_SLEEP          4u

#ifndef NK_MAX_TASKS
#define NK_MAX_TASKS 16u
#endif

#endif /* NULLK_KCONFIG_H */
