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
| `initrd-6.6.162-nk.img` | gzip cpio initramfs with `/init`, `/@` payload and DRM/fbdev modules |

### Intel framebuffer on UEFI laptops

The 6.6.162 build uses `CONFIG_DRM_I915=m` plus `CONFIG_FB=y`,
`CONFIG_FB_EFI=y`, `CONFIG_DRM_FBDEV_EMULATION=y` and
`CONFIG_FRAMEBUFFER_CONSOLE=y`. That gives the Acer Aspire A514-54 an Intel DRM
scanout, an EFI/GOP fallback and a real VT/fbcon text console instead of a black
screen after handoff.

`configs/initramfs/kernel-modules.txt` lists the modules packed into the image:
`drm_display_helper.ko`, `ttm.ko`, `drm_buddy.ko`, `i2c-algo-bit.ko` and
`i915.ko` (loaded last with `fastboot=1`). `/init` reads
`/lib/nullk/kernel-modules` and loads them before `nkabi3.ko`; no shell or
`insmod` binary is needed. `make initramfs` takes the modules from `KDIR` and,
when present, copies `$(FIRMWARE_DIR)/i915/*` to `/lib/firmware/i915/`.

Without i915 firmware files the driver still initialises the display, but DMC,
GuC and HuC features stay disabled. Install `linux-firmware` (or set
`FIRMWARE_DIR` to a tree containing `i915/`) and rebuild the image to get them.

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

There is no null_kernel framebuffer TUI. `src/user/tty_console.c` opens
`/dev/console` (which follows Linux's `console=` argument), falls back to
`/dev/tty0` and `/dev/ttyS0`, claims the tty as the controlling terminal and
duplicates it onto fd 0/1/2 in non-canonical, no-echo mode. nksh's own line
editor handles echo, backspace and escape sequences, so Ctrl+C and Ctrl+L are
not stolen by the terminal driver. If no tty can be opened, PID 1 keeps the
`/dev/console` descriptors inherited from the kernel.

With `-vga std` and no serial override, Linux uses `tty0` for `/dev/console`.
Add `console=ttyS0,115200` only when you specifically want a UART console:

```text
console=ttyS0,115200 init=/init rw loglevel=4
```

For a visible QEMU terminal:

```sh
qemu-system-x86_64 ... -vga std -vnc :99 \
  -append 'init=/init rw loglevel=4'
```

The `nk)` prompt appears in VNC and accepts keyboard input as soon as init
starts. The shared command engine tracks a working directory (`cd`, `.`,
`..`) and adds `stat`, `head`, `wc`, `grep`, `history` and `sysinfo`, plus a
line editor: Left/Right/Home/End/Delete, word delete, Ctrl+A/E/U/K/L/C and Tab
completion of command names.

### Optional LVGL desktop

The framebuffer is reserved for the optional UI, started from the tty with
`ui lvgl`. It is not started by init and does not need a console of its own.
With a virtio-GPU or real DRM card, `FB_INFO` and `FB_MAP` return a real
scanout buffer so LVGL/InterfaceKit can paint under Linux. Headless virtio-GPU
still gets a 1024x768 dumb-buffer fallback, and the shim periodically issues
`DRM_IOCTL_MODE_DIRTYFB` so the host actually repaints that buffer. LVGL input
comes from evdev nodes under `/dev/input/event*`. Without any framebuffer
device, the desktop is unavailable and the tty console keeps working.

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
