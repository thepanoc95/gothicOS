"""Application manifests and build planning for the gothicOS SDK."""

import json
import subprocess
import sys
from pathlib import Path

from .abi import SDK_VERSION_STRING, SUPPORTED_ABI, satisfies

MANIFEST_NAME = "app.json"
TEMPLATE_DIR = Path(__file__).with_name("templates")
UMBRA_VERSION = "0.1"
UMBRA_BYTECODE = "umbra-" + UMBRA_VERSION
SDK_ROOT = Path(__file__).resolve().parent.parent


def load_umbra_compiler():
    """Import the Umbra compiler, making the SDK root importable when installed."""
    sdk_root = str(SDK_ROOT)
    if sdk_root not in sys.path:
        sys.path.insert(0, sdk_root)
    from umbra import compile_file, compile_source

    return compile_file, compile_source


class AppError(Exception):
    """Invalid application manifest or failed build step."""


def load_manifest(location):
    """Load ``app.json`` from a manifest path or an application directory."""
    path = Path(location)
    if path.is_dir():
        path = path / MANIFEST_NAME
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise AppError("unable to read manifest %s: %s" % (path, error))
    except json.JSONDecodeError as error:
        raise AppError("invalid JSON in %s: %s" % (path, error))
    if not isinstance(manifest, dict):
        raise AppError("manifest must be a JSON object")
    return path, manifest


def validate_manifest(manifest):
    """Return a list of human-readable manifest errors (empty when valid)."""
    errors = []
    for key in ("name", "entry", "sources"):
        if key not in manifest:
            errors.append("missing required field %r" % key)
    name = manifest.get("name")
    if name is not None and (not isinstance(name, str) or not name.strip()):
        errors.append("'name' must be a non-empty string")
    entry = manifest.get("entry")
    if entry is not None and (not isinstance(entry, str) or not entry):
        errors.append("'entry' must be a non-empty string")
    sources = manifest.get("sources")
    if sources is not None:
        if not isinstance(sources, list) or not sources:
            errors.append("'sources' must be a non-empty list")
        elif any(not isinstance(item, str) or not item for item in sources):
            errors.append("every source must be a non-empty string")
    target = manifest.get("target", {})
    if not isinstance(target, dict):
        errors.append("'target' must be an object")
    else:
        bytecode = target.get("bytecode")
        if bytecode is not None and (
            not isinstance(bytecode, str)
            or not (bytecode.startswith("v") or bytecode.startswith("umbra-"))
        ):
            errors.append("'target.bytecode' must look like 'v6' or 'umbra-0.1'")
    for key in ("sdk", "abi"):
        value = manifest.get(key)
        if value is not None and (not isinstance(value, str) or not value):
            errors.append("%r must be a non-empty version constraint" % key)
    return errors


def check_compatibility(manifest):
    """Validate that the manifest's sdk/abi constraints include this SDK."""
    errors = validate_manifest(manifest)
    if errors:
        return errors
    sdk_spec = manifest.get("sdk", SUPPORTED_ABI)
    abi_spec = manifest.get("abi", SUPPORTED_ABI)
    try:
        sdk_ok = satisfies(SDK_VERSION_STRING, sdk_spec)
    except ValueError as error:
        return ["invalid sdk constraint: %s" % error]
    try:
        abi_ok = satisfies(SDK_VERSION_STRING, abi_spec)
    except ValueError as error:
        return ["invalid abi constraint: %s" % error]
    if not sdk_ok:
        return ["SDK version %s does not satisfy %s" % (SDK_VERSION_STRING, sdk_spec)]
    if not abi_ok:
        return ["kernel ABI %s does not satisfy %s" % (SDK_VERSION_STRING, abi_spec)]
    return []


def _render_template(name, template_name):
    source = (TEMPLATE_DIR / template_name).read_text(encoding="utf-8")
    class_name = "".join(
        character if character.isalnum() else "_" for character in name
    ).title().replace("_", "")
    return source.replace("{{NAME}}", name).replace("{{CLASS}}", class_name)


def new_app(name, directory=".", language="python"):
    """Create a minimal Python or UmbraTS app skeleton and return its directory."""
    if not isinstance(name, str) or not name:
        raise AppError("app name must be a non-empty string")
    if not all(character.isalnum() or character in "_-" for character in name):
        raise AppError("app name may contain only letters, digits, '_' and '-'")
    if language not in ("python", "umbrats", "umbra", "uts"):
        raise AppError("language must be 'python' or 'umbrats'")
    root = Path(directory) / name
    if root.exists():
        raise AppError("application directory already exists: %s" % root)
    root.mkdir(parents=True)
    if language == "python":
        source_name = "main.py"
        source_template = "main.py.tmpl"
        manifest_template = "app.json.tmpl"
    else:
        source_name = "main.uts"
        source_template = "main.uts.tmpl"
        manifest_template = "app.uts.json.tmpl"
    (root / source_name).write_text(
        _render_template(name, source_template), encoding="utf-8"
    )
    (root / MANIFEST_NAME).write_text(
        _render_template(name, manifest_template), encoding="utf-8"
    )
    return root


def plan_build(manifest, app_dir, output_dir, bytecode=None, arch=None, compiler="mpy-cross"):
    """Describe the compile actions for an app without executing them."""
    target = manifest.get("target", {})
    bytecode = bytecode if bytecode is not None else target.get("bytecode", "v6")
    arch = arch if arch is not None else target.get("arch")
    plans = []
    uses_umbra = False
    uses_python = False
    for source_name in manifest["sources"]:
        source = (Path(app_dir) / source_name).resolve()
        if not source.is_file():
            raise AppError("source file does not exist: %s" % source)
        if source.suffix == ".uts":
            uses_umbra = True
            output = Path(output_dir) / source.relative_to(Path(app_dir)).with_suffix(".umb")
            plans.append({
                "source": source,
                "output": output,
                "command": None,
                "compiler": "umbra",
                "text": source.read_text(encoding="utf-8"),
            })
        elif source.suffix == ".py":
            uses_python = True
            output = Path(output_dir) / source.relative_to(Path(app_dir)).with_suffix(".mpy")
            command = [compiler]
            if arch:
                command.append("-march=" + arch)
            command.extend(("-o", str(output), str(source)))
            plans.append({
                "source": source,
                "output": output,
                "command": tuple(command),
                "compiler": "mpy-cross",
            })
        else:
            raise AppError("unsupported source type %s: %s" % (source.suffix, source_name))
    if uses_umbra and not uses_python:
        bytecode = UMBRA_BYTECODE
    return {
        "name": manifest["name"],
        "bytecode": bytecode,
        "arch": arch,
        "actions": plans,
        "uses_umbra": uses_umbra,
        "manifest_output": Path(output_dir) / MANIFEST_NAME,
    }


def _compile_umbra(action, package_name):
    """Compile one UmbraTS source action into a ``.umb`` package."""
    try:
        _compile_file, compile_source = load_umbra_compiler()
    except ImportError as error:
        raise AppError("UmbraTS compiler is unavailable: %s" % error)
    try:
        result = compile_source(action["text"], package_name)
    except Exception as error:
        raise AppError("UmbraTS compilation failed for %s: %s" % (action["source"], error))
    action["output"].write_bytes(result.package)
    return {
        "file": action["output"].name,
        "entry": result.metadata["entry"],
        "capabilities": result.metadata["capabilities"],
        "resources": result.metadata["resources"],
    }


def build_app(location, output_dir=None, bytecode=None, arch=None, compiler="mpy-cross"):
    """Compile an app's sources into MicroPython bytecode and write a build manifest."""
    manifest_path, manifest = load_manifest(location)
    errors = check_compatibility(manifest)
    if errors:
        raise AppError("; ".join(errors))
    app_dir = manifest_path.parent
    output = Path(output_dir) if output_dir is not None else app_dir / "build"
    plan = plan_build(manifest, app_dir, output, bytecode, arch, compiler)
    output.mkdir(parents=True, exist_ok=True)
    umbra_metadata = []
    for action in plan["actions"]:
        action["output"].parent.mkdir(parents=True, exist_ok=True)
        if action["compiler"] == "umbra":
            umbra_metadata.append(_compile_umbra(action, plan["name"]))
            continue
        try:
            result = subprocess.run(action["command"], check=False)
        except OSError as error:
            raise AppError("unable to run %s: %s" % (action["command"][0], error))
        if result.returncode:
            raise AppError("compiler failed for %s" % action["source"])
    build_manifest = {
        "name": plan["name"],
        "entry": manifest["entry"],
        "abi": manifest.get("abi", SUPPORTED_ABI),
        "sdk": SDK_VERSION_STRING,
        "bytecode": plan["bytecode"],
        "arch": plan["arch"],
        "files": [
            action["output"].relative_to(output).as_posix()
            for action in plan["actions"]
        ],
    }
    if plan["uses_umbra"]:
        build_manifest["language"] = "UmbraTS"
        build_manifest["umbra"] = {
            "version": UMBRA_VERSION,
            "packages": umbra_metadata,
        }
    plan["manifest_output"].write_text(
        json.dumps(build_manifest, indent=2) + "\n", encoding="utf-8"
    )
    return build_manifest
