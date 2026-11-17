import struct

_FDT_MAGIC = 0xD00DFEED
_BEGIN_NODE = 1
_END_NODE = 2
_PROP = 3
_NOP = 4
_END = 9


def cpu_count_from_blob(blob):
    if len(blob) < 40:
        raise ValueError("FDT header is truncated")
    header = struct.unpack_from(">10I", blob, 0)
    if header[0] != _FDT_MAGIC:
        raise ValueError("invalid FDT magic")
    total_size = header[1]
    structure_offset = header[2]
    strings_offset = header[3]
    version = header[5]
    last_compatible_version = header[6]
    strings_size = header[8]
    structure_size = header[9]
    if version < 16 or last_compatible_version > 17:
        raise ValueError("unsupported FDT version")
    if total_size > len(blob) or total_size < 40:
        raise ValueError("FDT total size is invalid")
    if structure_offset + structure_size > total_size:
        raise ValueError("FDT structure block is outside total size")
    if strings_offset + strings_size > total_size:
        raise ValueError("FDT strings block is outside total size")

    structure_end = structure_offset + structure_size
    strings = blob[strings_offset : strings_offset + strings_size]
    position = structure_offset
    stack = []
    enabled_cpus = 0
    found_end = False
    while position + 4 <= structure_end:
        token = struct.unpack_from(">I", blob, position)[0]
        position += 4
        if token == _BEGIN_NODE:
            name_end = blob.find(b"\0", position, structure_end)
            if name_end < 0:
                raise ValueError("unterminated FDT node name")
            stack.append({
                "name": blob[position:name_end].decode("ascii"),
                "device_type": None,
                "status": None,
            })
            position = (name_end + 4) & ~3
        elif token == _END_NODE:
            if not stack:
                raise ValueError("unbalanced FDT end-node token")
            node = stack.pop()
            if (
                len(stack) == 2
                and stack[-1]["name"] == "cpus"
                and (
                    node["device_type"] == "cpu"
                    or node["name"].startswith("cpu@")
                )
                and node["status"] in (None, "okay", "ok")
            ):
                enabled_cpus += 1
        elif token == _PROP:
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
            if property_name == "device_type" or property_name == "status":
                value = blob[position:value_end].split(b"\0", 1)[0].decode("ascii")
                stack[-1][property_name] = value
            position = (value_end + 3) & ~3
        elif token == _NOP:
            continue
        elif token == _END:
            found_end = True
            break
        else:
            raise ValueError("unknown FDT structure token")

    if not found_end or stack:
        raise ValueError("FDT structure block is incomplete")
    if enabled_cpus == 0:
        raise ValueError("FDT contains no enabled CPU nodes")
    return enabled_cpus


def cpu_count_from_file(path):
    with open(path, "rb") as device_tree_file:
        return cpu_count_from_blob(device_tree_file.read())