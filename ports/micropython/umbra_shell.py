"""Launch the NullKern default shell through the UmbraTS binary execution module.

The device-side shell source is `sh.uts`; its AOT output is `sh.umb`, built by the host
SDK. The loader prefers the compiled package next to the source and falls back to reading
the given path directly when it already contains Umbra bytecode.

In interactive mode, REPL lines are translated to numeric command codes and delivered on
the ``shell.in`` port. The UmbraTS task owns the command loop, prompt and output.
"""

import sys

from boot_config import BootOptions

SHELL_PORT = "shell.in"

COMMAND_EXIT = 0
COMMAND_HELP = 1
COMMAND_INFO = 2
COMMAND_ECHO = 3


def _binary_module():
    try:
        from internal.nullkern.binary import (
            BLOCKED, FAILED, STOPPED, BinaryExecutionModule,
        )
        return BLOCKED, FAILED, STOPPED, BinaryExecutionModule
    except ImportError:
        try:
            from binary import BLOCKED, FAILED, STOPPED, BinaryExecutionModule
            return BLOCKED, FAILED, STOPPED, BinaryExecutionModule
        except ImportError:
            return None, None, None, None


def _read(path, fs):
    if fs is not None and hasattr(fs, "read"):
        return fs.read(path)
    with open(path, "rb") as stream:
        return stream.read()


def _default_input():
    """Read one serial line directly; the REPL's input() is not reentrant."""
    try:
        line = sys.stdin.readline()
    except (EOFError, KeyboardInterrupt, OSError):
        return "exit"
    if not line:
        return "exit"
    return line.rstrip("\r\n")


def _shell_command(line):
    """Map a REPL line to the numeric command codes understood by sh.uts."""
    if isinstance(line, int):
        return line, None
    text = "" if line is None else str(line).strip()
    lowered = text.lower()
    if lowered in ("exit", "quit"):
        return COMMAND_EXIT, None
    if lowered in ("help", "?"):
        return COMMAND_HELP, None
    if lowered == "info":
        return COMMAND_INFO, None
    return COMMAND_ECHO, text


def launch(shell_path, kernel=None, fs=None, max_steps=4096, package=None,
           interactive=False, input_fn=None):
    path = BootOptions.resolve(shell_path)
    used = "init.d.elf:.umbra" if package is not None else None
    if package is None:
        candidates = [path]
        if path.endswith(".uts"):
            candidates.insert(0, path[:-4] + ".umb")
        for candidate in candidates:
            try:
                package = _read(candidate, fs)
                used = candidate
                break
            except (OSError, IOError):
                continue
    if package is None:
        return {"ok": False, "error": "shell package not found", "path": used}

    blocked, failed, stopped, binary_class = _binary_module()
    if binary_class is None:
        return {"ok": False, "error": "UmbraTS runtime unavailable", "path": used}

    if kernel is None or not hasattr(kernel, "binary"):
        try:
            from internal.nullkern.kernel import Kernel
        except ImportError:
            from kernel import Kernel
        kernel = Kernel()
    module = kernel.binary if hasattr(kernel, "binary") else binary_class(kernel)
    kernel.port_open(SHELL_PORT)
    task = module.spawn(package, "sh")
    reader = input_fn if input_fn is not None else _default_input
    commands = 0
    steps = 0
    while task.state not in (stopped, failed) and steps < max_steps:
        if task.state == blocked:
            if not interactive and input_fn is None:
                break
            command, text = _shell_command(reader())
            kernel.port_send(SHELL_PORT, command)
            if text is not None:
                kernel.port_send(SHELL_PORT, text)
            commands += 1
        module.step(task)
        steps += 1
    return {
        "ok": task.state == stopped,
        "path": used,
        "state": task.state,
        "steps": steps,
        "commands": commands,
        "interactive": interactive or input_fn is not None,
    }
