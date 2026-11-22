#!/usr/bin/env python3
"""Build a small gothicOS Linux initramfs without requiring cpio(1).

The archive is a gzip-compressed "newc" cpio image containing /init, the
native ABI v3 userspace ELF, the optional /dev/nkabi3 module and the @/ payload
expected by init64. Linux 6.6 accepts this format directly.
"""
from __future__ import annotations

import argparse
import gzip
import os
from pathlib import Path

PORT_ROOT = Path(__file__).resolve().parents[1]
CONFIG_ROOT = PORT_ROOT / "configs" / "initramfs"


def header(name: str, mode: int, size: int, mtime: int, ino: int) -> bytes:
    name_bytes = name.encode() + b"\0"
    fields = [
        b"070701",
        f"{ino:08x}".encode(),      # ino
        f"{mode:08x}".encode(),      # mode
        b"00000000",                # uid
        b"00000000",                # gid
        b"00000001",                # nlink
        f"{mtime:08x}".encode(),    # mtime
        f"{size:08x}".encode(),     # filesize
        b"00000000",                # devmajor
        b"00000000",                # devminor
        b"00000000",                # rdevmajor
        b"00000000",                # rdevminor
        f"{len(name_bytes):08x}".encode(),
        b"00000000",                # check
    ]
    raw = b"".join(fields) + name_bytes
    while len(raw) % 4:
        raw += b"\0"
    return raw


def pad(data: bytes) -> bytes:
    while len(data) % 4:
        data += b"\0"
    return data


def add_file(out: bytearray, arcname: str, path: Path, ino: int) -> int:
    data = path.read_bytes()
    mode = 0o100755 if path.name == "init" else 0o100644
    out += header(arcname.lstrip("/"), mode, len(data),
                  int(path.stat().st_mtime), ino)
    out += pad(data)
    return ino + 1


def add_dir(out: bytearray, arcname: str, ino: int) -> int:
    out += header(arcname.strip("/") + "/", 0o40755, 0, 0, ino)
    return ino + 1


def build(out_path: Path, elf: Path, module: Path | None) -> None:
    if not elf.is_file():
        raise SystemExit(f"missing userspace ELF: {elf}")

    archive = bytearray()
    ino = 1
    for directory in ["", "lib", "lib/nullk", "lib/nullk/payload", "@",
                      "@/data", "@/data/system", "@/data/user",
                      "@/data/user/bin", "dev", "proc", "sys"]:
        ino = add_dir(archive, directory, ino)

    ino = add_file(archive, "init", CONFIG_ROOT / "init", ino)
    ino = add_file(archive, "lib/nullk/nullk-linux.elf", elf, ino)

    if module is not None and module.is_file():
        ino = add_file(archive, "lib/nullk/nkabi3.ko", module, ino)
    else:
        print("warning: LKM not found; building userspace-only initramfs")

    payload = CONFIG_ROOT / "data" / "system"
    ino = add_file(archive, "lib/nullk/payload/limine.cfg",
                   payload / "limine.cfg", ino)

    amd64_elf = PORT_ROOT.parents[2] / "build" / "NullKernel" / "amd64" / \
        "null_kernel_amd64.elf"
    kernel_payload = amd64_elf if amd64_elf.is_file() else \
        payload / "null_kernel.elf"
    ino = add_file(archive, "lib/nullk/payload/null_kernel.elf",
                   kernel_payload, ino)

    archive += header("TRAILER!!!", 0, 0, 0, 0)
    while len(archive) % 512:
        archive += b"\0"

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with gzip.GzipFile(filename="", mode="wb", fileobj=open(out_path, "wb"),
                       mtime=0) as gz:
        gz.write(archive)
    print(f"wrote {out_path} ({len(archive)} bytes uncompressed)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--module", type=Path)
    args = parser.parse_args()
    module = args.module if args.module and args.module.is_file() else None
    build(args.out, args.elf, module)


if __name__ == "__main__":
    main()
