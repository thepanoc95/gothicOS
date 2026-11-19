"""MicroPython OS loader behind the canonical `null.boot()` API.

Use the small `null` facade on devices:

    import null
    null.boot('-v -debug=0x01 -root=@flash/data -mem=200K')

Importing `osloader` also installs `boot` on the `os` module as a compatibility alias.
MicroPython's built-in `os` module is immutable on some ports, so a proxy is placed in
`sys.modules` as a fallback. Prefer `null.boot()` and import `osloader` before `os` when
using the compatibility alias.

Files may be read through an injected `fs` object with a `read(path)` method, which keeps
the whole chain testable on CPython without a device filesystem. On a device (no injected
`fs`) `null.boot()` also bridges REPL input to the UmbraTS shell; command loops can be
driven from tests with `input_fn`.
"""

import os as _os
import sys

import boot_config
import elf
import null_kernel
import umbra_shell

BOOT_OPTIONS = "-v -debug=0x01 -root=@flash/data -mem=200K"


def read_file(path, fs=None):
    if fs is not None and hasattr(fs, "read"):
        return fs.read(path)
    with open(path, "rb") as stream:
        return stream.read()


def boot(option_string=BOOT_OPTIONS, fs=None, core_count=2, first_managed_cpu=1,
         interactive=None, input_fn=None):
    options = boot_config.BootOptions.parse(option_string)
    init_path = options.resolved_init_path()

    image = read_file(init_path, fs)
    init_elf = elf.parse_elf(image)

    takeover = null_kernel.takeover(
        options, image, core_count=core_count, first_managed_cpu=first_managed_cpu
    )
    if not takeover.ok():
        return {
            "ok": False,
            "stage": "takeover",
            "boot": options.info(),
            "elf": init_elf.info(),
            "takeover": takeover.info(),
        }

    if interactive is None:
        interactive = fs is None
    shell = umbra_shell.launch(
        options.shell_path(), takeover.core, fs, package=init_elf.payload,
        interactive=interactive, input_fn=input_fn
    )
    return {
        "ok": shell.get("ok", False),
        "stage": "shell",
        "boot": options.info(),
        "elf": init_elf.info(),
        "takeover": takeover.info(),
        "shell": shell,
    }


class _OsProxy:
    """Fallback for MicroPython ports whose built-in `os` module is immutable."""

    def __init__(self, original):
        self._original = original

    def __getattr__(self, name):
        return getattr(self._original, name)

    def boot(self, option_string=BOOT_OPTIONS, fs=None, core_count=2,
             first_managed_cpu=1, interactive=None, input_fn=None):
        return boot(option_string, fs, core_count, first_managed_cpu,
                    interactive, input_fn)


def install():
    """Make `os.boot(...)` available, including on immutable-module MicroPython ports."""
    try:
        _os.boot = boot
        return True
    except (AttributeError, TypeError):
        try:
            current = sys.modules.get("os", _os)
            if not isinstance(current, _OsProxy):
                sys.modules["os"] = _OsProxy(current)
            return True
        except Exception:
            return False


install()
