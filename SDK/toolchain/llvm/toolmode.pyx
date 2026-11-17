# cython: language_level=3
"""Build modes for the gothicOS toolchain."""

MODE_DEBUG = "debug"
MODE_RELEASE = "release"
MODE_TEST = "test"
MODES = (MODE_DEBUG, MODE_RELEASE, MODE_TEST)

_FLAGS = {
    MODE_DEBUG: ("-O0", "-g"),
    MODE_RELEASE: ("-O2",),
    MODE_TEST: ("-O0", "-g", "--coverage"),
}


cpdef bint is_valid_mode(str mode):
    return mode in MODES


cpdef tuple mode_flags(str mode):
    return _FLAGS.get(mode, ())


def describe_mode(str mode):
    return {"mode": mode, "valid": is_valid_mode(mode), "flags": list(mode_flags(mode))}
