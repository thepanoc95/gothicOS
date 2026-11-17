from __future__ import annotations

import json
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from gothicos import main
from gothicos.OSBuild import BuildError, build, load_build_plan


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


if __name__ == "__main__":
    unittest.main()
