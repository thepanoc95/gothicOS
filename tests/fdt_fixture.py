import struct


def build_fdt(cpu_statuses=("okay", "okay")):
    strings = b"device_type\0status\0"
    device_type_offset = 0
    status_offset = len(b"device_type\0")
    structure = bytearray()

    def token(value):
        structure.extend(struct.pack(">I", value))

    def node(name):
        token(1)
        structure.extend(name.encode("ascii") + b"\0")
        while len(structure) % 4:
            structure.append(0)

    def prop(name_offset, value):
        token(3)
        structure.extend(struct.pack(">II", len(value), name_offset))
        structure.extend(value)
        while len(structure) % 4:
            structure.append(0)

    node("")
    node("cpus")
    for cpu_id, status in enumerate(cpu_statuses):
        node("cpu@%x" % cpu_id)
        prop(device_type_offset, b"cpu\0")
        if status != "okay":
            prop(status_offset, status.encode("ascii") + b"\0")
        token(2)
    token(2)
    token(2)
    token(9)

    reserve_offset = 40
    structure_offset = 56
    strings_offset = structure_offset + len(structure)
    total_size = strings_offset + len(strings)
    header = struct.pack(
        ">10I",
        0xD00DFEED,
        total_size,
        structure_offset,
        strings_offset,
        reserve_offset,
        17,
        16,
        0,
        len(strings),
        len(structure),
    )
    return header + (b"\0" * 16) + bytes(structure) + strings