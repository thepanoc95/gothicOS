# NULLKern Linux port

The Linux port uses Cython in userspace and a small C kernel module as a transport.
Cython-generated code cannot run in kernel space because it depends on CPython and
libc; the LKM therefore contains no Python runtime.

## Layout

| Path | Purpose |
| --- | --- |
| [`ports/linux/linux_port.pyx`](../ports/linux/linux_port.pyx) | Cython extension implementing the Linux NULLKern runtime and char-device client |
| [`ports/linux/include/gothicos_linux_port.h`](../ports/linux/include/gothicos_linux_port.h) | Shared ioctl/struct contract |
| [`ports/linux/nullkern_lkm.c`](../ports/linux/nullkern_lkm.c) | Thin C character device (`/dev/nullkern`) |
| [`ports/linux/build.py`](../ports/linux/build.py) | Cython-to-C translation script |
| [`ports/linux/setup.py`](../ports/linux/setup.py) | Optional CPython extension build |
| [`ports/linux/Makefile`](../ports/linux/Makefile) | Kernel module and extension targets |

## Userspace runtime

`LinuxKernel` provides a Cython implementation of the same core model: bounded task
slots, round-robin dispatch, mailboxes, named lifecycle abilities, soft-bus messages,
node capacity/load registration, remote ability calls, and scheduling hints. It exposes
`abi_invoke(opcode, *args)` with the subsystem ABI operations from v1.1.1.

```python
import gothicos_linux_port as port

kernel = port.kernel_from_device("/dev/nullkern")
kernel.register_local_ability("echo", lambda args: args)
kernel.register_remote_ability("echo", "local")
print(kernel.remote_call("echo", ["hello"]))
print(kernel.abi_invoke(port.OP_GET_INFO))
```

If `/dev/nullkern` is absent or the module is not loaded, the runtime still works; host
ioctls simply report `available: False`. This keeps host builds and tests independent of
kernel headers and root privileges.

## Building

Translate the port to C (requires Cython, no compiler needed):

```sh
python ports/linux/build.py --out build/LinuxPort
```

Build the CPython extension (requires Cython, setuptools, and Python development headers):

```sh
uv pip install setuptools
cd ports/linux
python setup.py build_ext --inplace
```

Build and load the optional kernel module (requires matching kernel headers and root):

```sh
cd ports/linux
make
sudo make install
```

`make uninstall` removes the module. The LKM exposes four ioctls: `GET_INFO`,
`RAISE_IRQ`, `PORT_SEND`, and `SCHED_HINT`. They update counters and wake waiters; they do
not execute application code.

## Testing with um-arm64

The [`linux-um-arm64`](https://github.com/zalexdev/linux-um-arm64) tree builds a
`ARCH=um SUBARCH=arm64` kernel that runs as an ordinary aarch64 process and supports
loadable modules, so `nullkern_lkm.ko` can be exercised inside the guest.

```sh
# Build the UML kernel (SUBARCH maps to an aarch64 target).
# Use LLVM=1 only if that tree is configured for clang; gcc-built trees work too.
make ARCH=um SUBARCH=arm64 -C /path/to/linux-um-arm64 defconfig
make ARCH=um SUBARCH=arm64 -C /path/to/linux-um-arm64 -j$(nproc)

# Build the out-of-tree nullkern module against that tree.
# The target uses the tree's configured compiler; add LLVM=1 for a clang tree.
make -C ports/linux KDIR=/path/to/linux-um-arm64 um-arm64
```

Running `./linux` with no arguments uses the defconfig default `root=98:0`, but
without a `ubd0=` image there is no block device to mount. Attach a rootfs image:

```sh
./linux mem=512M ubd0=rootfs/alpine.ext4 root=/dev/ubda rw \
        init=/bin/sh con=null con0=fd:0,fd:1
```

Copy `ports/linux/nullkern_lkm.ko` into that guest root filesystem, boot the UML
kernel, then load it **inside the guest**:

```sh
insmod /nullkern_lkm.ko
# With devtmpfs the module creates /dev/nullkern automatically; otherwise:
# mknod /dev/nullkern c <major from /proc/devices> 0
```

The small C probe avoids needing CPython inside the guest. Build it for the guest
architecture and run it after `insmod`:

```sh
make -C ports/linux probe
# copy ports/linux/nullkern_ioctl_probe into the guest, then:
./nullkern_ioctl_probe /dev/nullkern
```

It prints counter state before and after `RAISE_IRQ`, `PORT_SEND`, and
`SCHED_HINT`, and exits nonzero unless each counter advanced by one.

The ioctl structs use fixed-width `int32_t`/`uint64_t` fields, and the module's
`BUILD_BUG_ON` checks assert the 24-byte request and 32-byte response layouts, so
the ABI is valid on aarch64 as well as x86_64. Run the Cython client
(`kernel_from_device("/dev/nullkern")`) inside the guest to drive the four
ioctls. If your build host is x86_64, remember the resulting `linux` binary and
guest userspace are aarch64: you need an aarch64 host or an emulator such as
`qemu-aarch64` to run them.

## Native components

The Cython port's `abi_invoke(opcode, *args)` can be passed directly to
`gothicos_sdk.NativeComponent`, so a native C library built by the
`native/build.json` recipe runs against either `LinuxKernel` or the pure-Python
`Kernel` without changes:

```python
from gothicos_sdk import NativeComponent
import gothicos_linux_port as port

kernel = port.kernel_from_device("/dev/nullkern")
echo = NativeComponent("build/NativeComponents/Native/libgothicos_native.so", kernel.abi_invoke)
echo.start()
```

See [Native Components](NativeComponents.md) for the C ABI, build recipe, and
firmware-side notes.
