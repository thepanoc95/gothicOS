"""Checks for the multi-language NullKernRT stack and MicroPython chainloader."""

from __future__ import annotations

import ctypes
import importlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
KERNEL_ROOT = REPO_ROOT / "kernel"
MICROPYTHON_ROOT = REPO_ROOT / "ports" / "micropython"
SDK_INCLUDE = REPO_ROOT / "SDK" / "include"

VALUE_NONE = 0
VALUE_INT = 1
VALUE_STRING = 2

OP_PORT_OPEN = 10
OP_PORT_SEND = 12
OP_PORT_RECEIVE = 13

STATUS_OK = 0
HANDOFF_SERVICES_STARTED = 6
HANDOFF_FAILED = 7


class RawValue(ctypes.Structure):
    _fields_ = [
        ("kind", ctypes.c_int),
        ("integer", ctypes.c_int),
        ("string", ctypes.c_char_p),
    ]


class RawArguments(ctypes.Structure):
    _fields_ = [
        ("items", ctypes.POINTER(RawValue)),
        ("count", ctypes.c_uint),
    ]


class RawResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("value", RawValue),
    ]


class HandoffConfig(ctypes.Structure):
    _fields_ = [
        ("enabled", ctypes.c_int),
        ("first_managed_cpu", ctypes.c_uint),
        ("managed_cpu_count", ctypes.c_uint),
        ("init_path", ctypes.c_char * 160),
        ("shell_path", ctypes.c_char * 160),
        ("memory_limit_kb", ctypes.c_uint),
        ("debug_flags", ctypes.c_int),
        ("verbose", ctypes.c_int),
        ("root", ctypes.c_char * 160),
    ]


BIND_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint)
LOAD_FN = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint),
)
START_FN = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
)
SERVICE_FN = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_char_p)
LOG_FN = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_char_p)


class HandoffHooks(ctypes.Structure):
    _fields_ = [
        ("bind_cpu", BIND_FN),
        ("load_elf", LOAD_FN),
        ("start_init", START_FN),
        ("start_service", SERVICE_FN),
        ("log", LOG_FN),
        ("context", ctypes.c_void_p),
    ]


class Handoff(ctypes.Structure):
    _fields_ = [
        ("state", ctypes.c_int),
        ("config", HandoffConfig),
        ("hooks", HandoffHooks),
        ("bound_cpu_count", ctypes.c_uint),
        ("started_services", ctypes.c_uint),
        ("init_image", ctypes.c_void_p),
        ("init_image_size", ctypes.c_uint),
        ("error", ctypes.c_char * 160),
    ]


class NativeStackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiler = shutil.which("cc")
        cls.cxx_compiler = shutil.which("g++")
        cls.cargo = shutil.which("cargo")
        cls.build_dir = tempfile.TemporaryDirectory()
        cls.build_root = Path(cls.build_dir.name)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.build_dir.cleanup()

    def compile_shared(self, name: str, source: Path, include: Path):
        if self.compiler is None:
            self.skipTest("C compiler unavailable")
        output = self.build_root / name
        subprocess.run(
            [
                self.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fPIC", "-shared", f"-I{include}", f"-I{SDK_INCLUDE}",
                str(source), "-o", str(output),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        return ctypes.CDLL(str(output))

    def core_library(self):
        return self.compile_shared(
            "libnk_core.so",
            KERNEL_ROOT / "core" / "c" / "nk_core.c",
            KERNEL_ROOT / "core" / "include",
        )

    def handoff_library(self):
        return self.compile_shared(
            "libnk_handoff.so",
            KERNEL_ROOT / "linux" / "handoff.c",
            KERNEL_ROOT / "linux" / "include",
        )

    def test_c_core_boots_and_round_trips_a_port(self) -> None:
        library = self.core_library()
        library.nk_kernel_storage_size.restype = ctypes.c_size_t
        library.nk_kernel_init.argtypes = [
            ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_uint,
        ]
        library.nk_kernel_init.restype = ctypes.c_void_p
        library.nk_boot.argtypes = [ctypes.c_void_p]
        library.nk_boot.restype = ctypes.c_int
        library.nk_invoke.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(RawArguments),
            ctypes.POINTER(RawResult),
        ]
        library.nk_invoke.restype = ctypes.c_int

        size = library.nk_kernel_storage_size()
        storage = ctypes.create_string_buffer(size)
        kernel = library.nk_kernel_init(storage, size, 2, 1)
        self.assertTrue(kernel)
        self.assertEqual(library.nk_boot(kernel), STATUS_OK)

        name = ctypes.c_char_p(b"native.stack")
        message = ctypes.c_char_p(b"hello from C")
        items = (RawValue * 2)(
            RawValue(VALUE_STRING, 0, name),
            RawValue(VALUE_STRING, 0, message),
        )
        arguments = RawArguments(items, 2)
        result = RawResult()

        self.assertEqual(
            library.nk_invoke(kernel, OP_PORT_OPEN, ctypes.byref(arguments),
                              ctypes.byref(result)),
            STATUS_OK,
        )
        self.assertEqual(
            library.nk_invoke(kernel, OP_PORT_SEND, ctypes.byref(arguments),
                              ctypes.byref(result)),
            STATUS_OK,
        )
        received = RawArguments(items, 1)
        self.assertEqual(
            library.nk_invoke(kernel, OP_PORT_RECEIVE, ctypes.byref(received),
                              ctypes.byref(result)),
            STATUS_OK,
        )
        self.assertEqual(ctypes.string_at(result.value.string), b"hello from C")

    def test_handoff_is_opt_in_and_never_claims_cpu_zero(self) -> None:
        library = self.handoff_library()
        library.nk_handoff_parse_options.argtypes = [
            ctypes.POINTER(HandoffConfig), ctypes.c_char_p,
        ]
        library.nk_handoff_parse_options.restype = ctypes.c_int
        library.nk_handoff_configure.argtypes = [
            ctypes.POINTER(Handoff), ctypes.POINTER(HandoffConfig),
            ctypes.POINTER(HandoffHooks),
        ]
        library.nk_handoff_configure.restype = ctypes.c_int
        library.nk_handoff_start.argtypes = [ctypes.POINTER(Handoff)]
        library.nk_handoff_start.restype = ctypes.c_int

        config = HandoffConfig()
        self.assertEqual(
            library.nk_handoff_parse_options(
                ctypes.byref(config),
                b"-v -debug=0x01 -root=@flash/data -mem=200K",
            ),
            STATUS_OK,
        )
        self.assertEqual(config.debug_flags, 1)
        self.assertEqual(config.memory_limit_kb, 200)
        self.assertEqual(
            ctypes.string_at(config.init_path),
            b"@flash/data/system/init.d.elf",
        )
        self.assertEqual(
            ctypes.string_at(config.shell_path),
            b"@flash/data/user/bin/sh.uts",
        )

        bound: list[int] = []
        services: list[str] = []
        image = ctypes.create_string_buffer(b"\x7fELFfake")

        def bind_cpu(context, cpu):
            bound.append(cpu)
            return 0

        def load_elf(context, path, target, size):
            self.assertEqual(ctypes.string_at(path), b"@flash/data/system/init.d.elf")
            target[0] = ctypes.cast(image, ctypes.c_void_p).value
            size[0] = len(image.raw)
            return 0

        def start_init(context, loaded_image, image_size):
            self.assertEqual(loaded_image, ctypes.cast(image, ctypes.c_void_p).value)
            self.assertEqual(image_size, len(image.raw))
            return 0

        def start_service(context, path):
            services.append(path.decode())
            return 0

        def log(context, message):
            pass

        callbacks = (
            BIND_FN(bind_cpu), LOAD_FN(load_elf), START_FN(start_init),
            SERVICE_FN(start_service), LOG_FN(log),
        )
        config.enabled = 1
        config.first_managed_cpu = 1
        config.managed_cpu_count = 2
        hooks = HandoffHooks(*callbacks, None)
        handoff = Handoff()
        self.assertEqual(
            library.nk_handoff_configure(
                ctypes.byref(handoff), ctypes.byref(config), ctypes.byref(hooks)
            ),
            STATUS_OK,
        )
        self.assertEqual(library.nk_handoff_start(ctypes.byref(handoff)), STATUS_OK)
        self.assertEqual(handoff.state, HANDOFF_SERVICES_STARTED)
        self.assertEqual(bound, [1, 2])
        self.assertEqual(services, ["@flash/data/user/bin/sh.uts"])

        bad_config = HandoffConfig()
        bad_config.enabled = 1
        bad_config.first_managed_cpu = 0
        bad_config.managed_cpu_count = 1
        bad = Handoff()
        library.nk_handoff_configure(
            ctypes.byref(bad), ctypes.byref(bad_config), ctypes.byref(hooks)
        )
        library.nk_handoff_start(ctypes.byref(bad))
        self.assertEqual(bad.state, HANDOFF_FAILED)
        self.assertIn(b"CPU 0", ctypes.string_at(bad.error))

    def test_cpp_hal_kal_and_drivers_link(self) -> None:
        if self.compiler is None or self.cxx_compiler is None:
            self.skipTest("C/C++ compiler unavailable")
        core_object = self.build_root / "nk_core.o"
        subprocess.run(
            [
                self.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC",
                f"-I{KERNEL_ROOT / 'core' / 'include'}", f"-I{SDK_INCLUDE}",
                "-c", str(KERNEL_ROOT / "core" / "c" / "nk_core.c"),
                "-o", str(core_object),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        source = self.build_root / "cpp_smoke.cpp"
        source.write_text(
            """
#include <cassert>
#include <gothicos/drivers.hpp>
#include <gothicos/kal.hpp>

int main() {
    gothicos::kal::Kernel kernel(2, 1);
    assert(kernel.boot() == GOTHICOS_STATUS_OK);
    assert(kernel.platform()->bind_cpu(0) != 0);
    assert(kernel.platform()->bind_cpu(1) == 0);

    auto opened = kernel.invoke(GOTHICOS_OP_PORT_OPEN, {
        gothicos::kal::Value::text("cpp.stack"),
        gothicos::kal::Value::integer(4),
    });
    assert(opened.status == GOTHICOS_STATUS_OK);

    gothicos::drivers::DriverRegistry registry;
    assert(registry.install(kernel) == GOTHICOS_STATUS_OK);
    auto written = kernel.invoke(GOTHICOS_OP_DRIVER_IO, {
        gothicos::kal::Value::text("console"),
        gothicos::kal::Value::text("write"),
        gothicos::kal::Value::text("hi"),
    });
    assert(written.status == GOTHICOS_STATUS_OK);
    assert(written.value.as_int() == 2);
    return 0;
}
""",
            encoding="utf-8",
        )
        executable = self.build_root / "cpp_smoke"
        includes = [
            f"-I{KERNEL_ROOT / 'core' / 'include'}",
            f"-I{KERNEL_ROOT / 'hal' / 'cpp' / 'include'}",
            f"-I{KERNEL_ROOT / 'kal' / 'cpp' / 'include'}",
            f"-I{KERNEL_ROOT / 'drivers' / 'cpp' / 'include'}",
            f"-I{SDK_INCLUDE}",
        ]
        subprocess.run(
            [
                self.cxx_compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                *includes, str(source),
                str(KERNEL_ROOT / "hal" / "cpp" / "src" / "hal.cpp"),
                str(KERNEL_ROOT / "kal" / "cpp" / "src" / "kal.cpp"),
                str(KERNEL_ROOT / "drivers" / "cpp" / "src" / "drivers.cpp"),
                str(core_object), "-o", str(executable),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run([str(executable)], check=True, capture_output=True, text=True)

    def test_rust_workspace_tests_pass(self) -> None:
        if self.cargo is None or self.compiler is None:
            self.skipTest("Rust toolchain unavailable")
        environment = dict(os.environ)
        environment["CARGO_TARGET_DIR"] = str(self.build_root / "cargo-target")
        result = subprocess.run(
            [
                self.cargo, "test", "--workspace", "--offline",
                "--manifest-path", str(KERNEL_ROOT / "Cargo.toml"),
            ],
            cwd=REPO_ROOT,
            env=environment,
            capture_output=True,
            text=True,
            timeout=300,
        )
        self.assertEqual(
            result.returncode,
            0,
            "cargo test failed:\n" + result.stdout + "\n" + result.stderr,
        )
        self.assertIn("test result: ok", result.stdout)


class MicroPythonChainTests(unittest.TestCase):
    def test_chainloader_boots_init_elf_and_launches_umbra_shell(self) -> None:
        sys.path.insert(0, str(MICROPYTHON_ROOT))
        try:
            build = importlib.import_module("build")
            boot_config = importlib.import_module("boot_config")
            osloader = importlib.import_module("osloader")
            null_module = importlib.import_module("null")
            host_os = importlib.import_module("os")
        finally:
            sys.path.remove(str(MICROPYTHON_ROOT))

        self.assertIs(null_module.boot, osloader.boot)
        self.assertEqual(
            null_module.DEFAULT_OPTIONS,
            "-v -debug=0x01 -root=@flash/data -mem=200K",
        )
        self.assertTrue(callable(getattr(host_os, "boot", None)))
        self.assertIs(host_os.boot, osloader.boot)

        options = boot_config.BootOptions.parse(
            "-v -debug=0x01 -root=@flash/data -mem=200K"
        )
        self.assertEqual(options.resolved_init_path(), "/data/system/init.d.elf")
        self.assertEqual(options.resolved_shell_path(), "/data/user/bin/sh.uts")

        package = build._compile_shell()
        source = (MICROPYTHON_ROOT / "sh.uts").read_bytes()
        files = {
            "/data/system/init.d.elf": build._build_init_elf(package),
            "/data/user/bin/sh.uts": source,
            "/data/user/bin/sh.umb": package,
        }

        class FlashFS:
            def read(self, path):
                return files[path]

        commands = iter(["help", "info", "unknown", "exit"])

        def input_fn():
            return next(commands)

        result = null_module.boot(
            fs=FlashFS(), core_count=2, first_managed_cpu=1, input_fn=input_fn
        )
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["stage"], "shell")
        self.assertIn(".umbra", result["elf"]["sections"])
        self.assertTrue(result["elf"]["has_umbra_payload"])
        self.assertEqual(result["takeover"]["state"], "services-started")
        self.assertEqual(result["takeover"]["bound_cpus"], [1, 2])
        self.assertTrue(result["shell"]["ok"])
        self.assertEqual(result["shell"]["path"], "init.d.elf:.umbra")
        self.assertEqual(result["shell"]["state"], "STOPPED")
        self.assertEqual(result["shell"]["commands"], 4)


if __name__ == "__main__":
    unittest.main()
