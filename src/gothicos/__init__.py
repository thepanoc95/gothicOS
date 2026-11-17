from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Sequence

from .OSBuild import DEFAULT_BOARD_CONFIG, BuildError, build, load_build_plan


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="gothicos", description="gothicOS build tools")
    commands = parser.add_subparsers(dest="command", required=True)

    for command in ("validate", "build"):
        subparser = commands.add_parser(
            command,
            help="validate a board config and its local build recipes"
            if command == "validate"
            else "build outputs from local recipes referenced by a board config",
        )
        subparser.add_argument(
            "-c",
            "--config",
            type=Path,
            default=DEFAULT_BOARD_CONFIG,
            help="board config or build.json recipe (default: bundled rp2-rpiw board)",
        )
        subparser.add_argument(
            "--project-root",
            type=Path,
            default=Path.cwd(),
            help="root used to resolve %%PROJROOT and relative recipe paths",
        )
        if command == "build":
            subparser.add_argument(
                "--dry-run",
                action="store_true",
                help="validate and print commands without running them",
            )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "validate":
            actions = load_build_plan(arguments.config, arguments.project_root)
            print(f"Valid configuration; {len(actions)} build step(s) found.")
        else:
            actions = build(
                arguments.config,
                arguments.project_root,
                dry_run=arguments.dry_run,
            )
            if arguments.dry_run:
                for action in actions:
                    print(f"{action.recipe_name}: {action.step_name} (cwd: {action.cwd})")
                    print(f"  {json.dumps(action.command)}")
            else:
                print(f"Build completed; ran {len(actions)} action(s).")
    except BuildError as error:
        print(f"gothicos: error: {error}", file=sys.stderr)
        return 1
    return 0