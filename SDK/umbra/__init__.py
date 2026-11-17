"""UmbraTS compiler for gothicOS."""

from .compiler import CompileResult, compile_file, compile_source
from .format import VERSION_MAJOR, VERSION_MINOR

__version__ = "0.1.0"

__all__ = [
    "CompileResult",
    "compile_file",
    "compile_source",
    "VERSION_MAJOR",
    "VERSION_MINOR",
]
