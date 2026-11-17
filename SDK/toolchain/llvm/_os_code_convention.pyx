# cython: language_level=3
"""Calling-convention helpers for gothicOS host tools.

Syscalls are encoded into a 16-bit request word: the high byte is the opcode and the
low byte is the argument count. A native client can use this layout when marshalling a
call for a future privileged transport.
"""

cpdef int encode_syscall(int opcode, int argument_count):
    if opcode < 0 or opcode > 255 or argument_count < 0 or argument_count > 15:
        return -1
    return (opcode << 8) | argument_count


cpdef tuple decode_syscall(int packed):
    if packed < 0:
        return (-1, -1)
    return ((packed >> 8) & 0xff, packed & 0xff)


def describe():
    return {
        "name": "gothicos-syscall-v1",
        "width": 16,
        "opcode_bits": 8,
        "argument_bits": 8,
    }
