from __future__ import annotations

import ctypes
import json
import io
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from gothicos import main
from gothicos.OSBuild import BuildError, build, load_build_plan
try:
    from .fdt_fixture import build_fdt
except ImportError:
    from fdt_fixture import build_fdt


class OSBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        (self.root / "recipes").mkdir()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_recipe(self, name: str, step: dict[str, object]) -> None:
        recipe = {
            "$schema": "../../src/gothicos/OSBuild/schema/osbuild.json",
            "@build": {"name": name, "steps": [step]},
        }
        (self.root / "recipes" / f"{name}.json").write_text(
            json.dumps(recipe), encoding="utf-8"
        )

    def write_board(self, boot_recipe: str, kernel_recipe: str) -> Path:
        board = {
            "$schema": "../schema/osbuild.json",
            "@board": {
                "name": "test-board",
                "family": "test",
                "soc": "test-soc",
                "@cpu": {"test": {"core0": "test-core"}},
                "@mem": {"size": "1K", "type": "sram", "protocol": "test"},
                "@flash": {"type": "test", "size": "1M"},
                "@/boot": {
                    "@/boot/pre-software": {
                        "presoft.kernel.path": f"%PROJROOT/{boot_recipe}"
                    }
                },
                "@/software": {
                    "@kernel": {
                        "entry0": {
                            "name": "test-kernel",
                            "type": "kernel",
                            "path": f"%PROJROOT/{kernel_recipe}",
                        }
                    }
                },
            },
        }
        path = self.root / "board.json"
        path.write_text(json.dumps(board), encoding="utf-8")
        return path

    def test_build_runs_local_steps_in_board_order(self) -> None:
        (self.root / "recipes" / "boot.py").write_text(
            "from pathlib import Path\nPath('boot-built').touch()\n", encoding="utf-8"
        )
        (self.root / "recipes" / "kernel.py").write_text(
            "from pathlib import Path\nPath('kernel-built').touch()\n", encoding="utf-8"
        )
        self.write_recipe(
            "boot",
            {
                "name": "run boot builder",
                "run": [sys.executable, "%PROJROOT/recipes/boot.py"],
            },
        )
        self.write_recipe(
            "kernel",
            {
                "name": "run kernel builder",
                "run": [sys.executable, "%PROJROOT/recipes/kernel.py"],
            },
        )
        board = self.write_board("recipes/boot.json", "recipes/kernel.json")

        actions = build(board, self.root)

        self.assertEqual([action.recipe_name for action in actions], ["boot", "kernel"])
        self.assertTrue((self.root / "boot-built").exists())
        self.assertTrue((self.root / "kernel-built").exists())

    def test_dry_run_validates_without_running_steps(self) -> None:
        self.write_recipe(
            "kernel",
            {
                "name": "create marker",
                "run": [
                    sys.executable,
                    "-c",
                    "from pathlib import Path; Path('unexpected').touch()",
                ],
            },
        )
        board = self.write_board("recipes/kernel.json", "recipes/kernel.json")

        actions = build(board, self.root, dry_run=True)

        self.assertEqual(len(actions), 1)
        self.assertFalse((self.root / "unexpected").exists())

    def test_build_accepts_standalone_build_json_from_cli(self) -> None:
        recipe = self.root / "recipes" / "standalone.json"
        recipe.write_text(
            json.dumps(
                {
                    "$schema": "../../src/gothicos/OSBuild/schema/osbuild.json",
                    "@build": {
                        "name": "standalone",
                        "steps": [
                            {
                                "name": "create marker",
                                "run": [
                                    sys.executable,
                                    "-c",
                                    "from pathlib import Path; Path('unexpected').touch()",
                                ],
                                "cwd": "%PROJROOT",
                            }
                        ],
                    },
                }
            ),
            encoding="utf-8",
        )
        output = io.StringIO()

        with redirect_stdout(output):
            exit_code = main(
                [
                    "build",
                    "--config",
                    str(recipe),
                    "--project-root",
                    str(self.root),
                    "--dry-run",
                ]
            )

        self.assertEqual(exit_code, 0)
        self.assertIn("standalone: create marker", output.getvalue())
        self.assertFalse((self.root / "unexpected").exists())

    def test_build_runs_recipe_entry_relative_to_recipe_directory(self) -> None:
        recipe_directory = self.root / "recipes" / "simple"
        recipe_directory.mkdir()
        (recipe_directory / "build.py").write_text(
            "from pathlib import Path\nPath('built').touch()\n", encoding="utf-8"
        )
        (recipe_directory / "kernel.py").write_text("answer = 42\n", encoding="utf-8")
        (recipe_directory / "oshardware.pyx").write_text(
            "def hardware():\n    return 1\n", encoding="utf-8"
        )
        recipe = recipe_directory / "build.json"
        recipe.write_text(
            json.dumps(
                {
                    "$schema": "../../src/gothicos/OSBuild/schema/osbuild.json",
                    "@build": {
                        "recipe": {
                            "name": "simple",
                            "type": "Runtime Kernel",
                            "entry": ["build.py"],
                            "outputs": {
                                "KernelCollection": {
                                    "micropython-bytecode": "v6",
                                    "source.files": ["kernel.py", "oshardware.pyx"],
                                }
                            },
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        planned_actions = build(recipe, self.root, dry_run=True)

        self.assertEqual(len(planned_actions), 3)
        self.assertFalse((recipe_directory / "built").exists())
        self.assertFalse((self.root / "build").exists())

        actions = build(recipe, self.root)

        self.assertEqual(len(actions), 3)
        self.assertEqual(actions[0].cwd, recipe_directory)
        self.assertTrue((recipe_directory / "built").exists())
        self.assertTrue(
            (
                self.root
                / "build"
                / "simple"
                / "KernelCollection"
                / "recipes"
                / "simple"
                / "kernel.mpy"
            ).is_file()
        )
        self.assertTrue(
            (
                self.root
                / "build"
                / "simple"
                / "KernelCollection"
                / "recipes"
                / "simple"
                / "oshardware.c"
            ).is_file()
        )

    def test_source_recipe_passes_mpy_cross_architecture(self) -> None:
        recipe_directory = self.root / "recipes" / "rp2"
        recipe_directory.mkdir()
        (recipe_directory / "build.py").write_text("pass\n", encoding="utf-8")
        (recipe_directory / "kernel.py").write_text("answer = 42\n", encoding="utf-8")
        recipe = recipe_directory / "build.json"
        recipe.write_text(
            json.dumps(
                {
                    "$schema": "../../src/gothicos/OSBuild/schema/osbuild.json",
                    "@build": {
                        "recipe": {
                            "name": "rp2-kernel",
                            "type": "Runtime Kernel",
                            "kernel-abi": "1.0.0",
                            "entry": ["build.py"],
                            "outputs": {
                                "KernelCollection": {
                                    "micropython-bytecode": "v6",
                                    "mpy-cross-arch": "armv6m",
                                    "source.files": ["kernel.py"],
                                }
                            },
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        actions = build(recipe, self.root, dry_run=True)

        self.assertIn("-march=armv6m", actions[1].command)
        self.assertNotIn("-b", actions[1].command)
        self.assertEqual(actions[0].env["GOTHICOS_KERNEL_ABI"], "1.0.0")
        self.assertEqual(actions[1].env["GOTHICOS_KERNEL_ABI"], "1.0.0")

    def test_board_fdt_sets_cpu_topology_for_recipe_actions(self) -> None:
        self.write_recipe("boot", {"name": "boot", "run": [sys.executable, "-c", "pass"]})
        self.write_recipe("kernel", {"name": "kernel", "run": [sys.executable, "-c", "pass"]})
        board_path = self.write_board("recipes/boot.json", "recipes/kernel.json")
        fdt_path = self.root / "board.dtb"
        fdt_path.write_bytes(build_fdt(("okay", "okay", "disabled")))
        document = json.loads(board_path.read_text(encoding="utf-8"))
        document["@board"]["@fdt"] = {"path": "%PROJROOT/board.dtb"}
        board_path.write_text(json.dumps(document), encoding="utf-8")

        actions = load_build_plan(board_path, self.root)

        self.assertEqual(len(actions), 2)
        for action in actions:
            self.assertEqual(action.env["GOTHICOS_CPU_COUNT"], "2")
            self.assertEqual(action.env["GOTHICOS_MULTICORE"], "1")
            self.assertEqual(action.env["GOTHICOS_FDT_PATH"], str(fdt_path))

    def test_missing_recipe_is_reported(self) -> None:
        board = self.write_board("recipes/missing.json", "recipes/missing.json")

        with self.assertRaisesRegex(BuildError, "Unable to resolve build recipe"):
            load_build_plan(board, self.root)

    def test_recipe_path_cannot_escape_project_root(self) -> None:
        board = self.write_board("../outside/build.json", "../outside/build.json")

        with self.assertRaisesRegex(BuildError, "must be inside project root"):
            load_build_plan(board, self.root)

    def test_nonzero_step_exit_is_reported(self) -> None:
        self.write_recipe(
            "kernel",
            {
                "name": "fail",
                "run": [sys.executable, "-c", "raise SystemExit(7)"],
            },
        )
        board = self.write_board("recipes/kernel.json", "recipes/kernel.json")

        with self.assertRaisesRegex(BuildError, "failed with exit code 7"):
            build(board, self.root)

    @unittest.skipUnless(shutil.which("cc"), "a C compiler is required for native recipe tests")
    def test_source_recipe_builds_native_shared_library(self) -> None:
        recipe_directory = self.root / "recipes" / "native"
        recipe_directory.mkdir(parents=True)
        (recipe_directory / "include").mkdir()
        (recipe_directory / "build.py").write_text("pass\n", encoding="utf-8")
        (recipe_directory / "native.c").write_text(
            "int gothicos_native_probe(void) { return 42; }\n", encoding="utf-8"
        )
        recipe = recipe_directory / "build.json"
        recipe.write_text(
            json.dumps(
                {
                    "$schema": "../../src/gothicos/OSBuild/schema/osbuild.json",
                    "@build": {
                        "recipe": {
                            "name": "native-probe",
                            "type": "Native Components",
                            "kernel-abi": "1.1.1",
                            "entry": ["build.py"],
                            "outputs": {
                                "Native": {
                                    "native.source.files": ["native.c"],
                                    "native.include.dirs": ["include"],
                                    "native.shared-library": "libprobe.so",
                                }
                            },
                        }
                    },
                }
            ),
            encoding="utf-8",
        )

        actions = build(recipe, self.root, dry_run=True)

        self.assertEqual(len(actions), 3)
        self.assertEqual(actions[1].step_name, "compile native native.c")
        self.assertEqual(actions[2].step_name, "link libprobe.so")
        self.assertIn("-shared", actions[2].command)

        build(recipe, self.root)
        library = self.root / "build" / "native-probe" / "Native" / "libprobe.so"
        self.assertTrue(library.is_file())
        self.assertEqual(ctypes.CDLL(str(library)).gothicos_native_probe(), 42)


if __name__ == "__main__":
    unittest.main()
