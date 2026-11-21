#!/usr/bin/env python3
"""Build the 512-byte-block USTAR initrd for the AMD64 test profile.

No external dependencies. Usage examples:

    python3 tools/mkinitrd.py --out ../build/NullKernel/amd64/initrd.tar
    python3 tools/mkinitrd.py /tmp/initrd.tar --kernel /tmp/kernel.elf
    python3 tools/mkinitrd.py --out /tmp/initrd.tar --src /tmp/fake-build

The archive contains the @/data/system tree expected by the tiny kernel, an
informational README under @/data/user/bin and the USB-loaded demo ELF.
"""

import argparse
import os
import sys
import time
from pathlib import Path

BLOCK = 512
ZERO_BLOCK = b"\0" * BLOCK

README = (
    b"null_kernel @/ filesystem\n"
    b"=========================\n"
    b"\n"
    b"@/ is the null_kernel namespace root. The initrd is unpacked by the\n"
    b"tiny AMD64 kernel before any ring 3 task starts.\n"
    b"\n"
    b"@/data/system/   kernel ELF, Limine config and userspace ELFs\n"
    b"@/data/user/bin/ user-provided programs and notes\n"
    b"@/usb/           USB-loaded program images (NKUSBPRG header + ELF)\n"
    b"\n"
    b"Use the nk) shell's 'ls', 'cat' and 'stat' commands to inspect it.\n"
)

FALLBACK_LIMINE = (
    b"TIMEOUT=3\n"
    b"QUIET=no\n"
    b":null_kernel (AMD64, 16MB test RAM)\n"
    b"PROTOCOL=limine\n"
    b"KERNEL_PATH=boot:///build/NullKernel/amd64/null_kernel_amd64.elf\n"
    b"MODULE_PATH=boot:///build/NullKernel/amd64/initrd.tar\n"
    b"CMDLINE=root=@/ loglevel=6 mem=16M\n"
)


def warn(message: str) -> None:
    print("mkinitrd: warning: " + message, file=sys.stderr)


def first_existing(candidates):
    for path in candidates:
        if path is not None and path.is_file():
            return path
    return None


def read_source(path: Path, label: str) -> bytes:
    """Read a file; return a clearly marked placeholder when it is absent."""
    if path is None:
        warn("no source found for " + label + "; using placeholder")
        return ("null_kernel mkinitrd: %s missing (placeholder)\n" % label).encode()
    try:
        return path.read_bytes()
    except OSError as exc:
        warn("cannot read %s: %s" % (path, exc))
        return ("null_kernel mkinitrd: %s unreadable (placeholder)\n" % label).encode()


def octal_field(value: int, digits: int) -> bytes:
    text = ("%0*o" % (digits, value))[-digits:]
    return (text + "\0").encode("ascii")


def split_name(name: str):
    """Split a path into USTAR name (<=100) and prefix (<=155) fields."""
    raw = name.encode("utf-8")
    if len(raw) <= 100:
        return b"", raw
    for i, byte in enumerate(raw):
        if byte == ord("/"):
            prefix, rest = raw[:i], raw[i + 1:]
            if len(prefix) <= 155 and len(rest) <= 100:
                return prefix, rest
    raise ValueError("USTAR path too long: " + name)


def ustar_header(name: str, size: int, mode: int, mtime: int) -> bytes:
    prefix, short_name = split_name(name)
    header = bytearray(BLOCK)
    header[0:len(short_name)] = short_name
    header[100:108] = octal_field(mode, 7)
    header[108:116] = octal_field(0, 7)       # uid
    header[116:124] = octal_field(0, 7)       # gid
    header[124:136] = octal_field(size, 11)
    header[136:148] = octal_field(mtime, 11)
    header[148:156] = b"        "             # checksum, filled below
    header[156:157] = b"0"                    # regular file
    header[257:263] = b"ustar\0"
    header[263:265] = b"00"
    header[265:269] = b"root"
    header[297:301] = b"root"
    header[329:337] = octal_field(0, 7)       # devmajor
    header[337:345] = octal_field(0, 7)       # devminor
    header[345:345 + len(prefix)] = prefix

    checksum = sum(header)
    header[148:156] = ("%06o" % checksum).encode("ascii") + b"\0 "
    return bytes(header)


def make_archive(entries, mtime: int) -> bytes:
    out = bytearray()
    for name, data in entries:
        out += ustar_header(name, len(data), 0o644, mtime)
        out += data
        out += b"\0" * ((-len(data)) % BLOCK)
    out += ZERO_BLOCK + ZERO_BLOCK
    return bytes(out)


def parse_args(argv):
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent
    default_build = repo_root / "build" / "NullKernel" / "amd64"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_pos", nargs="?", metavar="OUT",
                        help="output archive path (alternative to --out)")
    parser.add_argument("src_pos", nargs="?", metavar="SRC",
                        help="source directory (alternative to --src)")
    parser.add_argument("--out", "-o", help="output archive path")
    parser.add_argument("--src", "-s", help="directory holding build outputs")
    parser.add_argument("--kernel", help="AMD64 kernel ELF")
    parser.add_argument("--init", help="userspace init ELF")
    parser.add_argument("--shell", help="userspace shell ELF")
    parser.add_argument("--usb", help="userspace USB service ELF")
    parser.add_argument("--usb-program", "--usbprog", dest="usb_program",
                        help="USB-loaded demo ELF")
    parser.add_argument("--limine-cfg", help="Limine config to copy")
    parser.add_argument("--mtime", type=int,
                        help="fixed USTAR mtime (default: now or SOURCE_DATE_EPOCH)")
    args = parser.parse_args(argv)

    raw_out = args.out or args.out_pos
    if not raw_out:
        parser.error("an output path is required (positional OUT or --out)")
    args.out_path = Path(raw_out)
    args.src_dir = Path(args.src or args.src_pos or default_build)
    args.script_dir = script_dir
    args.repo_root = repo_root
    args.default_build = default_build
    return args


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    src = args.src_dir
    build = args.default_build

    kernel = first_existing([
        Path(args.kernel) if args.kernel else None,
        src / "null_kernel_amd64.elf",
        src / "null_kernel.elf",
        build / "null_kernel_amd64.elf",
        build / "null_kernel.elf",
    ])
    init = first_existing([
        Path(args.init) if args.init else None,
        src / "init.elf",
        build / "init.elf",
    ])
    shell = first_existing([
        Path(args.shell) if args.shell else None,
        src / "sh.elf",
        src / "shell.elf",
        build / "sh.elf",
    ])
    usb = first_existing([
        Path(args.usb) if args.usb else None,
        src / "usb.elf",
        src / "usb64.elf",
        build / "usb.elf",
    ])
    usb_program = first_existing([
        Path(args.usb_program) if args.usb_program else None,
        src / "usb" / "program.elf",
        src / "program.elf",
        build / "usb" / "program.elf",
        args.repo_root / "nullk" / "usb" / "program.elf",
    ])
    limine_cfg = first_existing([
        Path(args.limine_cfg) if args.limine_cfg else None,
        args.script_dir.parent / "limine.cfg",
        src / "limine.cfg",
    ])

    if init is None:
        warn("init ELF not found; shell and USB copies fall back to placeholders")
    shell_fallback = init
    usb_fallback = init
    usbprog_fallback = init

    entries = [
        ("data/system/null_kernel.elf",
         read_source(kernel, "data/system/null_kernel.elf")),
        ("data/system/limine.cfg",
         read_source(limine_cfg, "data/system/limine.cfg")
         if limine_cfg else FALLBACK_LIMINE),
        ("data/system/init.elf", read_source(init, "data/system/init.elf")),
        ("data/system/sh.elf", read_source(shell_fallback, "data/system/sh.elf")),
        ("data/system/usb.elf", read_source(usb_fallback, "data/system/usb.elf")),
        ("data/user/bin/README.txt", README),
        ("usb/program.elf",
         read_source(usb_program or usbprog_fallback, "usb/program.elf")),
    ]

    if args.mtime is not None:
        mtime = args.mtime
    else:
        mtime = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))

    blob = make_archive(entries, mtime)
    out = args.out_path
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)

    print("mkinitrd: %s" % out)
    for name, data in entries:
        print("  %-36s %8d bytes" % (name, len(data)))
    print("mkinitrd: %d entries, %d bytes total (%d blocks)" %
          (len(entries), len(blob), len(blob) // BLOCK))
    return 0


if __name__ == "__main__":
    sys.exit(main())
