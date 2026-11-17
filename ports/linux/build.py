"""Cythonize the NULLKern Linux userspace port.

Generated C is written under the requested output directory. Compiling the Python
extension itself is handled by ``setup.py`` or by a distribution's normal build system.
"""

import argparse
import subprocess
import sys
from pathlib import Path

PORT_DIR = Path(__file__).resolve().parent
SOURCE_FILES = ("linux_port.pyx",)
HEADER_FILES = ("include/gothicos_linux_port.h",)


def build_port(output_dir, python=None, cython_module=None):
    """Translate the Linux port sources to C and return the generated paths."""
    output = Path(output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    python = python or sys.executable
    cython_module = cython_module or ["-m", "cython"]
    include_dir = PORT_DIR / "include"
    generated = []
    for relative_header in HEADER_FILES:
        if not (PORT_DIR / relative_header).is_file():
            raise FileNotFoundError("missing Linux port header: %s" % relative_header)
    for relative_source in SOURCE_FILES:
        source = PORT_DIR / relative_source
        if not source.is_file():
            raise FileNotFoundError("missing Linux port source: %s" % source)
        target = output / source.with_suffix(".c").name
        command = [str(python)] + list(cython_module) + [
            "-3", "-I", str(include_dir), "-o", str(target), str(source)
        ]
        result = subprocess.run(command, check=False)
        if result.returncode:
            raise RuntimeError("Cython failed for %s" % source.name)
        generated.append(target)
    return generated


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the gothicOS Linux port")
    parser.add_argument(
        "--out",
        default=str(Path.cwd() / "build" / "LinuxPort"),
        help="directory for generated C sources",
    )
    arguments = parser.parse_args(argv)
    try:
        generated = build_port(arguments.out)
    except (FileNotFoundError, OSError, RuntimeError) as error:
        print("gothicos Linux port: %s" % error, file=sys.stderr)
        return 1
    print("Generated %d C source(s) in %s" % (len(generated), arguments.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
