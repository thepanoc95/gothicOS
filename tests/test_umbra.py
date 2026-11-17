import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = REPO_ROOT / "SDK"
sys.path.insert(0, str(SDK_ROOT))

from umbra import compile_source  # noqa: E402
from umbra import format as host_format  # noqa: E402
from umbra.codegen import CompileError  # noqa: E402
from umbra.lexer import LexError, tokenize  # noqa: E402
from umbra.parser import parse  # noqa: E402

from internal.nullkern.binary import format as device_format  # noqa: E402
from internal.nullkern.binary import (  # noqa: E402
    CapabilityError,
    BinaryExecutionModule,
    FAILED,
    STOPPED,
    read_package,
    run_package,
    verify,
    verify_package,
    write_package,
)
from internal.nullkern.kernel import Kernel  # noqa: E402
from gothicos_sdk.app import AppError, build_app, load_manifest, plan_build  # noqa: E402
from gothicos_sdk.cli import main as sdk_main  # noqa: E402


SIMPLE_APP = '''
@Application
class Main {
    static main(): void {
        System.log("hello");
    }
}
'''


def _sections(package):
    fields = struct.unpack_from("<4sBBBBHHHHHHHH", package, 0)
    function_offset = device_format.HEADER_SIZE
    constant_offset = function_offset + fields[7] * device_format.FUNCTION_RECORD_SIZE
    code_offset = constant_offset + fields[9]
    return {
        "fields": fields,
        "function_offset": function_offset,
        "constant_offset": constant_offset,
        "code_offset": code_offset,
        "code_size": fields[10],
    }


def _find_opcode(package, opcode):
    sections = _sections(package)
    for position in range(sections["code_size"] // device_format.INSTRUCTION_SIZE):
        offset = sections["code_offset"] + position * device_format.INSTRUCTION_SIZE
        current, operand = struct.unpack_from("<BB", package, offset)
        if current == opcode:
            return offset, operand
    raise AssertionError("opcode %d not found" % opcode)


class FrontendTests(unittest.TestCase):
    def test_lexer_identifies_tokens_and_rejects_bad_characters(self):
        tokens = tokenize("let value: uint32 = 0x10;")
        self.assertEqual([token.kind for token in tokens[:4]],
                         ["KEYWORD", "IDENT", "OP", "KEYWORD"])
        self.assertEqual(tokens[4].value, "=")
        with self.assertRaises(LexError):
            tokenize("let value = #;")

    def test_parser_builds_application_ast(self):
        program = parse(SIMPLE_APP)
        self.assertEqual(len(program.classes), 1)
        self.assertEqual(program.classes[0].name, "Main")
        self.assertEqual(program.classes[0].methods[0].name, "main")
        self.assertIn("Application",
                      [decorator["name"] for decorator in program.classes[0].decorators])

    def test_compiler_records_metadata(self):
        source = '''
@Application
@Capability("system.logging")
class Main {
    static readonly MEMORY_LIMIT: uint32 = 32768;

    static main(): void {
        System.log("hello");
    }
}
'''
        result = compile_source(source, "meta")
        self.assertEqual(result.metadata["entry"], "Main.main")
        self.assertEqual(result.metadata["capabilities"], ["system.logging"])
        self.assertEqual(result.metadata["resources"]["memory_limit"], 32768)
        self.assertEqual(result.package[:4], device_format.MAGIC)

    def test_type_errors_are_reported(self):
        with self.assertRaisesRegex(CompileError, "cannot assign to const"):
            compile_source('''
@Application
class Main {
  static main(): void {
    const value: uint32 = 1;
    value = 2;
  }
}
''')
        with self.assertRaisesRegex(CompileError, "unknown identifier"):
            compile_source('''
@Application
class Main {
  static main(): void { System.log(missing); }
}
''')
        with self.assertRaisesRegex(CompileError, "expects 2 argument"):
            compile_source('''
@Application
class Main {
  static main(): void { Port.send("topic"); }
}
''')
        with self.assertRaisesRegex(CompileError, "two string operands"):
            compile_source('''
@Application
class Main {
  static main(): void { const bad: string = "x" + 1; }
}
''')

    def test_parse_error_is_surfaced(self):
        with self.assertRaisesRegex(CompileError, "unexpected token"):
            compile_source('''
@Application
class Main {
  static main(): void { let x = ; }
}
''')


class BytecodeTests(unittest.TestCase):
    def test_host_and_runtime_format_constants_match(self):
        for name in device_format.OPCODES if hasattr(device_format, "OPCODES") else []:
            self.assertEqual(getattr(host_format, name), getattr(device_format, name))
        for name in ("MAGIC", "VERSION_MAJOR", "VERSION_MINOR", "ABI_MAJOR", "ABI_MINOR",
                     "HEADER_SIZE", "FUNCTION_RECORD_SIZE", "INSTRUCTION_SIZE",
                     "MAX_STACK_DEPTH", "MAX_CALL_DEPTH", "MAX_ARRAY_LENGTH"):
            self.assertEqual(getattr(host_format, name), getattr(device_format, name))

    def test_verifier_accepts_compiled_package(self):
        result = compile_source(SIMPLE_APP)
        verification = verify(result.package)
        self.assertTrue(verification.ok, verification.errors)
        self.assertEqual(verification.metadata["entry"], "Main.main")
        self.assertIn("hello", verification.image["constants"])

    def test_verifier_rejects_corrupted_magic_and_abi(self):
        package = bytearray(compile_source(SIMPLE_APP).package)
        package[0] = ord("X")
        self.assertFalse(verify(package).ok)

        package = bytearray(compile_source(SIMPLE_APP).package)
        package[4] = 9
        self.assertFalse(verify(package).ok)

    def test_verifier_rejects_invalid_opcode(self):
        package = bytearray(compile_source(SIMPLE_APP).package)
        offset, _operand = _find_opcode(package, device_format.OP_PUSH_CONST)
        package[offset] = 255
        result = verify(package)
        self.assertFalse(result.ok)
        self.assertTrue(any("invalid opcode" in error for error in result.errors))

    def test_verifier_rejects_invalid_jump_target(self):
        source = '''
@Application
class Main {
  static main(): void {
    let x: uint32 = 0;
    while (x < 4) { x++; }
  }
}
'''
        package = bytearray(compile_source(source).package)
        offset, _operand = _find_opcode(package, device_format.OP_JZ)
        package[offset + 1] = 255
        result = verify(package)
        self.assertFalse(result.ok)
        self.assertTrue(any("jump target" in error for error in result.errors))

    def test_verifier_rejects_stack_underflow(self):
        package = bytearray(compile_source(SIMPLE_APP).package)
        offset, _operand = _find_opcode(package, device_format.OP_PUSH_CONST)
        package[offset] = device_format.OP_POP
        package[offset + 1] = 0
        result = verify(package)
        self.assertFalse(result.ok)
        self.assertTrue(any("underflow" in error for error in result.errors))

    def test_package_file_roundtrip(self):
        package = compile_source(SIMPLE_APP).package
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.umb"
            self.assertEqual(write_package(path, package), len(package))
            loaded = read_package(path)
            self.assertEqual(loaded.name, "app")
            self.assertEqual(loaded.info()["entry"], "Main.main")
            self.assertEqual(loaded.data, package)
        with self.assertRaises(ValueError):
            verify_package(b"not an umbra package")


class VMTests(unittest.TestCase):
    def run_source(self, source, steps=200):
        logs = []
        package = compile_source(source).package
        _kernel, _module, task = run_package(package, max_steps=steps, log=logs.append)
        return logs, task

    def test_arithmetic_loops_and_branches(self):
        source = '''
@Application
class Main {
  static main(): void {
    let total: uint32 = 0;
    let index: uint32 = 0;
    while (index < 5) {
      total = total + index;
      index++;
    }
    if (total == 10) {
      System.log("ten");
    } else {
      System.log("wrong");
    }
  }
}
'''
        logs, task = self.run_source(source)
        self.assertEqual(logs, ["ten"])
        self.assertEqual(task.state, STOPPED)
        self.assertIsNone(task.error)

    def test_function_calls_and_arrays(self):
        source = '''
@Application
class Main {
  static double(value: uint32): uint32 { return value * 2; }

  static main(): void {
    let values: uint32[] = [1, 2, 3];
    let total: uint32 = 0;
    for (let index: uint32 = 0; index < 3; index++) {
      total = total + Main.double(values[index]);
    }
    System.log(total);
  }
}
'''
        logs, task = self.run_source(source)
        self.assertEqual(logs, [12])
        self.assertEqual(task.state, STOPPED)

    def test_yield_returns_to_scheduler_and_resumes(self):
        source = '''
@Application
class Main {
  static main(): void {
    System.log("before");
    yield;
    System.log("after");
  }
}
'''
        logs, task = self.run_source(source)
        self.assertEqual(logs, ["before", "after"])
        self.assertEqual(task.state, STOPPED)

    def test_native_ports_and_memory(self):
        source = '''
@Application
@Capability("port.send")
@Capability("port.receive")
@Capability("memory.alloc")
@Capability("memory.free")
class Main {
  static main(): void {
    Port.send("topic", "ping");
    const message: string = Port.receive("topic");
    System.log(message);
    const block: uint32 = Memory.alloc("blocks", "hi");
    System.log(block);
    Memory.free("blocks", block);
  }
}
'''
        kernel = Kernel()
        kernel.port_open("topic")
        kernel.pool_create("blocks", 8, 2)
        logs = []
        module = BinaryExecutionModule(kernel, log=logs.append)
        task = module.spawn(compile_source(source).package)
        for _ in range(100):
            if not module.step(task):
                break
        self.assertEqual(logs, ["ping", 1])
        self.assertEqual(kernel.port_info("topic")["messages"], 0)
        self.assertEqual(kernel.memory.stats()["blocks"]["allocated"], 0)
        self.assertEqual(task.state, STOPPED)

    def test_missing_port_fails_task(self):
        source = '''
@Application
@Capability("port.send")
class Main {
  static main(): void { Port.send("missing", "ping"); }
}
'''
        package = compile_source(source).package
        kernel = Kernel()
        module = BinaryExecutionModule(kernel, log=lambda _value: None)
        task = module.spawn(package)
        module.step(task)
        self.assertEqual(task.state, FAILED)
        self.assertIn("port", str(task.error))

    def test_undeclared_capability_is_rejected(self):
        source = '''
@Application
class Main {
  static main(): void { Port.send("topic", "ping"); }
}
'''
        with self.assertRaisesRegex(CapabilityError, "did not declare"):
            BinaryExecutionModule(Kernel()).spawn(compile_source(source).package)

    def test_ungranted_capability_is_rejected(self):
        source = '''
@Application
@Capability("memory.alloc")
class Main {
  static main(): void { Memory.alloc("blocks", "x"); }
}
'''
        kernel = Kernel()
        host_task = kernel.spawn("host", lambda _kernel, _task_id: False)
        kernel.capabilities.revoke_holder(host_task)
        module = BinaryExecutionModule(kernel)
        with self.assertRaisesRegex(CapabilityError, "capability denied"):
            module.load(compile_source(source).package, task_id=host_task)

    def test_kernel_binary_module_integration(self):
        kernel = Kernel()
        logs = []
        kernel.binary.log = logs.append
        task = kernel.binary_spawn(compile_source(SIMPLE_APP).package)
        kernel.run(max_steps=20)
        self.assertEqual(logs, ["hello"])
        self.assertEqual(kernel.binary_status(task)["state"], STOPPED)


class SDKTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_new_umbrats_app_builds_umb_package(self):
        root = self.root / "umbra-app"
        with redirect_stdout(io.StringIO()):
            self.assertEqual(sdk_main(["new", "umbra-app", "--directory", str(self.root),
                                       "--language", "umbrats"]), 0)
        manifest_path, manifest = load_manifest(root)
        self.assertEqual(manifest["entry"], "main.uts")
        self.assertEqual(json.loads(manifest_path.read_text())["target"]["bytecode"],
                         "umbra-0.1")
        self.assertTrue((root / "main.uts").is_file())
        output = self.root / "built"
        result = build_app(root, output_dir=output)
        self.assertEqual(result["bytecode"], "umbra-0.1")
        self.assertEqual(result["files"], ["main.umb"])
        self.assertTrue((output / "main.umb").is_file())
        self.assertTrue(verify((output / "main.umb").read_bytes()).ok)

    def test_plan_build_rejects_unknown_source_type(self):
        root = self.root / "bad"
        root.mkdir()
        (root / "main.txt").write_text("nope")
        manifest = {"name": "bad", "entry": "main.txt", "sources": ["main.txt"]}
        with self.assertRaisesRegex(AppError, "unsupported source type"):
            plan_build(manifest, root, self.root / "out")

    def test_cli_umbra_build_check_and_run(self):
        source = self.root / "demo.uts"
        source.write_text(SIMPLE_APP, encoding="utf-8")
        output = self.root / "demo.umb"
        with redirect_stdout(io.StringIO()):
            self.assertEqual(sdk_main(["umbra", "build", str(source),
                                       "--output", str(output)]), 0)
            self.assertEqual(sdk_main(["umbra", "check", str(output)]), 0)
            self.assertEqual(sdk_main(["umbra", "run", str(source)]), 0)
        self.assertTrue(verify(output.read_bytes()).ok)

    def test_cli_umbra_check_reports_invalid_package(self):
        source = self.root / "broken.uts"
        source.write_text("not UmbraTS", encoding="utf-8")
        with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
            self.assertEqual(sdk_main(["umbra", "check", str(source)]), 1)


if __name__ == "__main__":
    unittest.main()
