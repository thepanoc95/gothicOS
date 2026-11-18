"""On-device check for the native echo component's ABI port protocol.

Run after copying the nullkernRT .mpy collection to the board:

    mpremote connect /dev/ttyACM0 run native/device/echo_component_check.py

This is a MicroPython stand-in for native/components/echo/echo_component.c:
it performs the same PORT_OPEN / PORT_RECEIVE / PORT_SEND sequence, so the
port names and message shape are validated on real hardware even when the
board runs stock MicroPython firmware without a linked C component.
"""

from internal.nullkern.OSPlatform import uname
from internal.nullkern.kernel import Kernel
from internal.nullkern.abi import (
    OP_PORT_OPEN,
    OP_PORT_SEND,
    OP_PORT_RECEIVE,
    STATUS_OK,
)

INPUT = "native.echo.in"
OUTPUT = "native.echo.out"

kernel = Kernel()
assert kernel.abi.invoke(OP_PORT_OPEN, INPUT, 8)[0] == STATUS_OK
assert kernel.abi.invoke(OP_PORT_OPEN, OUTPUT, 8)[0] == STATUS_OK
assert kernel.abi.invoke(OP_PORT_SEND, INPUT, "ping")[0] == STATUS_OK

# One native step: receive from the input port, emit the echoed payload.
status, received = kernel.abi.invoke(OP_PORT_RECEIVE, INPUT, -1)
assert status == STATUS_OK
assert received[1] == "ping"
assert kernel.abi.invoke(OP_PORT_SEND, OUTPUT, "echo:" + received[1])[0] == STATUS_OK

status, echoed = kernel.abi.invoke(OP_PORT_RECEIVE, OUTPUT, -1)
assert status == STATUS_OK
assert echoed[1] == "echo:ping"
print("native echo ABI contract OK on", uname()["machine"])
