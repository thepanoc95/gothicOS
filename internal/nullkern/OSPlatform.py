"""Platform identification for nullkern runtime guests."""

import os

try:
    import machine
except ImportError:
    machine = None

try:
    from .fdt import cpu_count_from_file
except ImportError:
    from fdt import cpu_count_from_file


def _os_platform_model():
    """Return the host description provided by ``os.uname()`` as a dictionary."""
    try:
        info = os.uname()
    except (AttributeError, OSError):
        return {
            "sysname": "unknown",
            "nodename": "unknown",
            "release": "unknown",
            "version": "unknown",
            "machine": "unknown",
        }
    if isinstance(info, tuple):
        return {
            "sysname": info[0],
            "nodename": info[1],
            "release": info[2],
            "version": info[3],
            "machine": info[4],
        }
    return {
        "sysname": info.sysname,
        "nodename": info.nodename,
        "release": info.release,
        "version": info.version,
        "machine": info.machine,
    }


def uname(fdt_path=None, core_count=1):
    """Return a normalized platform description.

    ``fdt_path`` may point to a device tree blob on the host or device; when supplied,
    the enabled CPU count replaces ``core_count``.
    """
    description = _os_platform_model()
    description["arch"] = description["machine"].lower()
    description["core_count"] = core_count
    description["multicore"] = core_count > 1
    description["micropython"] = hasattr(os, "uname") and description["sysname"] == "MicroPython"
    if fdt_path is not None:
        description["core_count"] = cpu_count_from_file(fdt_path)
        description["multicore"] = description["core_count"] > 1
        description["fdt_path"] = fdt_path
    return description


def describe(fdt_path=None, core_count=1):
    return uname(fdt_path, core_count)
