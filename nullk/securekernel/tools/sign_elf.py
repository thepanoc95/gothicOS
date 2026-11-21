#!/usr/bin/env python3
"""Inject or verify a SecureKernel NKSIGv1 signature in an ELF64 image.

The signature lives in an extra section with no name (sh_name = 0). Its bytes
are excluded from the SHA-256 and HMAC-SHA256 digest, so verification can simply
hash the file with that section skipped.

Usage:
    sign_elf.py sign   input.elf output.elf
    sign_elf.py verify input.elf

Set NK_SECUREKERNEL_KEY to override the development key. A 64-character hex
string is decoded as bytes; any other value is used as ASCII.
"""

import hashlib
import hmac
import os
import struct
import sys

MAGIC = b"NKSIGv1\x00"
SIG_SIZE = 88
DEFAULT_KEY = b"nullkernel-dev-signing-key-0001x"


def key_bytes():
    value = os.environ.get("NK_SECUREKERNEL_KEY")
    if value is None:
        return DEFAULT_KEY
    if len(value) == 64:
        try:
            return bytes.fromhex(value)
        except ValueError:
            pass
    return value.encode("utf-8")


def align8(value):
    return (value + 7) & ~7


def read_header(data):
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not a little-endian ELF64 file")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_type != 2 or e_machine != 62:
        raise ValueError("SecureKernel signs ET_EXEC x86-64 images only")
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 58)
    if shnum == 0 or shentsize < 64 or shoff == 0:
        raise ValueError("ELF has no usable section header table")
    if shoff + shnum * 64 > len(data):
        raise ValueError("section header table is out of bounds")
    return shoff, shentsize, shnum


def canonical_bytes(data, sig_offset):
    """Hash everything except the 88-byte signature payload."""
    return data[:sig_offset] + data[sig_offset + SIG_SIZE:]


def sign(data):
    shoff, _shentsize, shnum = read_header(data)
    old_table = bytearray(data[shoff:shoff + shnum * 64])

    new_shoff = align8(len(data))
    new_shdr = new_shoff + shnum * 64
    sig_offset = align8(new_shdr + 64)
    result_size = sig_offset + SIG_SIZE

    out = bytearray(data)
    out.extend(b"\x00" * (result_size - len(out)))
    out[shoff:shoff + len(old_table)] = b"\x00" * len(old_table)
    out[new_shoff:new_shoff + len(old_table)] = old_table

    struct.pack_into("<Q", out, 40, new_shoff)
    struct.pack_into("<H", out, 60, shnum + 1)

    # sh_name=0, SHT_PROGBITS, sh_offset=sig_offset, sh_size=88, align=8.
    struct.pack_into("<IIQQQQIIQQ", out, new_shdr,
                     0, 1, 0, 0, sig_offset, SIG_SIZE, 0, 0, 8, 0)

    canonical = bytes(out[:sig_offset])
    digest = hashlib.sha256(canonical).digest()
    mac = hmac.new(key_bytes(), canonical, hashlib.sha256).digest()
    struct.pack_into("<8sIIQ32s32s", out, sig_offset,
                     MAGIC, 1, 0, sig_offset, digest, mac)
    return bytes(out)


def find_signature(data):
    shoff, shentsize, shnum = read_header(data)
    for index in range(shnum):
        sh = data[shoff + index * shentsize:shoff + index * shentsize + 64]
        if len(sh) < 64:
            continue
        _name, sh_type, _flags, _addr, offset, size = struct.unpack_from(
            "<IIQQQQ", sh, 0)
        if size < SIG_SIZE or offset + SIG_SIZE > len(data):
            continue
        if data[offset:offset + 8] == MAGIC:
            return offset
    return None


def verify(data):
    offset = find_signature(data)
    if offset is None:
        return False, "unsigned image"
    version, _flags, image_size = struct.unpack_from("<IIQ", data, offset + 8)
    digest = data[offset + 24:offset + 56]
    mac = data[offset + 56:offset + 88]
    if version != 1 or image_size != len(data) - SIG_SIZE:
        return False, "invalid signature header"
    canonical = canonical_bytes(data, offset)
    expected_digest = hashlib.sha256(canonical).digest()
    expected_mac = hmac.new(key_bytes(), canonical, hashlib.sha256).digest()
    if not hmac.compare_digest(digest, expected_digest):
        return False, "SHA-256 mismatch"
    if not hmac.compare_digest(mac, expected_mac):
        return False, "HMAC mismatch"
    return True, "NKSIGv1 signature verified"


def main(argv):
    if len(argv) == 4 and argv[1] == "sign":
        with open(argv[2], "rb") as handle:
            data = handle.read()
        if find_signature(data) is not None:
            print("error: image is already signed", file=sys.stderr)
            return 2
        try:
            result = sign(data)
        except ValueError as exc:
            print("error: {0}".format(exc), file=sys.stderr)
            return 4
        with open(argv[3], "wb") as handle:
            handle.write(result)
        print("signed {0} -> {1}".format(argv[2], argv[3]))
        return 0
    if len(argv) == 3 and argv[1] == "verify":
        with open(argv[2], "rb") as handle:
            data = handle.read()
        try:
            ok, message = verify(data)
        except ValueError as exc:
            print("error: {0}".format(exc), file=sys.stderr)
            return 4
        print(message)
        return 0 if ok else 1
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
