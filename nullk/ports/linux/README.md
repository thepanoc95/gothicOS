# Native Linux port (ABI v3)

This replaces the deleted MicroPython/Python/Cython `ports/linux` project. It is
a native gothicOS/null_kernel port: the same ABI v3 user runtime, service model
and SecureKernel policy are used on AMD64 and under Linux 6.6.162 LTS.

## Status

Phase 1 is implemented and out-of-tree:

- `src/lkm/main.c` builds `nkabi3.ko`, a misc-device LKM that owns the bounded
  kernel half of ABI v3: `GETINFO`, `LOG`, `YIELD`, `SLEEP`, `TICKS` and the
  shared USB RAM-disk primitives.
- `src/user/nk64_syscall_linux.c` implements the `nk64_syscall()` primitive for
  the unmodified `libnk64` wrappers. ABI v3 numbers are decoded in userspace;
  they are never issued as Linux syscalls, so the two ABIs cannot collide.
- `src/user/init_pid1.c` is a PID 1 replacement. It starts the same `init64`
  service chain (SecureKernel, `usb64`, `nksh`) using Linux threads that share
  one address space, matching the AMD64 task model.
- `src/user/initramfs_init.c` and `scripts/mkinitramfs.py` build a static C
  `/init` (also installed as `/lib/init`) that mounts `/proc`, `/sys` and
  `/dev`, loads `nkabi3.ko`, stages `@/` at `/@` and execs the native init; no
  shell or `insmod` binary is needed inside the archive.
- `patches/optional/` is reserved for the deeper Linux patch set. The module
  exports `nk64_parasite_mode()` as the stable hook for scheduler/VFS/exec
  patches; no kernel source is downloaded or modified by this directory.

Filesystems, USB/SATA policy, SecureKernel and the shell remain userspace
services. The LKM does not absorb them and does not execute arbitrary ELFs in
kernel mode.

## Build

No Linux source is needed for the userspace binary and host checks:

```sh
make -C nullk/ports/linux
```

Build the LKM only when a matching Linux 6.6.162 tree is available:

```sh
make -C nullk/ports/linux module KDIR=/path/to/linux-6.6.162
make -C nullk/ports/linux initramfs KDIR=/path/to/linux-6.6.162
```

Outputs go to `build/NullKernel/linux/`:

| Output | Purpose |
| --- | --- |
| `nullk-linux.elf` | PID 1 replacement containing init64 + all ring-3 services |
| `test_linux_port` | Host ABI-layout/path-map checks |
| `nkabi3.ko` | Optional ABI v3 control-plane LKM |
| `initrd-6.6.162-nk.img` | gzip cpio initramfs with `/init` and `/@` payload |

## ABI boundary

`nullk/include/nullk/abi64.h` remains canonical. `include/nullk_linux/abi64_kernel.h`
is a byte-layout mirror for kernel code because Linux headers do not provide
`<stdint.h>`; `tests/test_abi_layout.c` asserts that the mirror and canonical
structs have identical sizes and offsets.

Syscall routing:

| Calls | Handler |
| --- | --- |
| `READ`, `WRITE`, `OPEN`, `CLOSE`, `STAT`, `READDIR` | Linux userspace VFS adapter rooted at `/@` (`NULLK64_ROOT` overrides it for tests) |
| `YIELD`, `SLEEP`, `TICKS`, `GETINFO`, `LOG` | `/dev/nkabi3` ioctl, with userspace fallback |
| `USB_*` | `/dev/nkabi3` bounded 2-LUN RAM disk |
| `TASK_CREATE`, `TASK_STATE`, `EXIT`, `EXEC`, `EXEC_MEM` | pthread task model + in-process ELF loader |
| `MMAP` | Linux anonymous mapping, including the ABI sandbox hint |
| `IRQ_BIND`, `IPC_*`, `INPUT_READ` | forwarded to the LKM; unsupported until the patch phase |
| `FB_INFO`, `FB_MAP` | Linux `/dev/fb0` first, then a DRM/KMS dumb buffer on `/dev/dri/card0` |

The ABI v3 shell owns the visible console. Output from background services
(SecureKernel, `usb64`, InterfaceKit diagnostics) is written to
`/@/data/system/nullk.log` so the `nk)` prompt is not trampled. The LKM's LOG
ioctl is logged at `pr_debug()` as well.

### Console selection

`nksh` inherits fd 0/1 from `/init`, so it follows Linux's `/dev/console`. On a
normal PC with no serial UART, omit `console=ttyS0,...` from the kernel cmdline:

```text
init=/init rw loglevel=4
```

Linux then uses `tty0`, and the `nk)` prompt plus keyboard input work on the
VGA/VT console without InterfaceKit or a desktop. Add the serial override only
when you want the shell on a UART:

```text
console=ttyS0,115200 init=/init rw loglevel=4
```

Background service chatter still goes to `/@/data/system/nullk.log`.

InterfaceKit stays opt-in. With a virtio-GPU or real DRM card, `FB_INFO` and
`FB_MAP` return a real scanout buffer so the desktop and future UI work under
Linux. Headless virtio-GPU still gets a 1024x768 dumb-buffer fallback, and the
shim periodically issues `DRM_IOCTL_MODE_DIRTYFB` so the host actually repaints
that buffer. Without any framebuffer device, it logs that the desktop is
disabled and serial services continue.

For a visible QEMU desktop, make the virtio GPU the primary display:

```sh
qemu-system-x86_64 ... -vga none -device virtio-gpu-pci -vnc :99
```

Using `-vga virtio` leaves QEMU showing the legacy VGA text scanout instead of
the DRM framebuffer.

## Linux boot wiring

The Limine config can boot Linux 6.6.162 with:

```text
KERNEL_PATH: boot():/vmlinux-6.6.162-nk
MODULE_PATH: boot():/initrd-6.6.162-nk.img
CMDLINE: root=LABEL=gothicOS-linux-root ro init=/init
```

The generated initramfs supplies `/init`, so the explicit `init=/init` is only
needed for debugging custom boot setups.
