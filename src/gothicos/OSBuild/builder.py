from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError

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


def _load_recipe(path: Path) -> tuple[str, list[dict[str, Any]]]:
    document = _read_json(path)
    _validate_document(document, path)
    if "@build" not in document:
        raise BuildError(f"Expected a local build recipe at {path}, but found a board config")
    build = document["@build"]
    return build["name"], build["steps"]


def _recipe_actions(
    recipe_path: Path,
    project_root: Path,
) -> list[BuildAction]:
    recipe_name, steps = _load_recipe(recipe_path)
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
                env=environment,
            )
        )
    return actions


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

    recipe_references = _collect_recipe_paths(board_document["@board"])
    if not recipe_references:
        raise BuildError(f"No local JSON build recipes are referenced by {config}")

    actions: list[BuildAction] = []
    seen_recipes: set[Path] = set()
    for reference in recipe_references:
        recipe_path = _resolve_project_path(reference, root, "build recipe")
        if recipe_path in seen_recipes:
            continue
        seen_recipes.add(recipe_path)
        actions.extend(_recipe_actions(recipe_path, root))
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
