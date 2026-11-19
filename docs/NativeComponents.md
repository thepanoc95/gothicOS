# Native components

Native components are C ABI objects that run **beside** the MicroPython-hosted
nullkernRT kernel. They do not import MicroPython, are not compiled to `.mpy`,
and never receive a Python callback. All kernel access goes through the stable
`gothicos_invoke_fn` transport from [`kernel_abi.h`](../SDK/include/gothicos/kernel_abi.h).

This keeps the existing ABI, SDK clients, Umbra VM, and tests valid while
letting performance-sensitive or non-Python code own a bounded `step()`.

## Model

```
host (CPython/Cython or firmware)
  |  installs transport: gothicos_invoke_fn
  v
native component library  --ABI opcodes-->  nullkernRT Kernel.abi.invoke
  |  init / step / shutdown
  v
ports, memory pools, soft bus, nodes, abilities
```

The native core stack in [`kernel/`](../kernel) can also be the host itself: the freestanding
C core is reached through `nk_invoke()`, then wrapped by the C++/Rust HAL and KAL. Native
components keep using the same `gothicos_invoke_fn` transport; see
[Native migration](NativeMigration.md) for the full layer map. C++ drivers live in
`kernel/drivers/cpp` and register with the KAL rather than importing MicroPython.

A component implements this descriptor from
[`native_component.h`](../native/include/gothicos/native_component.h):

```c
typedef struct {
    const char *name;
    int (*init)(void *context);
    int (*step)(void *context);
    void (*shutdown)(void *context);
    void *context;
} gothicos_native_component_t;
```

Callbacks return a kernel ABI status (`GOTHICOS_STATUS_OK` means continue).
`step()` must be bounded and cooperative: handle at most one message or event
and return. Empty queues should return `OK`, not block — a native component has
no MicroPython task object that the scheduler can park.

## ABI payload

`gothicos_invoke_fn` keeps the existing signature:

```c
typedef int (*gothicos_invoke_fn)(int opcode, const void *arguments, void *result);
```

Its payload types are now defined in `kernel_abi.h`:

- `arguments` points to `gothicos_arguments_t`, or is `NULL` for zero arguments.
- `result` points to `gothicos_result_t` (`status` plus a tagged value).
- `GOTHICOS_VALUE_INT` and `GOTHICOS_VALUE_STRING` are the supported value kinds.
  Strings are NUL-terminated UTF-8; result strings are valid until the next
  invoke, so C clients must copy them.

Complex Python-only results (for example `GET_INFO`'s dict) are flattened by the
host adapter: `abi_version` becomes a packed integer `0xMMmmpp`. That is what
`gothicos_sdk_init()` validates against `>=1.0.0,<2.0.0`.

The previously declaration-only functions `gothicos_status_name`,
`gothicos_opcode_name`, `gothicos_call`, `gothicos_sdk_abi_supported`, and
`gothicos_sdk_init` are implemented in `native/src/gothicos_native.c`.

## Included example

[`native/components/echo/echo_component.c`](../native/components/echo/echo_component.c)
opens `native.echo.in` and `native.echo.out`, echoes one message per step as
`echo:<message>`, and closes both ports in `shutdown()`. It uses only
`PORT_OPEN`, `PORT_RECEIVE`, `PORT_SEND`, and `PORT_CLOSE`.

## Writing another component

1. Include `<gothicos/native_component.h>`.
2. Keep component state in a `context` struct; only the descriptor should be static.
3. In `init`, acquire resources with `gothicos_native_port_open()` and friends.
   Treat `GOTHICOS_STATUS_ALREADY_EXISTS` as success for well-known port names so
   hosts can pre-create or inspect them.
4. In `step`, do one bounded unit of work. Translate `GOTHICOS_STATUS_WOULD_BLOCK`
   into `OK` so an idle queue is not an error.
5. In `shutdown`, release anything acquired in `init`.
6. Export a factory returning `const gothicos_native_component_t *`.

```c
#include <gothicos/native_component.h>

static int my_init(void *context) { /* open ports/pools */ return GOTHICOS_STATUS_OK; }
static int my_step(void *context) { /* one message or event */ return GOTHICOS_STATUS_OK; }
static void my_shutdown(void *context) { /* release resources */ }

static const gothicos_native_component_t my_component = {
    "vendor.my_component", my_init, my_step, my_shutdown, 0,
};

GOTHICOS_NATIVE_EXPORT const gothicos_native_component_t *gothicos_my_component(void)
{
    return &my_component;
}
```

### C API reference

| Function | Purpose |
| --- | --- |
| `gothicos_native_set_transport` / `gothicos_native_transport` | Install/read the process `gothicos_invoke_fn`. |
| `gothicos_native_call` | Invoke any ABI opcode with a `gothicos_arguments_t` and copied `gothicos_result_t`. |
| `gothicos_native_port_open` / `_close` / `_send` / `_receive` | Typed wrappers over the port opcodes. `_receive` takes an explicit `task_id`; use `-1` outside a task. |
| `gothicos_native_component_start` / `_step` / `_stop` / `_run` | Drive the descriptor lifecycle. `_run(steps)` pumps at most `steps` cooperative steps. |
| `gothicos_call` | Explicit-transport variant returning the legacy `gothicos_response_t`. |
| `gothicos_sdk_init` / `gothicos_sdk_abi_supported` | Install a transport and validate ABI `>=1.0.0,<2.0.0`. |

## Build

```sh
uv run gothicos validate --config native/build.json
uv run gothicos build --config native/build.json --dry-run
uv run gothicos build --config native/build.json
```

The `NativeComponents` source recipe compiles each `.c` file with `cc` and links
`build/NativeComponents/Native/libgothicos_native.so`. Override the compiler with
`GOTHICOS_CC`. OSBuild source outputs can now declare `native.source.files`,
`native.include.dirs`, and `native.shared-library` instead of (or alongside)
`.py`/`.pyx` `source.files`. See [OSBuild](OSBuild.md).

## Linux host

`gothicos_sdk.native.NativeComponent` adapts any `abi_invoke(opcode, *args)`
callable to a C transport pointer:

```python
from gothicos_sdk import NativeComponent
from internal.nullkern.kernel import Kernel

kernel = Kernel()
echo = NativeComponent(
    "build/NativeComponents/Native/libgothicos_native.so",
    kernel.abi.invoke,
)
echo.start()
kernel.port_send("native.echo.in", "ping")
echo.step()
status, message = kernel.abi.invoke(13, "native.echo.out", -1)
echo.stop()
```

The same constructor works with `gothicos_linux_port.kernel_from_device(...)`'s
`abi_invoke` method; no Linux-port code changes are needed. Only one transport
can be active per process because the component library stores a single global
function pointer.

### `NativeComponent` API

| Member | Purpose |
| --- | --- |
| `NativeComponent(library, abi_invoke, component_symbol="gothicos_echo_component")` | Loads the shared library, installs the transport via `gothicos_sdk_init()`, and resolves the component factory. Raises `NativeABIError` on ABI mismatch or a missing descriptor. |
| `name` | Decoded component name from the C descriptor. |
| `start()` / `step()` / `stop()` | Drive one lifecycle call. They return the integer ABI status; `step()` returns `0` for an empty queue. |
| `run(steps)` | Start, pump at most `steps` steps, then stop; stops early on a non-OK status. |
| `call(opcode, *args)` | Invoke one operation through `gothicos_call()`, returning `(status, int_value)`. Strings are not returned by this legacy-shaped helper; use ABI ports for payloads. |
| `status_name(status)` / `opcode_name(opcode)` | Human-readable names from the native library. |

The adapter packs Python `int`/`str` arguments into `gothicos_arguments_t`. It
flattens `(sender_id, message)` port receives to the message and packs
`GET_INFO`'s `abi_version` tuple into an integer. Other complex results are
encoded as `GOTHICOS_VALUE_NONE`; native components should use ports or memory
pools for structured data.

## RP2040 / firmware

The firmware side supplies the same function pointer using a C adapter over
`Kernel.abi.invoke` (or a direct native implementation of the opcode table),
then pumps `gothicos_native_component_step()` from its own scheduling loop.
The component library itself is plain C11 with no Python headers, so it can be
linked into a MicroPython firmware image together with the `.mpy`
`KernelCollection`. Use `-march=armv6m` for RP2040 when building the object
files, and keep `native.source.files` outputs separate from the `.mpy` output so
the compiler flags do not collide. Linking the C library into firmware requires
a custom MicroPython build; stock firmware can only run the `.mpy` side.

### Checking a connected board with mpremote

The compiled kernel and a MicroPython stand-in for the C component's port
protocol can be validated on a Pico W without custom firmware:

```sh
uv run gothicos build --config internal/nullkern/build.json
mpremote connect /dev/ttyACM0 fs cp -r build/nullkernRT/KernelCollection/internal/nullkern :internal/
mpremote connect /dev/ttyACM0 run native/device/echo_component_check.py
```

The check opens `native.echo.in`/`native.echo.out`, sends `ping`, and asserts
that one step produces `echo:ping`. This verifies that the ABI opcodes, port
names, and message shape used by `echo_component.c` hold on real hardware. The
device serial port may differ; `mpremote devs` lists connected boards.

## Other languages

- **C++**: include the headers inside `extern "C"`, export a descriptor with the
  same layout, and implement `init`/`step`/`shutdown` as `extern "C"` functions.
- **Rust**: a `no_std` `staticlib` can export `gothicos_*_component` returning a
  `#[repr(C)]` descriptor. Use `extern "C"` for callbacks and copy all borrowed
  strings into Rust-owned buffers before returning from a step.

## Limitations

- Native components are ABI clients, not a replacement kernel. Task callbacks,
  ability `invoke` handlers, and IRQ handlers are still Python callables in
  `internal/nullkern`.
- The transport is a process-global pointer; multiple transports require
  separate library instances or a future multi-client registry.
- Capability checks use the ABI caller id. Calls made outside a running task are
  treated as holder `-1` (kernel), so native components must be loaded only by
  trusted host code.
- `.c` is the only native source extension OSBuild compiles today; C++ and Rust
  need a command recipe or a future toolchain-specific output type.

## Verification

```sh
uv run python -m unittest discover -s tests
uv run gothicos validate --config native/build.json
uv run gothicos build --config native/build.json
```

`tests/test_native_components.py` compiles the C sources, loads the resulting
library with ctypes, and checks the echo round trip, idle steps, status/opcode
helpers, and ABI rejection. `tests/test_osbuild.py` checks that a native source
recipe compiles objects and links a callable shared library.

On a connected Pico W the on-device check described above produced:

```
native echo ABI contract OK on Raspberry Pi Pico W with RP2040
```

Deploying the `.mpy` collection leaves `internal/nullkern/*.mpy` on the board's
filesystem; remove it with `mpremote connect /dev/ttyACM0 fs rm -r :internal/nullkern`
if you want a clean device.
