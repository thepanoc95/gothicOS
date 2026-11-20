# gothicOS

> An OS for us all! :3

> [!NOTE]
> This project is still work in progress!

## Repository map

| Area | Purpose |
| --- | --- |
| `SDK/` | Stable C ABI (`kernel_abi.h`), native component helpers and UmbraTS SDK. |
| `internal/nullkern/` | Reference Python kernel, boot stages and ABI validation. |
| `kernel/` | NullKernRT native stack: freestanding C core, C/Rust HAL+KAL, C++ drivers, Linux handoff state machine. |
| `nullk/` | Baremetal null_kernel microkernel: SVC ABI v2, preemptive scheduler, capability IPC, `libnk`, host tests, Cortex-M3 QEMU image and Pico W UF2 image. |
| `ports/linux/` | Cython userspace port, `/dev/nullkern` module and opt-in confined handoff ioctls. |
| `ports/micropython/` | `null.boot()` chainloader, init ELF generation and UmbraTS shell launch. |
| `src/gothicos/OSBuild/` | Board and build recipe validation/execution. |
| `tests/` | Python, native stack and chainloader checks. |

See [Native migration](docs/NativeMigration.md) for the architecture and safety model,
[Linux port](docs/LinuxPort.md) for module/ioctl details, and
[UmbraTS](docs/UmbraTS.md) for the shell bytecode runtime.

## Quick checks

```sh
python -m unittest discover -s tests -q
make -C kernel all
cargo test --workspace --offline --manifest-path kernel/Cargo.toml
python ports/micropython/build.py
```
