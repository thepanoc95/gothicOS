import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

REPO_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = REPO_ROOT / "SDK"
sys.path.insert(0, str(SDK_ROOT))

from gothicos_sdk import KernelClient, KernelError, abi_info  # noqa: E402
from gothicos_sdk.app import (  # noqa: E402
    AppError,
    build_app,
    check_compatibility,
    load_manifest,
    new_app,
    plan_build,
    validate_manifest,
)
from gothicos_sdk.abi import (  # noqa: E402
    ABI_VERSION,
    OP_BOOT_INFO,
    OP_MEM_POOL_CREATE,
    OP_PORT_OPEN,
    STATUS_OK,
    satisfies,
)
from gothicos_sdk.cli import main as sdk_main  # noqa: E402

from internal.nullkern.abi import ABI_VERSION as KERNEL_ABI_VERSION  # noqa: E402
from internal.nullkern.kernel import Kernel  # noqa: E402


class SDKAbiTests(unittest.TestCase):
    def test_sdk_abi_constants_match_kernel(self):
        self.assertEqual(ABI_VERSION, KERNEL_ABI_VERSION)
        self.assertTrue(satisfies("1.1.0", ">=1.0.0,<2.0.0"))
        self.assertFalse(satisfies("2.0.0", ">=1.0.0,<2.0.0"))

    def test_abi_info_exposes_operations_and_statuses(self):
        info = abi_info()
        self.assertEqual(info["abi_version"], "1.1.1")
        self.assertIn("PORT_OPEN", info["operations"].values())
        self.assertIn("PERMISSION_DENIED", info["statuses"].values())


class SDKClientTests(unittest.TestCase):
    def test_client_wraps_real_kernel(self):
        client = KernelClient.from_kernel(Kernel())
        info = client.get_info()
        self.assertEqual(info["abi_version"], ABI_VERSION)
        task = client.create_task("worker", lambda _kernel, _id: False)
        self.assertEqual(task.info()["name"], "worker")
        self.assertEqual(client.yield_step(), task.task_id)
        self.assertEqual(task.state(), 4)

    def test_client_ports_and_memory(self):
        client = KernelClient.from_kernel(Kernel())
        port = client.open_port("topic", 4)
        self.assertIsNone(port.send("hello"))
        self.assertEqual(port.info()["messages"], 1)
        pool = client.create_pool("blocks", 8, 1)
        block = pool.allocate(b"abc")
        self.assertEqual(block.read(), b"abc\x00\x00\x00\x00\x00")
        block.write(b"xy")
        self.assertEqual(block.read(2), b"xy")
        block.release()

    def test_checked_raises_kernel_error(self):
        client = KernelClient.from_kernel(Kernel())
        with self.assertRaises(KernelError) as caught:
            client.checked(OP_PORT_OPEN, "topic", 1, "extra")
        self.assertEqual(caught.exception.status, 1)

    def test_boot_info_is_available_to_clients(self):
        client = KernelClient.from_kernel(Kernel())
        info = client.boot_info()
        self.assertIn("status", info)

    def test_client_subsystems_and_remote_calls(self):
        client = KernelClient.from_kernel(Kernel())
        client.register_ability("echo", "test", [], invoke=lambda arguments: arguments)
        client.add_node("phone-b", "phone-b")
        client.remote_route("echo", "phone-b")

        self.assertEqual(client.start_ability("echo")["state_name"], "RUNNING")
        self.assertEqual(client.remote_call("echo", ["hello"]), ["hello"])
        self.assertEqual(client.schedule("echo")["node_id"], "phone-b")


class SDKAppTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_new_app_writes_templates(self):
        root = new_app("demo", str(self.root))
        manifest_path, manifest = load_manifest(root)
        self.assertTrue((root / "main.py").is_file())
        self.assertEqual(manifest["name"], "demo")
        self.assertEqual(manifest["entry"], "main.py")
        self.assertEqual(check_compatibility(manifest), [])
        self.assertIn("demo", manifest_path.read_text(encoding="utf-8"))

    def test_validate_manifest_reports_missing_fields(self):
        errors = validate_manifest({"name": "broken"})
        self.assertIn("missing required field 'entry'", errors)
        self.assertIn("missing required field 'sources'", errors)

    def test_plan_build_forwards_architecture(self):
        root = new_app("demo", str(self.root))
        _path, manifest = load_manifest(root)
        plan = plan_build(manifest, root, self.root / "out")
        command = plan["actions"][0]["command"]
        self.assertIn("-march=armv6m", command)
        self.assertTrue(command[0].endswith("mpy-cross"))

    def test_build_app_writes_manifest_with_mocked_compiler(self):
        root = new_app("demo", str(self.root))
        with patch("gothicos_sdk.app.subprocess.run", return_value=subprocess.CompletedProcess((), 0)):
            result = build_app(root, output_dir=self.root / "out")
        self.assertEqual(result["name"], "demo")
        self.assertEqual(result["bytecode"], "v6")
        self.assertEqual(result["files"], ["main.mpy"])
        self.assertTrue((self.root / "out" / "app.json").is_file())

    def test_compiler_failure_is_reported(self):
        root = new_app("demo", str(self.root))
        with patch("gothicos_sdk.app.subprocess.run", return_value=subprocess.CompletedProcess((), 3)):
            with self.assertRaisesRegex(AppError, "compiler failed"):
                build_app(root, output_dir=self.root / "out")

    def test_cli_new_check_and_build(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = sdk_main(["new", "cliapp", "--directory", str(self.root)])
        self.assertEqual(code, 0)
        with redirect_stdout(output):
            code = sdk_main(["check", str(self.root / "cliapp")])
        self.assertEqual(code, 0)
        with patch("gothicos_sdk.app.subprocess.run", return_value=subprocess.CompletedProcess((), 0)):
            with redirect_stdout(output):
                code = sdk_main(["build", str(self.root / "cliapp"), "--output", str(self.root / "built")])
        self.assertEqual(code, 0)
        self.assertIn("Built cliapp", output.getvalue())

    def test_cli_abi_header_is_available(self):
        output = io.StringIO()
        with redirect_stdout(output):
            code = sdk_main(["abi", "--header"])
        self.assertEqual(code, 0)
        self.assertIn("GOTHICOS_ABI_VERSION", output.getvalue())

    def test_toolchain_source_list_matches_disk(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "toolchain_build", SDK_ROOT / "toolchain" / "build.py"
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for name in module.SOURCE_FILES:
            self.assertTrue((SDK_ROOT / "toolchain" / name).is_file(), name)
        self.assertEqual(len(module.SOURCE_FILES), 7)


if __name__ == "__main__":
    unittest.main()
