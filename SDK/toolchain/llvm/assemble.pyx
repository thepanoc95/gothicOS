# cython: language_level=3
"""Command assembly helpers for the gothicOS toolchain."""

from shlex import quote


cpdef str quote_argument(str value):
    return quote(str(value))


def assemble_command(compiler, sources, output, arch=None, bytecode=None):
    command = [str(compiler)]
    if arch:
        command.append("-march=" + arch)
    if bytecode:
        command.append("-b=" + bytecode)
    for source in sources:
        command.append(str(source))
    if output is not None:
        command.extend(("-o", str(output)))
    return tuple(command)


def format_command(command):
    return " ".join(quote_argument(str(part)) for part in command)
