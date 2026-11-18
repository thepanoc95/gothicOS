from __future__ import annotations

import json
import os
import subprocess
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError

from .fdt import cpu_count_from_fdt

SCHEMA_PATH = Path(__file__).with_name("schema") / "osbuild.json"
DEFAULT_BOARD_CONFIG = Path(__file__).with_name("boards") / "rp2-rpiw.json"
PROJECT_ROOT_TOKEN = "%PROJROOT"


class BuildError(Exception):
    """A configuration or build-step error suitable for CLI output."""


@dataclass(frozen=True)
class BuildAction:
    recipe_path: Path
    recipe_name: str
    step_name: str
    command: tuple[str, ...]
    cwd: Path
    env: dict[str, str]
    output_path: Path | None = None


@lru_cache(maxsize=1)
def _validator() -> Draft202012Validator:
    try:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
    except (OSError, json.JSONDecodeError, SchemaError) as error:
        raise BuildError(f"Unable to load OSBuild schema {SCHEMA_PATH}: {error}") from error
    return Draft202012Validator(schema)


def _read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise BuildError(f"Unable to read config {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise BuildError(
            f"Invalid JSON in {path} at line {error.lineno}, column {error.colno}: {error.msg}"
        ) from error


def _validate_document(document: Any, path: Path) -> None:
    errors = sorted(
        _validator().iter_errors(document),
        key=lambda error: tuple(str(part) for part in error.absolute_path),
    )
    if errors:
        details = "; ".join(
            f"{'.'.join(str(part) for part in error.absolute_path) or '<root>'}: "
            f"{error.message}"
            for error in errors
        )
        raise BuildError(f"Invalid OSBuild config {path}: {details}")


def _resolve_project_path(value: str, project_root: Path, context: str) -> Path:
    resolved_value = value.replace(PROJECT_ROOT_TOKEN, str(project_root))
    path = Path(resolved_value)
    if not path.is_absolute():
        path = project_root / path
    resolved = path.resolve()
    if not resolved.is_relative_to(project_root):
        raise BuildError(f"{context} must be inside project root {project_root}: {value}")
    if not resolved.exists():
        raise BuildError(f"Unable to resolve {context} {value!r}: path does not exist")
    return resolved


def _collect_recipe_paths(value: Any) -> list[str]:
    paths: list[str] = []
    if isinstance(value, dict):
        for key, child in value.items():
            if (
                (key == "path" or key.endswith(".path"))
                and isinstance(child, str)
                and Path(child.replace("\\", "/")).name.endswith(".json")
            ):
                paths.append(child)
            else:
                paths.extend(_collect_recipe_paths(child))
    elif isinstance(value, list):
        for child in value:
            paths.extend(_collect_recipe_paths(child))
    return paths


def _recipe_actions(
    recipe_path: Path,
    project_root: Path,
    build_environment: dict[str, str] | None = None,
) -> list[BuildAction]:
    document = _read_json(recipe_path)
    _validate_document(document, recipe_path)
    if "@build" not in document:
        raise BuildError(
            f"Expected a local build recipe at {recipe_path}, but found a board config"
        )

    build = document["@build"]
    if "recipe" in build:
        recipe = build["recipe"]
        recipe_environment = dict(build_environment or {})
        kernel_abi = recipe.get("kernel-abi")
        if kernel_abi is not None:
            version_parts = kernel_abi.split(".")
            if len(version_parts) != 3 or any(not part.isdigit() for part in version_parts):
                raise BuildError(f"Invalid kernel ABI version {kernel_abi!r}; expected MAJOR.MINOR.PATCH")
            recipe_environment["GOTHICOS_KERNEL_ABI"] = kernel_abi
        recipe_name = recipe["name"]
        if not _is_output_component(recipe_name):
            raise BuildError(f"Recipe name is not a valid output directory name: {recipe_name!r}")

        actions: list[BuildAction] = []
        for entry in recipe["entry"]:
            entry_path = _resolve_recipe_file(
                entry,
                recipe_path,
                project_root,
                f"entry point for recipe {recipe_name!r}",
            )
            actions.append(
                BuildAction(
                    recipe_path=recipe_path,
                    recipe_name=recipe_name,
                    step_name=f"entry {entry}",
                    command=(sys.executable, str(entry_path)),
                    cwd=recipe_path.parent,
                    env=recipe_environment,
                )
            )

        for output_name, output in recipe["outputs"].items():
            if not _is_output_component(output_name):
                raise BuildError(
                    f"Output name is not a valid output directory name: {output_name!r}"
                )
            python_sources = output.get("source.files", [])
            native_sources = output.get("native.source.files", [])
            if not python_sources and not native_sources:
                raise BuildError(
                    f"Output {output_name!r} declares neither source.files "
                    f"nor native.source.files"
                )

            mpy_cross_arch = output.get("mpy-cross-arch")
            if mpy_cross_arch is not None and (
                not isinstance(mpy_cross_arch, str) or not mpy_cross_arch
            ):
                raise BuildError(
                    f"Invalid mpy-cross architecture {mpy_cross_arch!r} "
                    f"in output {output_name!r}"
                )

            if python_sources:
                bytecode_version = output.get("micropython-bytecode")
                if bytecode_version is None:
                    raise BuildError(
                        f"Output {output_name!r} declares .py sources "
                        f"but no micropython-bytecode"
                    )
                if not bytecode_version.startswith("v") or not bytecode_version[1:].isdigit():
                    raise BuildError(
                        f"Unsupported MicroPython bytecode version {bytecode_version!r} "
                        f"in output {output_name!r}; expected a version such as 'v6'"
                    )

            for source in python_sources:
                source_path = _resolve_recipe_file(
                    source,
                    recipe_path,
                    project_root,
                    f"source file for output {output_name!r}",
                )
                if source_path.suffix == ".py":
                    compiler_parts = ["mpy-cross"]
                    if mpy_cross_arch is not None:
                        compiler_parts.append("-march=" + mpy_cross_arch)
                    compiler_parts.extend(
                        ("-s", source_path.relative_to(project_root).as_posix())
                    )
                    compiler = tuple(compiler_parts)
                    output_suffix = ".mpy"
                elif source_path.suffix == ".pyx":
                    compiler = (sys.executable, "-m", "cython", "-3")
                    output_suffix = ".c"
                else:
                    raise BuildError(
                        f"Output {output_name!r} does not support source file "
                        f"{source_path.name!r}; expected .py or .pyx"
                    )

                relative_source = source_path.relative_to(project_root)
                output_path = (
                    project_root
                    / "build"
                    / recipe_name
                    / output_name
                    / relative_source.with_suffix(output_suffix)
                )
                actions.append(
                    BuildAction(
                        recipe_path=recipe_path,
                        recipe_name=recipe_name,
                        step_name=f"compile {source}",
                        command=(
                            *compiler,
                            "-o",
                            str(output_path),
                            str(source_path),
                        ),
                        cwd=project_root,
                        env=recipe_environment,
                        output_path=output_path,
                    )
                )

            object_paths: list[Path] = []
            if native_sources:
                cc = os.environ.get("GOTHICOS_CC", "cc")
                include_dirs = [
                    _resolve_recipe_directory(
                        include,
                        recipe_path,
                        project_root,
                        f"native include directory for output {output_name!r}",
                    )
                    for include in output.get("native.include.dirs", [])
                ]
                compiler_parts = [cc, "-std=c11", "-Wall", "-Wextra", "-fPIC", "-c"]
                for directory in include_dirs:
                    compiler_parts.extend(("-I", str(directory)))
                compiler = tuple(compiler_parts)

                for source in native_sources:
                    source_path = _resolve_recipe_file(
                        source,
                        recipe_path,
                        project_root,
                        f"native source file for output {output_name!r}",
                    )
                    if source_path.suffix != ".c":
                        raise BuildError(
                            f"Output {output_name!r} does not support native source "
                            f"{source_path.name!r}; expected .c"
                        )
                    relative_source = source_path.relative_to(project_root)
                    output_path = (
                        project_root
                        / "build"
                        / recipe_name
                        / output_name
                        / relative_source.with_suffix(".o")
                    )
                    object_paths.append(output_path)
                    actions.append(
                        BuildAction(
                            recipe_path=recipe_path,
                            recipe_name=recipe_name,
                            step_name=f"compile native {source}",
                            command=(*compiler, "-o", str(output_path), str(source_path)),
                            cwd=project_root,
                            env=recipe_environment,
                            output_path=output_path,
                        )
                    )

                library_name = output.get("native.shared-library")
                if library_name is not None:
                    if not _is_output_component(library_name):
                        raise BuildError(
                            f"Shared library name is not a valid output name: {library_name!r}"
                        )
                    library_path = (
                        project_root / "build" / recipe_name / output_name / library_name
                    )
                    actions.append(
                        BuildAction(
                            recipe_path=recipe_path,
                            recipe_name=recipe_name,
                            step_name=f"link {library_name}",
                            command=(
                                cc,
                                "-shared",
                                "-o",
                                str(library_path),
                                *(str(path) for path in object_paths),
                            ),
                            cwd=project_root,
                            env=recipe_environment,
                            output_path=library_path,
                        )
                    )
        return actions

    recipe_name, steps = build["name"], build["steps"]
    actions: list[BuildAction] = []
    for step in steps:
        cwd_value = step.get("cwd", str(project_root))
        cwd = _resolve_project_path(
            cwd_value,
            project_root,
            f"working directory for {step['name']}",
        )
        if not cwd.is_dir():
            raise BuildError(f"Working directory for {step['name']} is not a directory: {cwd}")

        command = tuple(
            argument.replace(PROJECT_ROOT_TOKEN, str(project_root))
            for argument in step["run"]
        )
        environment = {
            key: value.replace(PROJECT_ROOT_TOKEN, str(project_root))
            for key, value in step.get("env", {}).items()
        }
        actions.append(
            BuildAction(
                recipe_path=recipe_path,
                recipe_name=recipe_name,
                step_name=step["name"],
                command=command,
                cwd=cwd,
                env={**(build_environment or {}), **environment},
            )
        )
    return actions


def _is_output_component(value: str) -> bool:
    return bool(value) and value not in {".", ".."} and all(
        character.isalnum() or character in "._-" for character in value
    )


def _resolve_recipe_file(
    value: str,
    recipe_path: Path,
    project_root: Path,
    context: str,
) -> Path:
    resolved_value = value.replace(PROJECT_ROOT_TOKEN, str(project_root))
    path = Path(resolved_value)
    if not path.is_absolute():
        path = recipe_path.parent / path
    resolved = path.resolve()
    if not resolved.is_relative_to(project_root):
        raise BuildError(
            f"{context.capitalize()} must be inside project root {project_root}: {value}"
        )
    if not resolved.is_file():
        raise BuildError(f"Unable to resolve {context} {value!r}: file does not exist")
    return resolved


def _resolve_recipe_directory(
    value: str,
    recipe_path: Path,
    project_root: Path,
    context: str,
) -> Path:
    resolved_value = value.replace(PROJECT_ROOT_TOKEN, str(project_root))
    path = Path(resolved_value)
    if not path.is_absolute():
        path = recipe_path.parent / path
    resolved = path.resolve()
    if not resolved.is_relative_to(project_root):
        raise BuildError(
            f"{context.capitalize()} must be inside project root {project_root}: {value}"
        )
    if not resolved.is_dir():
        raise BuildError(f"Unable to resolve {context} {value!r}: directory does not exist")
    return resolved


def _board_build_environment(board: dict[str, Any], project_root: Path) -> dict[str, str]:
    board_fdt = board.get("@fdt")
    if board_fdt is not None:
        fdt_path = _resolve_project_path(board_fdt["path"], project_root, "FDT")
        try:
            cpu_count = cpu_count_from_fdt(fdt_path.read_bytes())
        except (OSError, ValueError) as error:
            raise BuildError(f"Unable to read FDT {fdt_path}: {error}") from error
        return {
            "GOTHICOS_CPU_COUNT": str(cpu_count),
            "GOTHICOS_MULTICORE": "1" if cpu_count > 1 else "0",
            "GOTHICOS_FDT_PATH": str(fdt_path),
        }

    cpu_count = sum(len(cores) for cores in board.get("@cpu", {}).values())
    environment = {"GOTHICOS_CPU_COUNT": str(cpu_count)} if cpu_count else {}
    if cpu_count:
        environment["GOTHICOS_MULTICORE"] = "1" if cpu_count > 1 else "0"
    return environment


def load_build_plan(
    config_path: Path = DEFAULT_BOARD_CONFIG,
    project_root: Path | None = None,
) -> list[BuildAction]:
    root = (project_root or Path.cwd()).resolve()
    if not root.is_dir():
        raise BuildError(f"Project root is not a directory: {root}")
    config_candidate = config_path if config_path.is_absolute() else root / config_path
    try:
        config = config_candidate.resolve(strict=True)
    except OSError as error:
        raise BuildError(f"Unable to resolve board config {config_path}: {error}") from error

    board_document = _read_json(config)
    _validate_document(board_document, config)
    if "@build" in board_document:
        return _recipe_actions(config, root)

    board = board_document["@board"]
    build_environment = _board_build_environment(board, root)
    recipe_references = _collect_recipe_paths(board)
    if not recipe_references:
        raise BuildError(f"No local JSON build recipes are referenced by {config}")

    actions: list[BuildAction] = []
    seen_recipes: set[Path] = set()
    for reference in recipe_references:
        recipe_path = _resolve_project_path(reference, root, "build recipe")
        if recipe_path in seen_recipes:
            continue
        seen_recipes.add(recipe_path)
        actions.extend(_recipe_actions(recipe_path, root, build_environment))
    return actions


def build(
    config_path: Path = DEFAULT_BOARD_CONFIG,
    project_root: Path | None = None,
    *,
    dry_run: bool = False,
) -> list[BuildAction]:
    actions = load_build_plan(config_path, project_root)
    if dry_run:
        return actions

    for action in actions:
        environment = os.environ.copy()
        environment.update(action.env)
        try:
            if action.output_path is not None:
                action.output_path.parent.mkdir(parents=True, exist_ok=True)
            result = subprocess.run(
                action.command,
                cwd=action.cwd,
                env=environment,
                check=False,
            )
        except OSError as error:
            raise BuildError(
                f"Unable to run step {action.step_name!r} in {action.recipe_path}: {error}"
            ) from error
        if result.returncode:
            raise BuildError(
                f"Step {action.step_name!r} in {action.recipe_path} "
                f"failed with exit code {result.returncode}"
            )
    return actions
