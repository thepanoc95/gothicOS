"""Prepare the MicroPython chainloader flash tree and compile the UmbraTS shell."""

import struct
import sys
from pathlib import Path

PORT_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = PORT_ROOT.parents[1]
SDK_ROOT = PROJECT_ROOT / "SDK"
OUTPUT_ROOT = PROJECT_ROOT / "build" / "MicroPythonPort" / "Chainloader"


def _compile_shell():
    if str(SDK_ROOT) not in sys.path:
        sys.path.insert(0, str(SDK_ROOT))
    from umbra import compile_source

    source = (PORT_ROOT / "sh.uts").read_text(encoding="utf-8")
    return compile_source(source, "sh").package


def _build_init_elf(payload):
    """Wrap the shell package as a `.umbra` section in a minimal valid ELF64 image."""
    names = b"\x00.shstrtab\x00.umbra\x00"
    shstrtab_name_offset = names.index(b".shstrtab\x00")
    umbra_name_offset = names.index(b".umbra\x00")
    shstrtab_offset = 64
    umbra_offset = shstrtab_offset + len(names)
    shoff = umbra_offset + len(payload)

    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + bytes(8)
    header = ident + struct.pack(
        "<HHIQQQIHHHHHH",
        2,       # e_type: ET_EXEC
        0xB7,    # e_machine: AArch64
        1,       # e_version
        0,       # e_entry
        0,       # e_phoff
        shoff,   # e_shoff
        0,       # e_flags
        64,      # e_ehsize
        0,       # e_phentsize
        0,       # e_phnum
        64,      # e_shentsize
        3,       # e_shnum
        1,       # e_shstrndx
    )
    null_section = bytes(64)
    shstrtab_section = struct.pack(
        "<IIQQQQIIQQ", shstrtab_name_offset, 3, 0, 0, shstrtab_offset,
        len(names), 0, 0, 1, 0
    )
    umbra_section = struct.pack(
        "<IIQQQQIIQQ", umbra_name_offset, 1, 0, 0, umbra_offset,
        len(payload), 0, 0, 1, 0
    )
    return (
        header + names + payload + null_section + shstrtab_section + umbra_section
    )


def main():
    package = _compile_shell()
    data_root = OUTPUT_ROOT / "data"
    (data_root / "system").mkdir(parents=True, exist_ok=True)
    (data_root / "user" / "bin").mkdir(parents=True, exist_ok=True)
    (data_root / "system" / "init.d.elf").write_bytes(_build_init_elf(package))
    (data_root / "user" / "bin" / "sh.uts").write_text(
        (PORT_ROOT / "sh.uts").read_text(encoding="utf-8"), encoding="utf-8"
    )
    (data_root / "user" / "bin" / "sh.umb").write_bytes(package)
    print("Prepared MicroPython chainloader image in %s" % OUTPUT_ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
