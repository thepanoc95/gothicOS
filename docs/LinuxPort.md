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
