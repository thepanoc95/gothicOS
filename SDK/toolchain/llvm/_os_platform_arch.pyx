# cython: language_level=3
"""Architecture names used by the gothicOS toolchain."""

ARCH_ARMV6M = "armv6m"
ARCH_ARMV7M = "armv7m"
ARCH_X64 = "x64"
ARCH_UNKNOWN = "unknown"

_ALIASES = {
    "rp2": ARCH_ARMV6M,
    "rp2040": ARCH_ARMV6M,
    "cortex-m0": ARCH_ARMV6M,
    "cortex-m0plus": ARCH_ARMV6M,
    "cortex-m4": ARCH_ARMV7M,
    "cortex-m7": ARCH_ARMV7M,
    "x86_64": ARCH_X64,
    "amd64": ARCH_X64,
}


cpdef str normalize_arch(str name):
    if name is None:
        return ARCH_UNKNOWN
    lowered = name.strip().lower()
    return _ALIASES.get(lowered, lowered if lowered in (ARCH_ARMV6M, ARCH_ARMV7M, ARCH_X64) else ARCH_UNKNOWN)


cpdef str mpy_arch(str name):
    normalized = normalize_arch(name)
    if normalized == ARCH_UNKNOWN:
        return None
    return normalized


def describe_arch(str name):
    normalized = normalize_arch(name)
    return {
        "input": name,
        "arch": normalized,
        "mpy_cross_arch": mpy_arch(name),
        "supported": normalized != ARCH_UNKNOWN,
    }
