# Starting nullkernRT on a Raspberry Pi Pico W

These steps use `mpremote` against a board running stock MicroPython. The
compiled kernel is a set of `.mpy` files; there is no firmware flash needed for
this path.

## 1. Deploy the kernel collection

```sh
uv run gothicos build --config internal/nullkern/build.json
mpremote devs
mpremote connect /dev/ttyACM0 fs cp -r \
    build/nullkernRT/KernelCollection/internal/nullkern :internal/
```

If `:internal` does not exist yet, create it first:

```sh
mpremote connect /dev/ttyACM0 exec \
    "import os; os.mkdir('/internal'); os.mkdir('/internal/nullkern')"
```

Verify the install:

```sh
mpremote connect /dev/ttyACM0 exec \
    "from internal.nullkern.abi import ABI_VERSION; print(ABI_VERSION)"
# (1, 1, 1)
```

## 2. Start the kernel manually

There is no `main()` inside nullkernRT. The host/loader constructs `Kernel`,
runs the deterministic boot stages, and then pumps the scheduler:

```sh
mpremote connect /dev/ttyACM0 exec \
    "from internal.nullkern.kernel import Kernel; k = Kernel(); print(k.boot().status); print(k.run(max_steps=8))"
```

That prints `1` (boot complete) and the number of dispatched task steps.

For a longer-lived process, save a script and run it with `mpremote run`:

```python
from internal.nullkern.kernel import Kernel

kernel = Kernel()
result = kernel.boot()
assert result.ok(), result.info()

while True:
    kernel.run(max_steps=64)
```

`run(max_steps=N)` is deterministic and safe for testing. `run()` with no
argument enables the second RP2 thread; stock MicroPython on RP2040 reports
`_thread='unsafe'`, so prefer bounded steps unless you own the firmware.

## 3. Run the SDK hello app

Build it and install the two `.mpy` files at the device root:

```sh
uv run gothicos build --config SDK/examples/hello/build.json
mpremote connect /dev/ttyACM0 fs cp \
    build/hello/App/SDK/device/nullkern_sdk.mpy :nullkern_sdk.mpy
mpremote connect /dev/ttyACM0 fs cp \
    build/hello/App/SDK/examples/hello/main.mpy :main.mpy
```

Then boot the kernel, call the app's `start(kernel)` hook, and pump it:

```python
from internal.nullkern.kernel import Kernel
import main

kernel = Kernel()
kernel.boot()
print(main.start(kernel))
kernel.run(max_steps=10)
print(kernel.port_info("hello"))
```

The app convention is:

- `start(kernel)` runs once and returns whatever the loader needs.
- Spawned task callbacks have the signature `step(kernel, task_id)` and return
  `False` when finished.
- Each `kernel.run(max_steps=...)` dispatch runs one bounded step per ready task.

## 4. Autostart at power-on

MicroPython executes `/main.py` at boot. Install a launcher instead of the
example app if you want the kernel to start automatically:

```python
# main.py on the device
from internal.nullkern.kernel import Kernel
import hello_app

kernel = Kernel()
assert kernel.boot().ok()
hello_app.start(kernel)

while True:
    kernel.run(max_steps=64)
```

Copy the example app as `hello_app.mpy` rather than `main.mpy` so it does not
shadow the launcher. Be aware that an infinite loop in `main.py` keeps the REPL
busy; keep a way to interrupt it during development.

## 5. Native component check

The on-device echo check described in [Native Components](NativeComponents.md)
uses the same kernel install and validates the ABI port protocol used by the C
component:

```sh
mpremote connect /dev/ttyACM0 run native/device/echo_component_check.py
```

Stock firmware cannot load the C shared library itself; that needs a custom
MicroPython build with the C transport adapter.
