#!/usr/bin/env python3
"""Build a small gothicOS Linux initramfs without requiring cpio(1).

The archive is a gzip-compressed "newc" cpio image containing a static C /init,
the native ABI v3 userspace ELF, optional kernel modules (including the Intel
i915 framebuffer/DRM driver) and the @/ payload expected by init64. /lib/init
is an alias for existing Limine entries that pass init=/lib/init. Linux 6.6
accepts this format directly.
"""
from __future__ import annotations

import argparse
import gzip
from pathlib import Path

PORT_ROOT = Path(__file__).resolve().parents[1]
CONFIG_ROOT = PORT_ROOT / "configs" / "initramfs"
MODULE_ROOT = "lib/modules/6.6.162-nk"
FIRMWARE_ROOT = "lib/firmware/i915"
KERNEL_MODULE_LIST = CONFIG_ROOT / "kernel-modules.txt"


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


def add_data(out: bytearray, arcname: str, data: bytes, ino: int,
             mode: int = 0o100644) -> int:
    out += header(arcname.lstrip("/"), mode, len(data), 0, ino)
    out += pad(data)
    return ino + 1


def add_file(out: bytearray, arcname: str, path: Path, ino: int,
             mode: int | None = None) -> int:
    data = path.read_bytes()
    if mode is None:
        mode = 0o100755 if arcname in ("init", "lib/init",
                                        "lib/nullk/nullk-linux.elf") else 0o100644
    out += header(arcname.lstrip("/"), mode, len(data),
                  int(path.stat().st_mtime), ino)
    out += pad(data)
    return ino + 1


def add_dir(out: bytearray, arcname: str, ino: int) -> int:
    out += header(arcname.strip("/") + "/", 0o40755, 0, 0, ino)
    return ino + 1


def read_kernel_modules(module_dir: Path | None) -> list[tuple[str, str, Path]]:
    """Return (name, args, source path) tuples from the module manifest."""
    if module_dir is None or not KERNEL_MODULE_LIST.is_file():
        return []

    modules: list[tuple[str, str, Path]] = []
    for raw in KERNEL_MODULE_LIST.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 1)
        name = parts[0]
        args = parts[1].strip() if len(parts) > 1 else ""
        matches = sorted(module_dir.rglob(name))
        if not matches:
            raise SystemExit(
                f"kernel module {name} not found under {module_dir}")
        modules.append((name, args, matches[0]))
    return modules


def read_i915_firmware(firmware_dir: Path | None) -> list[Path]:
    """Return i915 firmware files to place under /lib/firmware/i915."""
    if firmware_dir is None:
        return []
    source = firmware_dir / "i915"
    if not source.is_dir():
        return []
    return sorted(path for path in source.rglob("*") if path.is_file())


def build(out_path: Path, init: Path, elf: Path, module: Path | None,
          module_dir: Path | None, firmware_dir: Path | None) -> None:
    if not init.is_file():
        raise SystemExit(f"missing static init: {init}")
    if not elf.is_file():
        raise SystemExit(f"missing userspace ELF: {elf}")

    kernel_modules = read_kernel_modules(module_dir)
    firmware = read_i915_firmware(firmware_dir)

    archive = bytearray()
    ino = 1
    for directory in ["", "lib", "lib/modules", MODULE_ROOT, "lib/firmware",
                      FIRMWARE_ROOT, "lib/nullk", "lib/nullk/payload", "@",
                      "@/data", "@/data/system", "@/data/user",
                      "@/data/user/bin", "dev", "proc", "sys"]:
        ino = add_dir(archive, directory, ino)

    ino = add_file(archive, "init", init, ino)
    ino = add_file(archive, "lib/init", init, ino)
    ino = add_file(archive, "lib/nullk/nullk-linux.elf", elf, ino)

    if module is not None and module.is_file():
        ino = add_file(archive, "lib/nullk/nkabi3.ko", module, ino)
    else:
        print("warning: LKM not found; building userspace-only initramfs")

    manifest = "".join(f"{name} {args}\n" for name, args, _ in kernel_modules)
    ino = add_data(archive, "lib/nullk/kernel-modules",
                   manifest.encode(), ino)
    for name, _, path in kernel_modules:
        ino = add_file(archive, f"{MODULE_ROOT}/{name}", path, ino)
    if kernel_modules:
        print("kernel modules: " + ", ".join(name for name, _, _ in kernel_modules))
    else:
        print("warning: no kernel modules packaged; pass --module-dir KDIR")
    for path in firmware:
        ino = add_file(archive, f"{FIRMWARE_ROOT}/{path.name}", path, ino)
    if firmware:
        print(f"i915 firmware: {len(firmware)} file(s)")

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
    parser.add_argument("--init", type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--module", type=Path)
    parser.add_argument("--module-dir", type=Path,
                        help="built Linux tree; modules listed in "
                             "configs/initramfs/kernel-modules.txt are copied")
    parser.add_argument("--firmware-dir", type=Path,
                        help="firmware root; i915/* is copied when available")
    args = parser.parse_args()
    init = args.init or args.elf.parent / "initramfs-init"
    module = args.module if args.module and args.module.is_file() else None
    module_dir = args.module_dir if args.module_dir and \
        args.module_dir.is_dir() else None
    if args.module_dir and module_dir is None:
        print(f"warning: kernel module dir not found: {args.module_dir}")
    firmware_dir = args.firmware_dir if args.firmware_dir and \
        args.firmware_dir.is_dir() else None
    build(args.out, init, args.elf, module, module_dir, firmware_dir)


if __name__ == "__main__":
    main()
