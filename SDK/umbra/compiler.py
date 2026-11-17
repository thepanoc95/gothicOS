"""End-to-end UmbraTS compiler entry point."""

import json
import struct

from . import format as fmt
from .checker import Checker
from .codegen import CodeGenerator, CompileError
from .parser import ParseError, parse


class CompileResult:
    def __init__(self, package, metadata, program, checker):
        self.package = package
        self.metadata = metadata
        self.program = program
        self.checker = checker

    @property
    def functions(self):
        return self.metadata["functions"]


def _metadata_from_package(package):
    metadata_size = struct.unpack_from("<H", package, 20)[0]
    return json.loads(package[-metadata_size:].decode("utf-8"))


def compile_source(source, package_name="app"):
    """Compile UmbraTS source text into a Umbra Bytecode package."""
    try:
        program = parse(source)
    except ParseError as error:
        raise CompileError(str(error))
    checker = Checker(program)
    errors = checker.check()
    if errors:
        raise CompileError("; ".join(str(error) for error in errors))
    package = CodeGenerator(program, checker, package_name).generate()
    return CompileResult(package, _metadata_from_package(package), program, checker)


def compile_file(path, package_name=None):
    with open(path, "r", encoding="utf-8") as source_file:
        source = source_file.read()
    return compile_source(source, package_name or path.rsplit("/", 1)[-1].rsplit(".", 1)[0])
