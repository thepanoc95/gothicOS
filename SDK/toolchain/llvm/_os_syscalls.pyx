# cython: language_level=3
"""Syscall names and request construction for host-side tooling."""

STATUS_NAMES = {
    0: "OK", 1: "INVALID_ARGUMENT", 2: "NOT_FOUND", 3: "WOULD_BLOCK",
    4: "QUEUE_FULL", 5: "UNSUPPORTED", 6: "INTERNAL_ERROR",
    7: "PERMISSION_DENIED", 8: "ALREADY_EXISTS", 9: "OUT_OF_MEMORY",
}

OPCODE_NAMES = {
    0: "GET_INFO", 1: "TASK_CREATE", 2: "TASK_STOP", 3: "TASK_RESUME",
    4: "TASK_STATE", 5: "IPC_SEND", 6: "IPC_RECEIVE", 7: "YIELD", 8: "RUN",
    9: "TASK_INFO", 10: "PORT_OPEN", 11: "PORT_CLOSE", 12: "PORT_SEND",
    13: "PORT_RECEIVE", 14: "PORT_INFO", 15: "MEM_POOL_CREATE",
    16: "MEM_ALLOC", 17: "MEM_FREE", 18: "MEM_WRITE", 19: "MEM_READ",
    20: "IRQ_REGISTER", 21: "IRQ_UNREGISTER", 22: "IRQ_RAISE",
    23: "IRQ_SERVICE", 24: "DRIVER_INFO", 25: "DRIVER_IO", 26: "CAP_GRANT",
    27: "CAP_REVOKE", 28: "CAP_INFO", 29: "BOOT_INFO",
}


cpdef str status_name(int status):
    return STATUS_NAMES.get(status, "UNKNOWN")


cpdef str opcode_name(int opcode):
    return OPCODE_NAMES.get(opcode, "UNKNOWN")


def build_request(int opcode, arguments):
    if opcode not in OPCODE_NAMES:
        raise ValueError("unknown syscall opcode")
    return {"opcode": opcode, "name": opcode_name(opcode), "arguments": list(arguments)}


def invoke(opcode, arguments, invoke=None):
    request = build_request(opcode, arguments)
    if invoke is None:
        return request
    return invoke(request["opcode"], *request["arguments"])
