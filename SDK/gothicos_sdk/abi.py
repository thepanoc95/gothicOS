"""Mirror of the nullkernRT v1 ABI for SDK clients.

The constants below are kept in sync with ``internal/nullkern/abi.py``. The SDK never
imports the kernel at runtime, so applications can be built on a host that has no
kernel tree installed.
"""

SDK_VERSION = (1, 1, 1)
SDK_VERSION_STRING = "1.1.1"
ABI_VERSION = (1, 1, 1)
SUPPORTED_ABI = ">=1.0.0,<2.0.0"

STATUS_OK = 0
STATUS_INVALID_ARGUMENT = 1
STATUS_NOT_FOUND = 2
STATUS_WOULD_BLOCK = 3
STATUS_QUEUE_FULL = 4
STATUS_UNSUPPORTED = 5
STATUS_INTERNAL_ERROR = 6
STATUS_PERMISSION_DENIED = 7
STATUS_ALREADY_EXISTS = 8
STATUS_OUT_OF_MEMORY = 9

STATUS_NAMES = {
    STATUS_OK: "OK",
    STATUS_INVALID_ARGUMENT: "INVALID_ARGUMENT",
    STATUS_NOT_FOUND: "NOT_FOUND",
    STATUS_WOULD_BLOCK: "WOULD_BLOCK",
    STATUS_QUEUE_FULL: "QUEUE_FULL",
    STATUS_UNSUPPORTED: "UNSUPPORTED",
    STATUS_INTERNAL_ERROR: "INTERNAL_ERROR",
    STATUS_PERMISSION_DENIED: "PERMISSION_DENIED",
    STATUS_ALREADY_EXISTS: "ALREADY_EXISTS",
    STATUS_OUT_OF_MEMORY: "OUT_OF_MEMORY",
}

OP_GET_INFO = 0
OP_TASK_CREATE = 1
OP_TASK_STOP = 2
OP_TASK_RESUME = 3
OP_TASK_STATE = 4
OP_IPC_SEND = 5
OP_IPC_RECEIVE = 6
OP_YIELD = 7
OP_RUN = 8
OP_TASK_INFO = 9
OP_PORT_OPEN = 10
OP_PORT_CLOSE = 11
OP_PORT_SEND = 12
OP_PORT_RECEIVE = 13
OP_PORT_INFO = 14
OP_MEM_POOL_CREATE = 15
OP_MEM_ALLOC = 16
OP_MEM_FREE = 17
OP_MEM_WRITE = 18
OP_MEM_READ = 19
OP_IRQ_REGISTER = 20
OP_IRQ_UNREGISTER = 21
OP_IRQ_RAISE = 22
OP_IRQ_SERVICE = 23
OP_DRIVER_INFO = 24
OP_DRIVER_IO = 25
OP_CAP_GRANT = 26
OP_CAP_REVOKE = 27
OP_CAP_INFO = 28
OP_BOOT_INFO = 29
OP_ABILITY_REGISTER = 30
OP_ABILITY_START = 31
OP_ABILITY_STOP = 32
OP_ABILITY_STATE = 33
OP_ABILITY_INFO = 34
OP_SOFTBUS_SEND = 35
OP_SOFTBUS_SUBSCRIBE = 36
OP_SOFTBUS_INFO = 37
OP_REMOTE_CALL = 38
OP_REMOTE_ROUTE = 39
OP_NODE_REGISTER = 40
OP_NODE_INFO = 41
OP_SCHEDULE = 42
OP_DISTRIBUTED_INFO = 43

OP_NAMES = {
    OP_GET_INFO: "GET_INFO",
    OP_TASK_CREATE: "TASK_CREATE",
    OP_TASK_STOP: "TASK_STOP",
    OP_TASK_RESUME: "TASK_RESUME",
    OP_TASK_STATE: "TASK_STATE",
    OP_IPC_SEND: "IPC_SEND",
    OP_IPC_RECEIVE: "IPC_RECEIVE",
    OP_YIELD: "YIELD",
    OP_RUN: "RUN",
    OP_TASK_INFO: "TASK_INFO",
    OP_PORT_OPEN: "PORT_OPEN",
    OP_PORT_CLOSE: "PORT_CLOSE",
    OP_PORT_SEND: "PORT_SEND",
    OP_PORT_RECEIVE: "PORT_RECEIVE",
    OP_PORT_INFO: "PORT_INFO",
    OP_MEM_POOL_CREATE: "MEM_POOL_CREATE",
    OP_MEM_ALLOC: "MEM_ALLOC",
    OP_MEM_FREE: "MEM_FREE",
    OP_MEM_WRITE: "MEM_WRITE",
    OP_MEM_READ: "MEM_READ",
    OP_IRQ_REGISTER: "IRQ_REGISTER",
    OP_IRQ_UNREGISTER: "IRQ_UNREGISTER",
    OP_IRQ_RAISE: "IRQ_RAISE",
    OP_IRQ_SERVICE: "IRQ_SERVICE",
    OP_DRIVER_INFO: "DRIVER_INFO",
    OP_DRIVER_IO: "DRIVER_IO",
    OP_CAP_GRANT: "CAP_GRANT",
    OP_CAP_REVOKE: "CAP_REVOKE",
    OP_CAP_INFO: "CAP_INFO",
    OP_BOOT_INFO: "BOOT_INFO",
    OP_ABILITY_REGISTER: "ABILITY_REGISTER",
    OP_ABILITY_START: "ABILITY_START",
    OP_ABILITY_STOP: "ABILITY_STOP",
    OP_ABILITY_STATE: "ABILITY_STATE",
    OP_ABILITY_INFO: "ABILITY_INFO",
    OP_SOFTBUS_SEND: "SOFTBUS_SEND",
    OP_SOFTBUS_SUBSCRIBE: "SOFTBUS_SUBSCRIBE",
    OP_SOFTBUS_INFO: "SOFTBUS_INFO",
    OP_REMOTE_CALL: "REMOTE_CALL",
    OP_REMOTE_ROUTE: "REMOTE_ROUTE",
    OP_NODE_REGISTER: "NODE_REGISTER",
    OP_NODE_INFO: "NODE_INFO",
    OP_SCHEDULE: "SCHEDULE",
    OP_DISTRIBUTED_INFO: "DISTRIBUTED_INFO",
}

CAP_TASKS = 1
CAP_IPC = 2
CAP_MULTICORE = 4
CAP_SUBSYSTEMS = 8
CAP_DISTRIBUTED = 16

ABILITY_STOPPED = 0
ABILITY_STARTING = 1
ABILITY_RUNNING = 2
ABILITY_STOPPING = 3
ABILITY_FAILED = 4

ABILITY_STATE_NAMES = {
    ABILITY_STOPPED: "STOPPED",
    ABILITY_STARTING: "STARTING",
    ABILITY_RUNNING: "RUNNING",
    ABILITY_STOPPING: "STOPPING",
    ABILITY_FAILED: "FAILED",
}

SOFTBUS_REQUEST = 1
SOFTBUS_RESPONSE = 2
SOFTBUS_EVENT = 3

TASK_READY = 1
TASK_RUNNING = 2
TASK_BLOCKED = 3
TASK_STOPPED = 4

TASK_STATE_NAMES = {
    TASK_READY: "READY",
    TASK_RUNNING: "RUNNING",
    TASK_BLOCKED: "BLOCKED",
    TASK_STOPPED: "STOPPED",
}

RIGHT_READ = 1
RIGHT_WRITE = 2
RIGHT_EXECUTE = 4
RIGHT_MANAGE = 8
FULL_RIGHTS = 15

IRQ_TIMER = 1
IRQ_GPIO = 2
IRQ_PORT = 3
IRQ_CUSTOM = 4


def version_string(version):
    return ".".join(str(part) for part in version)


def parse_version(text):
    if not isinstance(text, str):
        raise ValueError("version must be a string")
    parts = text.strip().lstrip("vV").split(".")
    if len(parts) != 3 or any(not part.isdigit() for part in parts):
        raise ValueError("invalid semantic version: %r" % text)
    return tuple(int(part) for part in parts)


def _compare(version, expression):
    for operator in (">=", "<=", "==", "!=", ">", "<"):
        if expression.startswith(operator):
            target = parse_version(expression[len(operator):])
            if operator == ">=":
                return version >= target
            if operator == "<=":
                return version <= target
            if operator == "==":
                return version == target
            if operator == "!=":
                return version != target
            if operator == ">":
                return version > target
            return version < target
    return version == parse_version(expression)


def satisfies(version, specification):
    """Check a version tuple against a comma-separated constraint string."""
    if isinstance(version, str):
        version = parse_version(version)
    text = specification or ""
    return all(_compare(version, part.strip()) for part in text.split(",") if part.strip())


def status_name(status):
    return STATUS_NAMES.get(status, "UNKNOWN(%d)" % status)


def opcode_name(opcode):
    return OP_NAMES.get(opcode, "UNKNOWN(%d)" % opcode)


def abi_info():
    return {
        "sdk_version": SDK_VERSION_STRING,
        "abi_version": version_string(ABI_VERSION),
        "supported": SUPPORTED_ABI,
        "operations": OP_NAMES,
        "statuses": STATUS_NAMES,
    }
