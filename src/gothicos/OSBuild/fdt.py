from __future__ import annotations

import struct

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9
FDT_HEADER_SIZE = 40


def cpu_count_from_fdt(blob: bytes) -> int:
    if len(blob) < FDT_HEADER_SIZE:
        raise ValueError("FDT header is truncated")

    (
        magic,
        total_size,
        structure_offset,
        strings_offset,
        _reserved_offset,
        version,
        last_compatible_version,
        _boot_cpu,
        strings_size,
        structure_size,
    ) = struct.unpack_from(">10I", blob, 0)
    if magic != FDT_MAGIC:
        raise ValueError("invalid FDT magic")
    if version < 16 or last_compatible_version > 17:
        raise ValueError("unsupported FDT version")
    if total_size > len(blob) or total_size < FDT_HEADER_SIZE:
        raise ValueError("FDT total size is invalid")
    if (
        structure_offset + structure_size > total_size
        or strings_offset + strings_size > total_size
    ):
        raise ValueError("FDT block is outside the declared total size")

    structure_end = structure_offset + structure_size
    strings = blob[strings_offset : strings_offset + strings_size]
    position = structure_offset
    stack: list[dict[str, object]] = []
    enabled_cpus = 0
    found_end = False

    while position + 4 <= structure_end:
        token = struct.unpack_from(">I", blob, position)[0]
        position += 4

        if token == FDT_BEGIN_NODE:
            name_end = blob.find(b"\0", position, structure_end)
            if name_end < 0:
                raise ValueError("unterminated FDT node name")
            name = blob[position:name_end].decode("ascii")
            position = (name_end + 4) & ~3
            stack.append({"name": name, "device_type": None, "status": None})
        elif token == FDT_END_NODE:
            if not stack:
                raise ValueError("unbalanced FDT end-node token")
            node = stack.pop()
            if (
                len(stack) == 2
                and stack[-1]["name"] == "cpus"
                and (
                    node["device_type"] == "cpu"
                    or str(node["name"]).startswith("cpu@")
                )
                and node["status"] in (None, "okay", "ok")
            ):
                enabled_cpus += 1
        elif token == FDT_PROP:
            if not stack or position + 8 > structure_end:
                raise ValueError("malformed FDT property")
            property_size, name_offset = struct.unpack_from(">II", blob, position)
            position += 8
            value_end = position + property_size
            if value_end > structure_end or name_offset >= len(strings):
                raise ValueError("FDT property is outside its data block")
            property_end = strings.find(b"\0", name_offset)
            if property_end < 0:
                raise ValueError("unterminated FDT property name")
            property_name = strings[name_offset:property_end].decode("ascii")
            if property_name in {"device_type", "status"}:
                value = blob[position:value_end].split(b"\0", 1)[0].decode("ascii")
                stack[-1][property_name] = value
            position = (value_end + 3) & ~3
        elif token == FDT_NOP:
            continue
        elif token == FDT_END:
            found_end = True
            break
        else:
            raise ValueError(f"unknown FDT structure token {token}")

    if not found_end or stack:
        raise ValueError("FDT structure block is incomplete")
    if enabled_cpus == 0:
        raise ValueError("FDT contains no enabled CPU nodes")
    return enabled_cpus