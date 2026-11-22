/*
 * Stable hook surface for the optional Linux patch set.
 *
 * Phase 1 is entirely out-of-tree: the module creates /dev/nkabi3, owns the
 * ABI v3 control plane and exports this one function. A later patch can call
 * nk64_parasite_mode() from scheduler/VFS/exec sites to make Linux cooperate
 * with the reverse-monolithic layout without linking against module internals.
 */
#ifndef NULLK_LINUX_PARASITE_H
#define NULLK_LINUX_PARASITE_H

#ifdef __KERNEL__
#include <linux/types.h>
u32 nk64_parasite_mode(void);
#endif

#endif /* NULLK_LINUX_PARASITE_H */
