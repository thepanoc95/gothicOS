# Native Linux ABI v3 port

The old `ports/linux` project was a MicroPython/Python/Cython ABI v1.1.1
transport and has been removed. The replacement lives in
[`nullk/ports/linux/`](../nullk/ports/linux/README.md) and targets NULLK ABI v3
directly.

## What it provides

- an out-of-tree `nkabi3.ko` LKM exposing bounded ABI v3 kernel primitives on
  `/dev/nkabi3`;
- a Linux backend for the unchanged `libnk64` wrappers, so ABI v3 numbers never
  collide with Linux syscalls;
- a PID 1 replacement that starts SecureKernel, `usb64` and `nksh` as Linux
  threads sharing one address space;
- an `@/` VFS adapter rooted at `/@` inside initramfs;
- an in-process ELF loader for `EXEC`/`EXEC_MEM`, preserving the AMD64
  check-then-exec-bytes model after SecureKernel verification;
- a generated gzip-cpio initramfs with a static C PID 1 that mounts the basic
  pseudo-filesystems, loads `i915.ko` plus its DRM/fbdev dependencies from
  `/lib/modules/6.6.162-nk`, loads `nkabi3.ko` and boots the native userspace;
- a Linux framebuffer backend: `/dev/fb0` first, then a DRM/KMS dumb buffer on
  `/dev/dri/card0` (with a 1024x768 fallback for headless virtio-GPU and a
  periodic `DIRTYFB` flush so the host repaints it), so LVGL/InterfaceKit can
  use the VGA/virtio-GPU adapter;
- a tty console for `nksh`: PID 1 opens `/dev/console` (or `/dev/tty0` /
  `/dev/ttyS0`), claims it as the controlling terminal and sets it to
  non-canonical, no-echo mode so the shell's own line editor sees every key;
- console routing that follows Linux's `/dev/console` for serial/VT output while
  background service chatter goes to `/@/data/system/nullk.log`;
- an optional LVGL desktop, started with `ui lvgl` from the tty, which uses the
  framebuffer backend above and keyboard/pointer events from
  `/dev/input/event*`; no desktop starts at boot.

The first phase is out-of-tree. Optional Linux 6.6.162 LTS patches belong in
`nullk/ports/linux/patches/optional/` and hook the exported
`nk64_parasite_mode()` symbol. No kernel source is downloaded or patched by the
default build.

## Build and test

```sh
# host checks + userspace ELF (no kernel source required)
make -C nullk/ports/linux

# only when a matching Linux 6.6.162 tree is available
make -C nullk/ports/linux module KDIR=/path/to/linux-6.6.162
make -C nullk/ports/linux initramfs KDIR=/path/to/linux-6.6.162
```

Outputs are written to `build/NullKernel/linux/`. See the port README for the
ABI routing table and boot wiring.
