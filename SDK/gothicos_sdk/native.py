"""Host glue for MicroPython-free native components.

A native component is a shared library exporting a
``gothicos_native_component_t`` descriptor. It receives kernel services through
a ``gothicos_invoke_fn`` function pointer; this module adapts any Python callable
with ``abi_invoke(opcode, *args) -> (status, value)`` to that pointer, so the same
library works with the nullkern kernel, the Cython Linux port, or a firmware
transport.
"""

from __future__ import annotations

import ctypes
from pathlib import Path
from typing import Any, Callable

InvokeFunction = Callable[..., tuple[int, Any]]

VALUE_NONE = 0
VALUE_INT = 1
VALUE_STRING = 2


class NativeABIError(RuntimeError):
    """Raised when a native library cannot be installed or configured."""

    def __init__(self, operation: str, status: int) -> None:
        super().__init__(f"{operation} failed with status {status}")
        self.operation = operation
        self.status = status


class _Value(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int),
        ("integer", ctypes.c_int),
        ("string", ctypes.c_char_p),
    ]


class _Arguments(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(_Value)),
        ("count", ctypes.c_uint),
    ]


class _Result(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("value", _Value),
    ]


class _Response(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("value", ctypes.c_int),
    ]


class _Component(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("init", ctypes.c_void_p),
        ("step", ctypes.c_void_p),
        ("shutdown", ctypes.c_void_p),
        ("context", ctypes.c_void_p),
    ]


_INVOKE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)


class NativeComponent:
    """Load and drive one native component through the stable C ABI."""

    def __init__(
        self,
        library: str | Path,
        abi_invoke: InvokeFunction,
        component_symbol: str = "gothicos_echo_component",
    ) -> None:
        self._library = ctypes.CDLL(str(library))
        self._abi_invoke = abi_invoke
        self._callback = _INVOKE(self._trampoline)
        self._callback_pointer = ctypes.cast(self._callback, ctypes.c_void_p)
        self._buffers: list[bytes] = []
        self._configure_prototypes()
        self._buffers.clear()
        status = self._library.gothicos_sdk_init(self._callback_pointer)
        if status:
            raise NativeABIError("gothicos_sdk_init", status)

        component_factory = getattr(self._library, component_symbol)
        component_factory.restype = ctypes.POINTER(_Component)
        component_factory.argtypes = []
        descriptor = component_factory()
        if not descriptor:
            raise NativeABIError(component_symbol, -1)
        self._component = descriptor
        self.name = descriptor.contents.name.decode("utf-8")

    def _configure_prototypes(self) -> None:
        library = self._library
        library.gothicos_sdk_init.argtypes = [ctypes.c_void_p]
        library.gothicos_sdk_init.restype = ctypes.c_int
        library.gothicos_call.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
        library.gothicos_call.restype = _Response
        library.gothicos_status_name.argtypes = [ctypes.c_int]
        library.gothicos_status_name.restype = ctypes.c_char_p
        library.gothicos_opcode_name.argtypes = [ctypes.c_int]
        library.gothicos_opcode_name.restype = ctypes.c_char_p
        library.gothicos_native_component_start.argtypes = [ctypes.POINTER(_Component)]
        library.gothicos_native_component_start.restype = ctypes.c_int
        library.gothicos_native_component_step.argtypes = [ctypes.POINTER(_Component)]
        library.gothicos_native_component_step.restype = ctypes.c_int
        library.gothicos_native_component_stop.argtypes = [ctypes.POINTER(_Component)]
        library.gothicos_native_component_stop.restype = None
        library.gothicos_native_component_run.argtypes = [
            ctypes.POINTER(_Component),
            ctypes.c_uint,
        ]
        library.gothicos_native_component_run.restype = ctypes.c_int

    def _trampoline(
        self,
        opcode: int,
        arguments_pointer: int,
        result_pointer: int,
    ) -> int:
        arguments: tuple[Any, ...] = ()
        if arguments_pointer:
            arguments_struct = ctypes.cast(arguments_pointer, ctypes.POINTER(_Arguments)).contents
            arguments = tuple(
                self._decode(arguments_struct.items[index])
                for index in range(arguments_struct.count)
            )

        status, value = self._abi_invoke(opcode, *arguments)
        if result_pointer:
            result = ctypes.cast(result_pointer, ctypes.POINTER(_Result)).contents
            result.status = status
            kind, integer, text = self._encode(value)
            result.value.kind = kind
            result.value.integer = integer
            result.value.string = text
        return status

    def _decode(self, value: _Value) -> Any:
        if value.kind == VALUE_INT:
            return value.integer
        if value.kind == VALUE_STRING:
            return value.string.decode("utf-8") if value.string else ""
        return None

    def _encode(self, value: Any) -> tuple[int, int, bytes | None]:
        if isinstance(value, bool):
            return VALUE_INT, int(value), None
        if isinstance(value, int):
            return VALUE_INT, value, None
        if isinstance(value, str):
            encoded = value.encode("utf-8")
            self._buffers.append(encoded)
            return VALUE_STRING, 0, encoded
        # Port receives return (sender_id, message); native helpers care about
        # the payload, not the sender tag.
        if isinstance(value, tuple) and len(value) == 2:
            return self._encode(value[1])
        if isinstance(value, dict) and isinstance(value.get("abi_version"), (tuple, list)):
            version = value["abi_version"]
            if len(version) == 3:
                encoded_version = (version[0] << 16) | (version[1] << 8) | version[2]
                return VALUE_INT, encoded_version, None
        return VALUE_NONE, 0, None

    def _arguments(self, values: tuple[Any, ...]) -> _Arguments | None:
        if not values:
            return None
        items = (_Value * len(values))()
        for index, value in enumerate(values):
            kind, integer, text = self._encode(value)
            items[index].kind = kind
            items[index].integer = integer
            items[index].string = text
        return _Arguments(items, len(values))

    def call(self, opcode: int, *arguments: Any) -> tuple[int, Any]:
        """Invoke one ABI operation through the C ``gothicos_call`` helper."""
        self._buffers.clear()
        packed = self._arguments(arguments)
        response = self._library.gothicos_call(
            self._callback_pointer,
            opcode,
            ctypes.byref(packed) if packed is not None else None,
        )
        return response.status, response.value

    def start(self) -> int:
        return self._library.gothicos_native_component_start(self._component)

    def step(self) -> int:
        self._buffers.clear()
        return self._library.gothicos_native_component_step(self._component)

    def stop(self) -> None:
        self._library.gothicos_native_component_stop(self._component)

    def run(self, steps: int) -> int:
        self._buffers.clear()
        return self._library.gothicos_native_component_run(self._component, steps)

    def status_name(self, status: int) -> str:
        return self._library.gothicos_status_name(status).decode("utf-8")

    def opcode_name(self, opcode: int) -> str:
        return self._library.gothicos_opcode_name(opcode).decode("utf-8")


__all__ = ["NativeABIError", "NativeComponent"]
