"""Command-line interface for the gothicOS SDK."""

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .abi import abi_info
from .app import (
    AppError,
    build_app,
    check_compatibility,
    load_manifest,
    load_umbra_compiler,
    new_app,
)


def _ensure_repo_root():
    """Make ``internal.nullkern`` importable when the SDK is installed elsewhere."""
    root = str(Path(__file__).resolve().parents[2])
    if root not in sys.path:
        sys.path.insert(0, root)


def _command_version(_arguments):
    print("gothicOS SDK " + __version__)
    return 0


def _command_abi(arguments):
    info = abi_info()
    if arguments.header:
        header = Path(__file__).parent.parent / "include" / "gothicos" / "kernel_abi.h"
        print(header.read_text(encoding="utf-8"), end="")
        return 0
    print(json.dumps(info, indent=2, sort_keys=True))
    return 0


def _command_check(arguments):
    try:
        _path, manifest = load_manifest(arguments.app)
        errors = check_compatibility(manifest)
    except AppError as error:
        errors = [str(error)]
    for error in errors:
        print("gothicos-sdk: " + error, file=sys.stderr)
    if errors:
        return 1
    print("Application is compatible with gothicOS SDK " + __version__)
    return 0


def _command_new(arguments):
    try:
        root = new_app(arguments.name, arguments.directory, arguments.language)
    except AppError as error:
        print("gothicos-sdk: %s" % error, file=sys.stderr)
        return 1
    print("Created " + str(root))
    return 0


def _command_build(arguments):
    try:
        manifest = build_app(
            arguments.app,
            output_dir=arguments.output,
            bytecode=arguments.bytecode,
            arch=arguments.arch,
        )
    except AppError as error:
        print("gothicos-sdk: %s" % error, file=sys.stderr)
        return 1
    print("Built %s (%d file(s))" % (manifest["name"], len(manifest["files"])))
    return 0


def _umbra_compile(source_path, name=None):
    compile_file, _compile_source = load_umbra_compiler()
    return compile_file(str(source_path), name)


def _umbra_package(arguments):
    source = Path(arguments.source)
    if source.suffix == ".uts":
        result = _umbra_compile(source, arguments.name)
        return result.package, result.metadata
    if source.suffix == ".umb":
        return source.read_bytes(), None
    raise AppError("Umbra source must be a .uts or .umb file")


def _command_umbra_build(arguments):
    source = Path(arguments.source)
    try:
        package, metadata = _umbra_package(arguments)
        output = Path(arguments.output) if arguments.output else source.with_suffix(".umb")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(package)
        if arguments.metadata:
            Path(arguments.metadata).write_text(
                json.dumps(metadata or {}, indent=2) + "\n", encoding="utf-8"
            )
    except (AppError, OSError, Exception) as error:
        print("gothicos-sdk: %s" % error, file=sys.stderr)
        return 1
    print("Built %s (%d bytes)" % (output, len(package)))
    return 0


def _command_umbra_check(arguments):
    try:
        package, _metadata = _umbra_package(arguments)
        try:
            _ensure_repo_root()
            from internal.nullkern.binary import verify
        except ImportError:
            print("Umbra source is valid; runtime verifier is not installed")
            return 0
        result = verify(package)
        if not result.ok:
            for error in result.errors:
                print("gothicos-sdk: " + error, file=sys.stderr)
            return 1
    except Exception as error:
        print("gothicos-sdk: %s" % error, file=sys.stderr)
        return 1
    print("Umbra package is valid")
    return 0


def _command_umbra_run(arguments):
    try:
        package, _metadata = _umbra_package(arguments)
        _ensure_repo_root()
        from internal.nullkern.binary import run_package
    except Exception as error:
        print("gothicos-sdk: %s" % error, file=sys.stderr)
        return 1
    _kernel, _module, task = run_package(package, max_steps=arguments.steps)
    print("Umbra task %s" % task.state)
    if task.error is not None:
        print("gothicos-sdk: %s" % task.error, file=sys.stderr)
        return 1
    return 0


def _build_parser():
    parser = argparse.ArgumentParser(prog="gothicos-sdk", description="gothicOS app SDK")
    commands = parser.add_subparsers(dest="command")
    commands.add_parser("version", help="print SDK version").set_defaults(handler=_command_version)
    abi = commands.add_parser("abi", help="print ABI metadata or the C header")
    abi.add_argument("--header", action="store_true", help="print kernel_abi.h")
    abi.set_defaults(handler=_command_abi)
    check = commands.add_parser("check", help="check an app manifest")
    check.add_argument("app", help="manifest file or application directory")
    check.set_defaults(handler=_command_check)
    new = commands.add_parser("new", help="create a new application")
    new.add_argument("name")
    new.add_argument("--directory", default=".", help="parent directory")
    new.add_argument("--language", default="python", choices=("python", "umbrats"),
                     help="source language for the new app")
    new.set_defaults(handler=_command_new)
    build = commands.add_parser("build", help="compile an application")
    build.add_argument("app", help="manifest file or application directory")
    build.add_argument("--output", help="output directory")
    build.add_argument("--bytecode", help="MicroPython bytecode version, e.g. v6")
    build.add_argument("--arch", help="mpy-cross architecture, e.g. armv6m")
    build.set_defaults(handler=_command_build)

    umbra = commands.add_parser("umbra", help="UmbraTS compiler tools")
    umbra_commands = umbra.add_subparsers(dest="umbra_command")
    umbra_build = umbra_commands.add_parser("build", help="compile .uts to .umb")
    umbra_build.add_argument("source", help=".uts source or .umb package")
    umbra_build.add_argument("--output", help="output .umb path")
    umbra_build.add_argument("--name", help="application package name")
    umbra_build.add_argument("--metadata", help="write package metadata as JSON")
    umbra_build.set_defaults(handler=_command_umbra_build)
    umbra_check = umbra_commands.add_parser("check", help="verify an Umbra package")
    umbra_check.add_argument("source", help=".uts source or .umb package")
    umbra_check.add_argument("--name", help="application package name")
    umbra_check.set_defaults(handler=_command_umbra_check)
    umbra_run = umbra_commands.add_parser("run", help="run an Umbra package on a fresh kernel")
    umbra_run.add_argument("source", help=".uts source or .umb package")
    umbra_run.add_argument("--name", help="application package name")
    umbra_run.add_argument("--steps", type=int, default=1000, help="maximum VM steps")
    umbra_run.set_defaults(handler=_command_umbra_run)
    return parser


def main(argv=None):
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    handler = getattr(arguments, "handler", None)
    if handler is None:
        parser.print_help()
        return 0
    return handler(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
