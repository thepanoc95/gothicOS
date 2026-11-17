"""Build the Cython host tools used by the gothicOS SDK and OSBuild.

This script deliberately uses ``python -m cython`` instead of setuptools so it works in
a bare ``uv`` environment where setuptools is not installed. Generated C files are
written under the requested output directory.
"""

import argparse
import subprocess
import sys
from pathlib import Path

TOOLCHAIN_DIR = Path(__file__).resolve().parent
SOURCE_FILES = (
    "llvm/_os_code_convention.pyx",
    "llvm/_os_syscalls.pyx",
    "llvm/_os_threads.pyx",
    "llvm/_os_platform_arch.pyx",
    "llvm/toolmode.pyx",
    "llvm/hostBuildCalls.pyx",
    "llvm/assemble.pyx",
)


def build_toolchain(output_dir, python=None, cython_module=None):
    """Cythonize every host tool and return the list of generated C files."""
    output = Path(output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    python = python or sys.executable
    cython_module = cython_module or ["-m", "cython"]
    generated = []
    for relative_source in SOURCE_FILES:
        source = TOOLCHAIN_DIR / relative_source
        if not source.is_file():
            raise FileNotFoundError("missing toolchain source: %s" % source)
        target = output / source.with_suffix(".c").name
        command = [str(python)] + list(cython_module) + ["-3", "-o", str(target), str(source)]
        result = subprocess.run(command, check=False)
        if result.returncode:
            raise RuntimeError("Cython failed for %s" % source.name)
        generated.append(target)
    return generated


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build gothicOS SDK host tools")
    parser.add_argument(
        "--out",
        default=str(Path.cwd() / "build" / "SDK" / "Toolchain"),
        help="directory for generated C sources",
    )
    arguments = parser.parse_args(argv)
    try:
        generated = build_toolchain(arguments.out)
    except (FileNotFoundError, OSError, RuntimeError) as error:
        print("gothicos-sdk toolchain: %s" % error, file=sys.stderr)
        return 1
    print("Generated %d C source(s) in %s" % (len(generated), arguments.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
