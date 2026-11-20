# NULLK ABI v2

NULLK ABI v2 is a baremetal syscall interface. It is **not POSIX** and does not
emulate POSIX. There are no file descriptors, paths, processes, signals,
sockets, or libc entry points. All task/kernel interaction is a synchronous
`svc` call from Thread mode.

## Calling convention (ARM Cortex-M)

| Item | Contract |
|---|---|
| Instruction | `svc #imm8`, where `imm8` is the syscall opcode |
| Arguments | `r0`, `r1`, `r2`, `r3` (`uint32_t` values) |
| Return | `r0` (`uint32_t` status or direct value) |
| Clobbered | `r0-r3`, `r12`, `lr` (per AAPCS); `r4-r11` preserved by kernel |
| Stack | User tasks run on PSP; kernel runs on MSP |
| Pointers | Only pointers to caller-owned structs/buffers are passed; the kernel copies them with bounded lengths |
| Rights | Kernel objects are referenced by capability handles, never raw pointers |

The ABI is identical on the Cortex-M3 QEMU target and the RP2040/Cortex-M0+
Pico W target. ARMv6-M has fewer encodable instructions, but the syscall,
register and stack contracts above do not change.

`libnk` provides typed wrappers; application code should not issue raw `svc`
instructions.

## Status codes

`NK_STATUS_OK` is 0. Errors are positive integers and never negative:

```
OK=0, INVALID_ARGUMENT=1, NOT_FOUND=2, WOULD_BLOCK=3, QUEUE_FULL=4,
UNSUPPORTED=5, INTERNAL_ERROR=6, PERMISSION_DENIED=7, ALREADY_EXISTS=8,
OUT_OF_MEMORY=9, BUSY=10, DEAD=11
```

## Syscalls

| # | Name | Arguments | Returns |
|---:|---|---|---|
| 0 | `NK_SC_GET_INFO` | `a0 = nk_info_t *out` | status |
| 1 | `NK_SC_TASK_SELF` | none | task id (direct) |
| 2 | `NK_SC_TASK_CREATE` | `a0 = const nk_task_create_info_t *info` | status, `a0` receives id via `info->out_id` |
| 3 | `NK_SC_TASK_EXIT` | `a0 = uint32_t code` | never returns |
| 4 | `NK_SC_TASK_STATE` | `a0 = uint32_t task_id`, `a1 = nk_task_info_t *out` | status |
| 5 | `NK_SC_YIELD` | none | status |
| 6 | `NK_SC_SLEEP` | `a0 = uint32_t ticks` | status |
| 7 | `NK_SC_TICKS` | none | tick count (direct) |
| 8 | `NK_SC_IPC_OPEN` | `a0 = uint32_t capacity`, `a1 = nk_handle_t *out` | status |
| 9 | `NK_SC_IPC_CLOSE` | `a0 = nk_handle_t h` | status |
| 10 | `NK_SC_IPC_SEND` | `a0 = nk_handle_t h`, `a1 = const nk_msg_t *msg` | status |
| 11 | `NK_SC_IPC_RECV` | `a0 = nk_handle_t h`, `a1 = nk_msg_t *out` | status |
| 12 | `NK_SC_IPC_CALL` | `a0 = nk_handle_t h`, `a1 = nk_msg_t *msg`, `a2 = nk_msg_t *reply` | status |
| 13 | `NK_SC_IPC_REPLY` | `a0 = nk_handle_t h`, `a1 = uint32_t token`, `a2 = const nk_msg_t *msg` | status |
| 14 | `NK_SC_CAP_GRANT` | `a0 = task_id`, `a1 = handle`, `a2 = rights`, `a3 = nk_handle_t *out` | status |
| 15 | `NK_SC_CAP_REVOKE` | `a0 = task_id`, `a1 = handle` | status |
| 16 | `NK_SC_MEM_ALLOC` | `a0 = nk_mem_desc_t *out` | status |
| 17 | `NK_SC_MEM_FREE` | `a0 = uint32_t block_id` | status |
| 18 | `NK_SC_MEM_INFO` | `a0 = nk_mem_info_t *out` | status |
| 19 | `NK_SC_IRQ_BIND` | `a0 = uint32_t source`, `a1 = nk_handle_t h` | status |
| 20 | `NK_SC_IRQ_UNBIND` | `a0 = uint32_t source` | status |
| 21 | `NK_SC_IRQ_RAISE` | `a0 = uint32_t source`, `a1 = uint32_t payload` | status |
| 22 | `NK_SC_LOG` | `a0 = const char *text`, `a1 = uint32_t len` | status |
| 23 | `NK_SC_DEBUG` | `a0 = uint32_t op`, `a1 = uint32_t arg` | status |

Blocking calls (`IPC_SEND` on a full queue, `IPC_RECV` on an empty queue,
`IPC_CALL`, `SLEEP`) put the caller in `NK_TASK_BLOCKED`. The scheduler never
busy-waits.

## Message format

Messages are fixed size so the kernel never allocates while copying them.

```c
typedef struct {
    uint32_t tag;       /* application-defined, except NK_MSG_IRQ for IRQ events */
    uint32_t token;     /* used by CALL/REPLY */
    uint32_t length;    /* valid bytes in data, <= 48 */
    uint32_t reserved;  /* must be 0 */
    uint8_t data[48];
} nk_msg_t;             /* sizeof == 64 */
```

## Capabilities

A handle is a 16-bit index into the caller's capability table. Handle 0 is
invalid. Boot grants every task handle 1 to the kernel endpoint with
`NK_RIGHT_READ | NK_RIGHT_WRITE`. Rights are `READ=1`, `WRITE=2`, `GRANT=4`,
`MANAGE=8`. Granting copies a handle into another task's table; revoking
clears it. There is no ambient authority: `NK_SC_CAP_GRANT` requires the
`GRANT` right on the source handle.

## Memory

`NK_SC_MEM_ALLOC` returns a block id plus a pointer into a fixed 64-byte block
pool. This Cortex-M image has no MPU setup, so memory is **not isolated**; the
handle/rights model is the API contract that later MPU-enabled ports can
enforce. Blocks are zeroed on free.

## Runlevels

`nk_info_t.runlevel` reports the last completed boot stage:

```
1 PLATFORM, 2 CORE, 3 MEMORY, 4 IPC, 5 IRQ, 6 DRIVERS, 7 TASKS, 8 READY
```
