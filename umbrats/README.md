# UmbraRT C++ runtime (initial implementation)

Runtime-only C++ implementation for gothicOS. It does **not** contain a lexer,
parser, source compiler, linker, or `.uts` frontend.

## Compatibility

- Umbra Bytecode container: `UMB1`, version `0.1`.
- Bytecode ABI field: `1.1`, matching `docs/UmbraTS.md`.
- Shared stack VM and package verifier.
- NULLK ABI v2: includes `nullk/libnk.h` and uses `nk_log`, `nk_yield`,
  `nk_get_info`, `nk_mem_alloc`, `nk_mem_free`, and `nk_irq_raise`.
- AMD64 ABI v3: uses `nk64_syscall` from `nullk/abi64.h` for log, yield,
  and get-info. The AMD64 ABI's IPC/message contract differs from ABI v2,
  so IPC-backed native calls are intentionally reported as unsupported.

## Build

Compile with a freestanding-capable C++ compiler and the gothicOS include path:

```sh
# ABI v2 target
g++ -std=gnu++17 -ffreestanding -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -DUMBRA_NULLK_ABI_V2 \
    -I/path/to/gothicOS/nullk/include \
    -c umbrart.cpp -o umbrart.o

# AMD64 ABI v3 target
g++ -std=gnu++17 -ffreestanding -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -DUMBRA_NULLK_AMD64 \
    -I/path/to/gothicOS/nullk/include \
    -c umbrart.cpp -o umbrart.o
```

For ABI v2, link against the existing `libnk` implementation. For AMD64, link
against `libnk64` (which provides `nk64_syscall`).

The caller supplies a pointer to the complete `.umb` package:
```cpp
static umbra::Runtime runtime;
umbra::Result r = runtime.load(package_bytes, package_size);
if (r.status == umbra::Status::Ok) {
    while (runtime.state() == umbra::TaskState::Ready)
        runtime.step(128);
}
```

The package buffer must remain valid for the runtime's lifetime. This initial
runtime uses fixed internal storage and does not allocate from the C++ heap.

## Current runtime limits

- 64 functions, 256 constants, 64 operand-stack slots, 16 call frames.
- 8 arrays of up to 256 elements each.
- String constants are zero-copy views into the package image.
- String concatenation is not implemented because the runtime has no string
  allocator/pool yet.
- Native calls implemented: `System.log`, `System.yield`, `System.info`.
- ABI v2 additionally implements `Memory.alloc`, `Memory.free`, and
  `Interrupt.timer` at a basic level.
- Port send/receive and task stop are declared in the bytecode spec but are
  not yet wired to a stable cross-ABI endpoint/task contract.
- The runtime checks that privileged native calls have matching declared
  `@Capability` metadata. A declaration is **not a grant**: the gothicOS
  loader/task manager must independently enforce actual kernel rights.

## Important integration notes

1. `docs/UmbraTS.md` describes kernel ABI `1.1`, while the currently linked
   NULLK headers expose ABI v2 and AMD64 ABI v3. These are distinct version
   namespaces; do not compare them as if they were the same integer.
2. The existing Umbra verifier performs deeper control-flow and stack-depth
   validation than this first C++ verifier. Before treating untrusted packages
   as production-safe, port those control-flow/stack-merge checks from
   `internal/nullkern/binary/verifier.py`.
3. `Memory.alloc` currently uses the v2 fixed-block allocator; its two bytecode
   arguments are accepted for ABI compatibility but the current `nk_mem_alloc`
   API allocates one fixed-size block.
4. This implementation is a runtime foundation, not yet a complete semantic
   implementation of every feature listed in the UmbraTS language document.

## OS TypeScript modules (`os_modules`)

`os_modules.hpp/.cpp` provide the native module manifest used by the UmbraTS
compiler/linker. `os_modules/gothic.d.ts` contains the corresponding SDK
surface declarations. Imports are statically linked; there is no runtime
source loader or dynamic import mechanism.

| Import | Export surface | Implementation path |
|---|---|---|
| `@gothic/core` | `VERSION` | Compiler-provided constant; no syscall |
| `@gothic/system` | `log`, `yield`, `info`, `sleep`, `uptime`, `exit` | Existing native IDs 1–3 plus subsystem dispatcher |
| `@gothic/memory` | `alloc`, `free`, `stats` | Existing ABI v2 allocator IDs 6–7; stats via dispatcher |
| `@gothic/io` | `read`, `write` | I/O subsystem dispatcher |
| `@gothic/fs` | `open`, `read`, `write`, `close`, `stat` | Filesystem subsystem dispatcher |
| `@gothic/network` | `status`, `connect`, `send`, `receive`, `close` | Network subsystem dispatcher |
| `@gothic/async` | `yield`, `sleep`, `cancel` | Yield is native ID 2; sleep/cancel via scheduler dispatcher |
| `@gothic/device` | `query`, `invoke` | Device/HDF subsystem dispatcher |
| `@gothic/logging` | `debug`, `info`, `warn`, `error` | Native logging path (severity currently collapsed to kernel log) |
| `@gothic/math` | `abs`, `min`, `max` | Implemented in the runtime; no kernel call |

### Registering gothicOS subsystem services

Native IDs 10–32 are stable UmbraRT module-binding IDs, **not** NULLK syscall
numbers. The gothicOS process/task integration layer supplies the actual
filesystem, network, I/O, scheduler, and device implementations with
`Runtime::set_native_dispatch`:

```cpp
static bool dispatch_os(void *context, uint8_t id,
                        const umbra::Value *args, uint8_t argc,
                        umbra::Value *result, bool *yielded) {
    // Route id to the relevant gothicOS subsystem and validate its granted
    // capability/handle ownership here. Set *yielded for cooperative waits.
    // Return false for unsupported operations or subsystem errors.
    return false;
}

umbra::Runtime runtime;
runtime.set_native_dispatch(dispatch_os, nullptr);
```

The callback receives only the compact `Value` ABI (null, bool, integer,
float, string, array handle). It must validate all handles and must enforce
actual task grants; package `@Capability` metadata is only a declaration.
The callback should not block the kernel scheduler. For a pending async
operation it can set `*yielded = true` and let the scheduler resume the task.

The existing NULLK headers do not define stable filesystem, network, device,
or generic stream APIs, so those exports are registered but intentionally
require the gothicOS subsystem dispatcher. `@gothic/math` is implemented
locally. `@gothic/core` is compiler-provided and currently exports only the
compile-time `VERSION` constant.
