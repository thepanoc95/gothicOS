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
| `nullk/` | null_kernel: ARM microkernel (ABI v2 SVC, Cortex-M3/Pico W) **and** Limine-booted AMD64 PC port (ABI v3, `printk`, `@/` filesystem, `nk) ` shell, USB-as-SATA loader, ACPI). |
| `ports/linux/` | Cython userspace port, `/dev/nullkern` module and opt-in confined handoff ioctls. |
| `ports/micropython/` | `null.boot()` chainloader, init ELF generation and UmbraTS shell launch. |
| `src/gothicos/OSBuild/` | Board and build recipe validation/execution. |
| `tests/` | Python, native stack and chainloader checks. |

See [Native migration](docs/NativeMigration.md) for the architecture and safety model,
[Linux port](docs/LinuxPort.md) for module/ioctl details, and
[UmbraTS](docs/UmbraTS.md) for the shell bytecode runtime.

## Quick checks

```sh
make -C nullk test
make -C nullk BOARD=amd64 test
make -C nullk BOARD=amd64 all

# AMD64 boot details, @/ tree, shell and USB-as-SATA protocol:
#   nullk/docs/AMD64.md
```
