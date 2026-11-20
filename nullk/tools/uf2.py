#!/usr/bin/env python3
"""Wrap a binary image in RP2040-compatible UF2 blocks."""

import argparse
import struct

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
RP2040_FAMILY_ID = 0xE48BFF56
PAYLOAD_SIZE = 256
BLOCK_SIZE = 512


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="raw flash image")
    parser.add_argument("output", help="UF2 output file")
    parser.add_argument(
        "--target",
        type=lambda value: int(value, 0),
        default=0x10000000,
        help="load address (default: 0x10000000)",
    )
    parser.add_argument(
        "--family",
        type=lambda value: int(value, 0),
        default=RP2040_FAMILY_ID,
        help="UF2 family ID (default: RP2040)",
    )
    args = parser.parse_args()

    with open(args.input, "rb") as src:
        data = src.read()

    block_count = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    with open(args.output, "wb") as dst:
        for block_no in range(block_count):
            payload = data[
                block_no * PAYLOAD_SIZE : (block_no + 1) * PAYLOAD_SIZE
            ]
            block = bytearray(BLOCK_SIZE)
            struct.pack_into(
                "<8I",
                block,
                0,
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY_ID,
                args.target + block_no * PAYLOAD_SIZE,
                PAYLOAD_SIZE,
                block_no,
                block_count,
                args.family,
            )
            block[32 : 32 + len(payload)] = payload
            struct.pack_into("<I", block, BLOCK_SIZE - 4, UF2_MAGIC_END)
            dst.write(block)


if __name__ == "__main__":
    main()
