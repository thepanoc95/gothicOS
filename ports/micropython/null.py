"""NullKern boot facade for MicroPython.

Canonical device API:

    import null
    null.boot('-v -debug=0x01 -root=@flash/data -mem=200K')

On a device, `null.boot()` starts an interactive UmbraTS shell; type `help` for commands
and `exit` to return to the MicroPython REPL. `os.boot()` remains available as a
compatibility alias, but `null.boot()` is the supported entry point.
"""

from osloader import BOOT_OPTIONS, boot

DEFAULT_OPTIONS = BOOT_OPTIONS

__all__ = ["BOOT_OPTIONS", "DEFAULT_OPTIONS", "boot"]
