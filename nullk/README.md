# nullk / null_kernel (baremetal)

`nullk/` is the baremetal implementation of null_kernel. It is a microkernel:
the only things that run in privileged mode are scheduling, preemption, IPC,
capability bookkeeping, memory pools, IRQ dispatch and a console driver.
Everything else lives in tasks and uses the **NULLK ABI v2** SVC interface.
NULLK is deliberately **not POSIX**; there are no file descriptors,
`open/read/write`, `fork`, signals or pthreads. The default Cortex-M3 image is
freestanding with no libc dependency. The optional Pico W port uses the Pico SDK
and TinyUSB for its USB serial console, so that image does link libc.

## Layout

```
nullk/
  arch/arm/cortexm/     Cortex-M3 QEMU target: boot, vectors, UART, linker
  arch/arm/dev/rp2040/  legacy freestanding RP2040 backend (boot2, M0+ switcher, USB)
  pico/                 Pico SDK + TinyUSB build of the RP2040/Pico W port
  tools/                host-side boot-stage checksum and UF2 packer
  include/nullk/        public ABI and kernel-native library headers
  nk/                   microkernel core (boot, tasks, scheduler, IPC, caps,
                        memory, IRQ, syscalls, runmode, HDF console entry)
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
