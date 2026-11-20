#!/usr/bin/env python3
"""Pad a raw RP2040 stage-2 image to 256 bytes and add the boot ROM checksum."""

import argparse
import binascii
import struct


def bitrev(value, width):
    return int(f"{value:0{width}b}"[::-1], 2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="raw linked stage-2 binary")
    parser.add_argument("output", help="checksummed 256-byte binary")
    args = parser.parse_args()

    with open(args.input, "rb") as src:
        data = src.read()
    if len(data) > 252:
        raise SystemExit(f"stage 2 is {len(data)} bytes; limit is 252")

    padded = data + bytes(252 - len(data))
    # RP2040 boot ROM uses a bit-reversed CRC-32 with all-ones seed.
    checksum = bitrev(
        (
            binascii.crc32(
                bytes(bitrev(byte, 8) for byte in padded),
                0,
            )
            ^ 0xFFFFFFFF
        )
        & 0xFFFFFFFF,
        32,
    )
    with open(args.output, "wb") as dst:
        dst.write(padded + struct.pack("<I", checksum))


if __name__ == "__main__":
    main()
