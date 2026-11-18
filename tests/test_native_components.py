import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from gothicos_sdk import NativeABIError, NativeComponent
from internal.nullkern.kernel import Kernel

REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_SOURCES = (
    "native/src/gothicos_native.c",
    "native/components/echo/echo_component.c",
)


def build_native_library(output_directory: Path) -> Path:
    compiler = shutil.which("cc")
    if compiler is None:
        raise unittest.SkipTest("a C compiler is required for native component tests")

    include_paths = (
        REPO_ROOT / "native" / "include",
        REPO_ROOT / "SDK" / "include",
    )
    objects = []
    for source in NATIVE_SOURCES:
        obj = output_directory / (Path(source).stem + ".o")
        command = [compiler, "-std=c11", "-Wall", "-Wextra", "-fPIC", "-c"]
        for include in include_paths:
            command.extend(("-I", str(include)))
        command.extend(("-o", str(obj), str(REPO_ROOT / source)))
        subprocess.run(command, check=True)
        objects.append(obj)

    library = output_directory / "libgothicos_native.so"
    subprocess.run([compiler, "-shared", "-o", str(library), *(str(obj) for obj in objects)], check=True)
    return library


@unittest.skipUnless(shutil.which("cc"), "a C compiler is required for native component tests")
class NativeComponentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls._temporary_directory = tempfile.TemporaryDirectory()
        cls.library = build_native_library(Path(cls._temporary_directory.name))

    @classmethod
    def tearDownClass(cls) -> None:
        cls._temporary_directory.cleanup()

    def setUp(self) -> None:
        self.kernel = Kernel()
        self.component = NativeComponent(self.library, self.kernel.abi.invoke)

    def test_component_registers_and_echoes_through_abi_ports(self) -> None:
        self.assertEqual(self.component.name, "native.echo")
        self.assertEqual(self.component.start(), 0)
        self.assertEqual(self.kernel.port_info("native.echo.in")["capacity"], 8)

        self.assertTrue(self.kernel.port_send("native.echo.in", "ping"))
        self.assertEqual(self.component.step(), 0)
        status, message = self.kernel.abi.invoke(13, "native.echo.out", -1)
        self.assertEqual(status, 0)
        self.assertEqual(message[1], "echo:ping")

        self.component.stop()
        with self.assertRaises(LookupError):
            self.kernel.port_info("native.echo.in")

    def test_empty_port_is_not_an_error_for_a_native_step(self) -> None:
        self.assertEqual(self.component.start(), 0)
        self.assertEqual(self.component.step(), 0)
        self.assertEqual(self.component.step(), 0)
        self.component.stop()

    def test_gothicos_call_and_name_helpers_are_implemented(self) -> None:
        self.assertEqual(self.component.call(0), (0, 0x010101))
        self.assertEqual(self.component.status_name(3), "WOULD_BLOCK")
        self.assertEqual(self.component.opcode_name(12), "PORT_SEND")
        self.assertEqual(self.component.status_name(99), "UNKNOWN")
        self.assertEqual(self.component.opcode_name(-1), "UNKNOWN")

    def test_sdk_init_rejects_unsupported_abi_version(self) -> None:
        def old_abi(opcode, *arguments):
            return 0, {"abi_version": (0, 9, 0)}

        with self.assertRaises(NativeABIError) as context:
            NativeComponent(self.library, old_abi)
        self.assertEqual(context.exception.status, 5)


if __name__ == "__main__":
    unittest.main()
