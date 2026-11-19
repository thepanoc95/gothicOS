"""Minimal ELF reader for NullKern init images.

Only what the chainloader needs: validate the header, walk section headers, and extract a
`.umbra` payload when one is present. Native ELF execution needs firmware support and is
deliberately out of scope here; the payload view keeps the chain testable on stock
MicroPython.
"""

import struct

ELFCLASS32 = 1
ELFCLASS64 = 2
ELFDATA2LSB = 1
SHT_NOBITS = 8


class ElfImage:
    def __init__(self, elf_class, machine, entry, sections, payload):
        self.elf_class = elf_class
        self.machine = machine
        self.entry = entry
        self.sections = sections
        self.payload = payload

    def info(self):
        return {
            "class": self.elf_class,
            "machine": self.machine,
            "entry": self.entry,
            "sections": list(self.sections.keys()),
            "has_umbra_payload": self.payload is not None,
        }


def _unpack(fmt, data, offset):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError("truncated ELF structure")
    return struct.unpack_from(fmt, data, offset)


def parse_elf(data):
    data = memoryview(data)
    if len(data) < 16 or bytes(data[:4]) != b"\x7fELF":
        raise ValueError("not an ELF image")
    elf_class = data[4]
    encoding = data[5]
    if encoding != ELFDATA2LSB:
        raise ValueError("only little-endian ELF is supported")
    # Both ELF32 and ELF64 header tuples share these positions after e_ident:
    # 0 type, 1 machine, 2 version, 3 entry, 4 phoff, 5 shoff, ...
    if elf_class == ELFCLASS32:
        header = _unpack("<HHIIIIIHHHHHH", data, 16)
        section_fmt = "<IIIIIIIIII"
    elif elf_class == ELFCLASS64:
        header = _unpack("<HHIQQQIHHHHHH", data, 16)
        section_fmt = "<IIQQQQIIQQ"
    else:
        raise ValueError("unsupported ELF class")

    machine = header[1]
    entry = header[3]
    shoff = header[5]
    shentsize = header[10]
    shnum = header[11]
    shstrndx = header[12]

    if shoff == 0 or shnum == 0 or shentsize == 0:
        return ElfImage(elf_class, machine, entry, {}, None)

    sections = {}
    raw_sections = []
    for index in range(shnum):
        offset = shoff + index * shentsize
        fields = _unpack(section_fmt, data, offset)
        raw_sections.append(fields)
    if shstrndx >= len(raw_sections):
        raise ValueError("bad ELF section name table index")
    name_table = raw_sections[shstrndx]
    names_start = name_table[4]
    names_size = name_table[5]
    if names_start + names_size > len(data):
        raise ValueError("bad ELF section name table")

    for fields in raw_sections:
        name_offset = fields[0]
        if name_offset >= names_size:
            continue
        end = name_offset
        while end < names_size and data[names_start + end] != 0:
            end += 1
        name = bytes(data[names_start + name_offset:names_start + end]).decode("utf-8")
        if name:
            sections[name] = fields

    payload = None
    umbra = sections.get(".umbra")
    if umbra is not None:
        start = umbra[4]
        size = umbra[5]
        if umbra[2] != SHT_NOBITS and start + size <= len(data):
            payload = bytes(data[start:start + size])
    return ElfImage(elf_class, machine, entry, sections, payload)
