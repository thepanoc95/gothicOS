# gothicOS

## OSBuild

OSBuild validates board configurations, runs command recipes, and compiles declared
MicroPython/Cython source recipes into collections under `build/`. Board recipes are
processed in boot-then-software order. Remote `url` entries are descriptive only; OSBuild
does not clone or build them.

```sh
uv run gothicos validate --config src/gothicos/OSBuild/boards/rp2-rpiw.json
uv run gothicos build --config src/gothicos/OSBuild/boards/rp2-rpiw.json --dry-run
uv run gothicos build --config src/gothicos/OSBuild/boards/rp2-rpiw.json
uv run gothicos validate --config internal/nullkern/build.json
uv run gothicos build --config internal/nullkern/build.json --dry-run
uv run gothicos build --config internal/nullkern/build.json
```

Paths in configs are relative to the project root (the current directory by default).
`%PROJROOT` can be used in recipe paths, command arguments, working directories, and
environment values. Recipe files and working directories must stay inside the project root.

## Runtime Kernel

`internal/nullkern` is nullkernRT: a microkernel that runs as MicroPython code and is
compiled to `.mpy` by the `nullkernRT` recipe. It provides:

- round-robin dispatch of up to eight tasks on one or two RP2 interpreter cores
  (`_thread`); callbacks do one bounded step and return `False` to stop
- bounded task mailboxes plus named IPC ports with fixed-size queues
- fixed-block memory pools with numeric block handles (no raw pointers)
- capability tokens with per-right enforcement at the ABI boundary
- deferred software interrupts serviced cooperatively by the scheduler
- a device-driver registry, platform reporting, and a nine-stage boot sequence
- System Ability Manager, soft bus, remote abilities, and distributed scheduling hints
- runtime FDT CPU discovery with `Kernel.from_fdt(path_or_bytes)`
- a verified Umbra Bytecode execution module (`kernel.binary`) with bounded VM tasks,
  capability-checked SDK natives, and cooperative `yield` scheduling

An empty `receive()` blocks the caller until a sender wakes it. MicroPython's RP2 port
supports only two interpreter threads, one per core. Python callbacks cannot be safely
interrupted mid-step, so hard preemption still requires a native RTOS/context-switch
layer. Tasks share the interpreter heap: capability checks constrain operations but are
not a hardware MMU/MPU boundary. The RP2 recipe targets Cortex-M0+ with
`mpy-cross-arch: armv6m` and uses MicroPython's native, Viper, and Thumb inline-assembler
emitters.

The public runtime boundary is `Kernel.abi.invoke(opcode, *args)`. ABI v1.1.1 keeps all
v1.0.0 operations (info, task lifecycle/inspection, IPC, yield, run) and adds ports,
memory pools, interrupts, drivers, capabilities, boot metadata, subsystem abilities,
soft-bus messaging, remote ability calls, and scheduling hints. Calls return
`(status, value)`; status codes distinguish invalid arguments, missing tasks, blocked
receives, full queues, unsupported operations, permission failures, already-existing
objects, out-of-memory, and internal task failures. Operation and status constants are
exported by `internal/nullkern/abi.py` and mirrored by the SDK. To boot the kernel on
hardware, see [Starting nullkernRT on a Pico W](docs/PicoW.md).

## Native components

[`native/`](native/) adds MicroPython-free C components that live beside nullkernRT.
They implement a small `init`/`step`/`shutdown` descriptor and call the same v1.1.1 ABI
through a `gothicos_invoke_fn` function pointer, so one library works with the Linux
port and with firmware. The included echo component is built by
`gothicos build --config native/build.json`; `gothicos_sdk.NativeComponent` loads it
from Python. See [Native Components](docs/NativeComponents.md).

## SDK / Toolchain

[`SDK/`](SDK/README.md) contains the application SDK: a host-side Python client and
`gothicos-sdk` CLI, the UmbraTS compiler in `SDK/umbra/`, a tiny MicroPython client compiled
to `nullkern_sdk.mpy`, Cython host tools with a standalone build script, stable C headers
under `SDK/include/gothicos/`, and a working `examples/hello` app.

```sh
uv pip install -e SDK
gothicos-sdk new demo
gothicos-sdk check demo
gothicos-sdk build demo --output build/demo
python SDK/toolchain/build.py --out build/SDK/Toolchain
gothicos build --config SDK/build.json
gothicos-sdk new demo --language umbrats
gothicos-sdk umbra build demo/main.uts --output demo.umb
```

The SDK accepts kernel ABI `>=1.0.0,<2.0.0`. See [SDK documentation](SDK/README.md) for
manifests, the device client, toolchain outputs, and header details. The implemented
UmbraTS subset and its bytecode/runtime contract are described in
[UmbraTS documentation](docs/UmbraTS.md).

The subsystem model is documented in
[Distributed Subsystems](docs/DistributedSubsystems.md). A Cython userspace port with an
optional thin C char-device LKM for Linux is described in [Linux Port](docs/LinuxPort.md).

Recipes support either ordered command steps or a declarative source recipe. The source
recipe form is:

```json
{
  "$schema": "../src/gothicos/OSBuild/schema/osbuild.json",
  "@build": {
    "recipe": {
      "name": "example",
      "type": "Runtime Kernel",
      "kernel-abi": "1.1.1",
      "entry": ["build.py"],
      "outputs": {
        "KernelCollection": {
          "micropython-bytecode": "v6",
          "source.files": ["kernel.py"]
        }
      }
    }
  }
}
```

`.py` inputs compile to MicroPython bytecode with `mpy-cross`; `.pyx` inputs translate
to C with Cython. Source recipe `kernel-abi` is a `MAJOR.MINOR.PATCH` version passed to
entry and compile actions as `GOTHICOS_KERNEL_ABI`. Output files retain their project-relative paths below
`build/<recipe>/<output>/`. See [OSBuild documentation](docs/OSBuild.md) for both recipe
formats, path rules, and limitations.
