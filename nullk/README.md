# nullk / null_kernel (baremetal)

`nullk/` is the baremetal implementation of null_kernel. It is a microkernel:
the only things that run in privileged mode are scheduling, preemption, IPC,
capability bookkeeping, memory pools, IRQ dispatch and a console driver.
Everything else lives in tasks and uses the **NULLK ABI v2** SVC interface.

The same `nk/` kernel tree also holds the AMD64 PC port (`nk/kernel64/`). It
uses **NULLK ABI v3** and a reverse-monolithic layout: ring 0 only handles CPU,
memory, interrupts, IPC, scheduling, `printk`, a bounded framebuffer/input
interface and a small emergency shell; the `@/` filesystem, the `nk) ` shell,
the USB/SATA loader and the InterfaceKit desktop server run as ring 3 tasks.
InterfaceKit itself is board-neutral: its common grained-glass widgets are also
built into the embedded ARM images with a reduced control set. See
[docs/AMD64.md](docs/AMD64.md) and [docs/InterfaceKit.md](docs/InterfaceKit.md).

NULLK is deliberately **not POSIX**; there are no file descriptors,
`open/read/write`, `fork`, signals or pthreads. The default Cortex-M3 image is
freestanding with no libc dependency. The optional Pico W port uses the Pico SDK
and TinyUSB for its USB serial console, so that image does link libc.

## Layout

```
nullk/
  nk/                   the kernel tree, shared by all ports: boot, tasks,
                        scheduler, IPC, caps, memory, IRQ, syscalls, runmode,
                        HDF console entry
    kernel64/           AMD64 ring 0 core (ABI v3, ELF/UART loaders)
  arch/amd64/           Limine-booted AMD64 target: boot, paging, IDT, UART, PS/2 input
  shared/               portable printk, VFS, ELF64, ACPI, shell, USB-as-SATA, InterfaceKit drawing/widgets
  apps/                 AMD64 userspace: init, InterfaceKit server/demo, shell, USB, libnk64
  securekernel/         userspace security layer: type policy, audit, ELF integrity
  arch/arm/cortexm/     Cortex-M3 QEMU target: boot, vectors, UART, linker
  arch/arm/dev/rp2040/  legacy freestanding RP2040 backend (boot2, M0+ switcher, USB)
  pico/                 Pico SDK + TinyUSB build of the RP2040/Pico W port
  tools/                host-side boot-stage checksum and UF2 packer
  include/nullk/        public ABI and kernel-native library headers
  lib/libnk/            freestanding kernel-native library (SVC wrappers)
  apps/init/            initial task set built into the image
  tests/                host-side tests for portable kernel logic
```

## Design

- **Preemptive scheduler**: 8 priority levels, round-robin within a level,
  fixed time slice driven by SysTick. `PendSV` performs the actual context
  switch at the lowest exception priority.
- **Tasks**: fixed-size TCBs and statically carved stacks. Task states are
  `READY`, `RUNNING`, `BLOCKED`, `SUSPENDED` and `DEAD`. A task blocks in the
  kernel on IPC or sleep; it never busy-waits.
- **IPC**: bounded, capability-protected endpoint queues of fixed 64-byte
  messages. `SEND`/`RECV` are asynchronous; `CALL`/`REPLY` provide a
  synchronous request/response pattern with a token. Full/empty queues block
  the caller.
- **Capabilities**: integer handles stored in a per-task table with explicit
  `READ`/`WRITE`/`GRANT`/`MANAGE` rights. Handles can be granted or revoked
  only through syscalls; user code never passes raw kernel pointers.
- **ABI v2**: all task-to-kernel traffic goes through `svc #imm`. The immediate
  is the syscall opcode and `r0-r3` carry arguments. Results are returned in
  `r0`. No POSIX compatibility layer exists.
- **Kernel-native library**: `libnk` is a small freestanding C library that
  wraps the ABI, provides bounded string/memory helpers and a minimal
  `nk_logf` formatter. It has no libc or POSIX dependencies.
- **Runlevels**: boot is staged (`PLATFORM`, `CORE`, `MEMORY`, `IPC`, `IRQ`,
  `DRIVERS`, `TASKS`, `READY`) and the final stage is recorded in `NK_GET_INFO`.

See [docs/ABI-v2.md](docs/ABI-v2.md) for the syscall contract.

## Build

Host tests (no cross toolchain needed):

```sh
make -C nullk test
```

Cross compile a QEMU-testable ARM Cortex-M3 image:

```sh
make -C nullk CROSS=arm-none-eabi- all
```

Boot it under QEMU (optional, when `qemu-system-arm` is available):

```sh
make -C nullk CROSS=arm-none-eabi- qemu
```

The default board is `lm3s6965evb` (Cortex-M3, UART0 at `0x4000c000`).
Embedded builds include the reduced InterfaceKit widget set
(`-DINTERFACEKIT_EMBEDDED`): panel, label, button and progress controls with a
lighter grained-glass effect. The full desktop profile (AMD64 and ARM64) adds
toggle and slider widgets without changing the drawing core.

### AMD64 PC (Limine)

```sh
make -C nullk BOARD=amd64 test
make -C nullk BOARD=amd64 all
make -C nullk BOARD=amd64 qemu LIMINE_DIR=/path/to/limine
make -C nullk BOARD=amd64 qemu-ui LIMINE_DIR=/path/to/limine
```

The QEMU profile uses exactly 16 MiB of RAM (`-m 16M`). The build produces a
small higher-half ELF at `build/NullKernel/amd64/null_kernel_amd64.elf`, a
freestanding `init.elf`, and an `@/` initrd tarball. `qemu-ui` keeps the display
attached so the InterfaceKit desktop can claim a Limine framebuffer. Full
details are in [docs/AMD64.md](docs/AMD64.md) and
[docs/InterfaceKit.md](docs/InterfaceKit.md).

### Raspberry Pi Pico W

The RP2040/Pico W port is built with the Raspberry Pi Pico SDK and its TinyUSB
CDC ACM stdio backend. This is the only board build that links the SDK; the
default Cortex-M3 build remains freestanding. Set `PICO_SDK_PATH` and build:

```sh
cmake -S nullk/pico -B build/NullKernel/pico -DPICO_BOARD=pico_w
cmake --build build/NullKernel/pico -j4
```

Equivalently, `make -C nullk pico-sdk` runs both commands.

The flashable image is:

```
build/NullKernel/pico/null_kernel_pico.uf2
```

The console is USB CDC ACM on the micro-USB connector; no USB–TTL adapter or
GPIO wiring is needed. `nullk/pico/arch.c` implements the kernel arch hooks on
top of the SDK; `nullk/pico/context_sdk.S` reuses the RP2040 M0+ context switch
with the SDK's vector-table names. The CYW43 wireless chip is not initialized:
this port is the kernel core, scheduler, IPC and USB serial console only.

To flash, hold BOOTSEL while plugging in the Pico W, then either copy the UF2
file to the USB mass-storage device or run:

```sh
sudo picotool load -u -x build/NullKernel/pico/null_kernel_pico.uf2
```

`picotool -x` reboots into the new image. It replaces whatever firmware is
currently on the board. If `picotool` reports a permissions error, install a
udev rule for the RP2040 BOOTSEL device or run with elevated privileges.

After reboot the board enumerates as a standard Pico CDC device (VID `0x2e8a`,
PID `0x000a`). On Windows with usbipd, attach it to WSL first:

```sh
usbipd.exe list
usbipd.exe attach --wsl --busid <BUSID>
```

Then open its serial port, for example `screen /dev/ttyACM0 115200` on Linux,
to see the kernel log.
