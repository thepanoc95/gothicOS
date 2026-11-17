"""Stable syscall-style ABI for nullkern runtime clients."""

try:
    from .capability import RIGHT_EXECUTE, RIGHT_READ, RIGHT_WRITE, RIGHT_MANAGE
except ImportError:
    from capability import RIGHT_EXECUTE, RIGHT_READ, RIGHT_WRITE, RIGHT_MANAGE

ABI_MAJOR = 1
ABI_MINOR = 1
ABI_PATCH = 1
ABI_VERSION = (ABI_MAJOR, ABI_MINOR, ABI_PATCH)

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

SOFTBUS_REQUEST = 1
SOFTBUS_RESPONSE = 2
SOFTBUS_EVENT = 3

TASK_READY = 1
TASK_RUNNING = 2
TASK_BLOCKED = 3
TASK_STOPPED = 4

_OP_ARGUMENT_COUNTS = {
    OP_GET_INFO: (0, 0),
    OP_TASK_CREATE: (2, 2),
    OP_TASK_STOP: (1, 1),
    OP_TASK_RESUME: (1, 1),
    OP_TASK_STATE: (1, 1),
    OP_IPC_SEND: (2, 3),
    OP_IPC_RECEIVE: (0, 1),
    OP_YIELD: (0, 0),
    OP_RUN: (0, 1),
    OP_TASK_INFO: (1, 1),
    OP_PORT_OPEN: (1, 2),
    OP_PORT_CLOSE: (1, 1),
    OP_PORT_SEND: (2, 3),
    OP_PORT_RECEIVE: (1, 2),
    OP_PORT_INFO: (1, 1),
    OP_MEM_POOL_CREATE: (3, 3),
    OP_MEM_ALLOC: (1, 2),
    OP_MEM_FREE: (2, 2),
    OP_MEM_WRITE: (3, 3),
    OP_MEM_READ: (2, 3),
    OP_IRQ_REGISTER: (2, 2),
    OP_IRQ_UNREGISTER: (1, 1),
    OP_IRQ_RAISE: (1, 2),
    OP_IRQ_SERVICE: (0, 1),
    OP_DRIVER_INFO: (1, 1),
    OP_DRIVER_IO: (2, 3),
    OP_CAP_GRANT: (2, 4),
    OP_CAP_REVOKE: (1, 1),
    OP_CAP_INFO: (0, 0),
    OP_BOOT_INFO: (0, 0),
    OP_ABILITY_REGISTER: (3, 7),
    OP_ABILITY_START: (1, 1),
    OP_ABILITY_STOP: (1, 1),
    OP_ABILITY_STATE: (1, 1),
    OP_ABILITY_INFO: (0, 1),
    OP_SOFTBUS_SEND: (3, 4),
    OP_SOFTBUS_SUBSCRIBE: (2, 2),
    OP_SOFTBUS_INFO: (0, 0),
    OP_REMOTE_CALL: (1, 3),
    OP_REMOTE_ROUTE: (2, 2),
    OP_NODE_REGISTER: (2, 5),
    OP_NODE_INFO: (0, 1),
    OP_SCHEDULE: (1, 2),
    OP_DISTRIBUTED_INFO: (0, 0),
}


class _ABIStatusError(Exception):
    def __init__(self, status):
        self.status = status


class KernelABI:
    """Versioned adapter that keeps callers independent of Kernel internals.

    Calls return ``(status_code, value)``. Failed calls always return ``None`` as
    the value; exceptions raised by user task callbacks become INTERNAL_ERROR.
    """

    def __init__(self, kernel):
        self.kernel = kernel

    def _caller(self):
        task_id = self.kernel.current_task
        return -1 if task_id is None else task_id

    def _require(self, name, right, object_id=None):
        return self.kernel.capabilities.require(self._caller(), name, right, object_id)

    def invoke(self, opcode, *arguments):
        if type(opcode) is not int or opcode not in _OP_ARGUMENT_COUNTS:
            return STATUS_UNSUPPORTED, None
        minimum, maximum = _OP_ARGUMENT_COUNTS[opcode]
        if len(arguments) < minimum or len(arguments) > maximum:
            return STATUS_INVALID_ARGUMENT, None

        try:
            return STATUS_OK, self._dispatch(opcode, arguments)
        except _ABIStatusError as error:
            return error.status, None
        except LookupError:
            return STATUS_NOT_FOUND, None
        except ValueError as error:
            message = str(error)
            if message == "task slot is empty":
                return STATUS_NOT_FOUND, None
            if message in ("port already exists", "pool already exists", "driver already exists", "ability already exists"):
                return STATUS_ALREADY_EXISTS, None
            if message in ("ability dependency cycle", "ability is already starting"):
                return STATUS_INVALID_ARGUMENT, None
            return STATUS_INVALID_ARGUMENT, None
        except TypeError:
            return STATUS_INVALID_ARGUMENT, None
        except RuntimeError as error:
            message = str(error)
            if message == "task table is full":
                return STATUS_QUEUE_FULL, None
            if message == "no task is currently running":
                return STATUS_INVALID_ARGUMENT, None
            if message == "capability denied":
                return STATUS_PERMISSION_DENIED, None
            if message in ("interrupt table is full", "pool is too large"):
                return STATUS_OUT_OF_MEMORY, None
            if message in ("soft bus target unavailable", "no route to ability"):
                return STATUS_NOT_FOUND, None
            if message == "soft bus request timed out":
                return STATUS_WOULD_BLOCK, None
            if message in ("no soft bus transport attached", "ability has no invoke handler"):
                return STATUS_UNSUPPORTED, None
            return STATUS_INTERNAL_ERROR, None
        except MemoryError:
            return STATUS_OUT_OF_MEMORY, None
        except Exception:
            return STATUS_INTERNAL_ERROR, None

    def _dispatch(self, opcode, arguments):
        if opcode == OP_GET_INFO:
            return self.get_info()
        if opcode == OP_TASK_CREATE:
            name, callback = arguments
            if not isinstance(name, str) or not name:
                raise ValueError("task name must be a non-empty string")
            return self.kernel.spawn(name, callback)
        if opcode == OP_TASK_STOP:
            self.kernel.stop(arguments[0])
            return None
        if opcode == OP_TASK_RESUME:
            self.kernel.resume(arguments[0])
            return None
        if opcode == OP_TASK_STATE:
            return self.kernel.task_state(arguments[0])
        if opcode == OP_TASK_INFO:
            return self.kernel.task_info(arguments[0])
        if opcode == OP_IPC_SEND:
            receiver_id, message = arguments[:2]
            sender_id = arguments[2] if len(arguments) == 3 else None
            if self.kernel.send(receiver_id, message, sender_id):
                return None
            if self.kernel.task_state(receiver_id) == 4:
                raise _ABIStatusError(STATUS_NOT_FOUND)
            raise _ABIStatusError(STATUS_QUEUE_FULL)
        if opcode == OP_IPC_RECEIVE:
            task_id = arguments[0] if arguments else None
            message = self.kernel.receive(task_id)
            if message is None:
                raise _ABIStatusError(STATUS_WOULD_BLOCK)
            return message
        if opcode == OP_YIELD:
            try:
                return self.kernel.run_once()
            except Exception:
                raise _ABIStatusError(STATUS_INTERNAL_ERROR)
        if opcode == OP_RUN:
            max_steps = arguments[0] if arguments else None
            if max_steps is not None and (
                not isinstance(max_steps, int) or max_steps < 0
            ):
                raise ValueError("max_steps must be a non-negative integer")
            try:
                return self.kernel.run(max_steps)
            except Exception:
                raise _ABIStatusError(STATUS_INTERNAL_ERROR)

        if opcode == OP_PORT_OPEN:
            self._require("port", RIGHT_MANAGE)
            capacity = arguments[1] if len(arguments) == 2 else None
            return self.kernel.port_open(arguments[0], capacity)
        if opcode == OP_PORT_CLOSE:
            self._require("port", RIGHT_MANAGE, arguments[0])
            self.kernel.port_close(arguments[0])
            return None
        if opcode == OP_PORT_SEND:
            self._require("port", RIGHT_WRITE, arguments[0])
            sender_id = arguments[2] if len(arguments) == 3 else None
            if self.kernel.port_send(arguments[0], arguments[1], sender_id):
                return None
            raise _ABIStatusError(STATUS_QUEUE_FULL)
        if opcode == OP_PORT_RECEIVE:
            self._require("port", RIGHT_READ, arguments[0])
            task_id = arguments[1] if len(arguments) == 2 else None
            message = self.kernel.port_receive(arguments[0], task_id)
            if message is None:
                raise _ABIStatusError(STATUS_WOULD_BLOCK)
            return message
        if opcode == OP_PORT_INFO:
            self._require("port", RIGHT_READ, arguments[0])
            return self.kernel.port_info(arguments[0])

        if opcode == OP_MEM_POOL_CREATE:
            self._require("memory", RIGHT_MANAGE)
            return self.kernel.pool_create(arguments[0], arguments[1], arguments[2])
        if opcode == OP_MEM_ALLOC:
            self._require("memory", RIGHT_MANAGE, arguments[0])
            data = arguments[1] if len(arguments) == 2 else None
            block_id = self.kernel.pool_allocate(arguments[0], data)
            if block_id < 0:
                raise _ABIStatusError(STATUS_OUT_OF_MEMORY)
            return block_id
        if opcode == OP_MEM_FREE:
            self._require("memory", RIGHT_MANAGE, arguments[0])
            self.kernel.pool_release(arguments[0], arguments[1])
            return None
        if opcode == OP_MEM_WRITE:
            self._require("memory", RIGHT_WRITE, arguments[0])
            self.kernel.pool_write(arguments[0], arguments[1], arguments[2])
            return None
        if opcode == OP_MEM_READ:
            self._require("memory", RIGHT_READ, arguments[0])
            length = arguments[2] if len(arguments) == 3 else None
            return self.kernel.pool_read(arguments[0], arguments[1], length)

        if opcode == OP_IRQ_REGISTER:
            self._require("irq", RIGHT_MANAGE)
            return self.kernel.irq_register(arguments[0], arguments[1])
        if opcode == OP_IRQ_UNREGISTER:
            self._require("irq", RIGHT_MANAGE, arguments[0])
            self.kernel.irq_unregister(arguments[0])
            return None
        if opcode == OP_IRQ_RAISE:
            self._require("irq", RIGHT_WRITE)
            payload = arguments[1] if len(arguments) == 2 else None
            if self.kernel.irq_raise(arguments[0], payload):
                return None
            raise _ABIStatusError(STATUS_QUEUE_FULL)
        if opcode == OP_IRQ_SERVICE:
            self._require("irq", RIGHT_READ)
            return self.kernel.irq_service(arguments[0] if arguments else 4)

        if opcode == OP_DRIVER_INFO:
            self._require("driver", RIGHT_READ, arguments[0])
            return self.kernel.driver_info(arguments[0])
        if opcode == OP_DRIVER_IO:
            self._require("driver", RIGHT_WRITE, arguments[0])
            argument = arguments[2] if len(arguments) == 3 else None
            return self.kernel.driver_io(arguments[0], arguments[1], argument)

        if opcode == OP_CAP_GRANT:
            self._require("boot", RIGHT_MANAGE)
            object_id = arguments[3] if len(arguments) == 4 else None
            return self.kernel.cap_grant(arguments[0], arguments[1], object_id)
        if opcode == OP_CAP_REVOKE:
            self._require("boot", RIGHT_MANAGE)
            self.kernel.cap_revoke(arguments[0])
            return None
        if opcode == OP_CAP_INFO:
            return self.kernel.cap_info()
        if opcode == OP_BOOT_INFO:
            if self.kernel.boot_result is not None:
                return self.kernel.boot_result.info()
            return {"status": self.kernel.boot_status, "stages": []}

        holder = self._caller()
        if opcode == OP_ABILITY_REGISTER:
            name, subsystem, dependencies = arguments[:3]
            start = arguments[3] if len(arguments) > 3 else None
            stop = arguments[4] if len(arguments) > 4 else None
            invoke = arguments[5] if len(arguments) > 5 else None
            description = arguments[6] if len(arguments) > 6 else ""
            self._require("ability", RIGHT_MANAGE, name)
            return self.kernel.abilities.register(
                name, subsystem, dependencies, start, stop, invoke, description, holder
            )
        if opcode == OP_ABILITY_START:
            self._require("ability", RIGHT_EXECUTE, arguments[0])
            return self.kernel.abilities.start(arguments[0], holder)
        if opcode == OP_ABILITY_STOP:
            self._require("ability", RIGHT_MANAGE, arguments[0])
            return self.kernel.abilities.stop(arguments[0], holder)
        if opcode == OP_ABILITY_STATE:
            self._require("ability", RIGHT_READ, arguments[0])
            return self.kernel.abilities.state(arguments[0], holder)
        if opcode == OP_ABILITY_INFO:
            name = arguments[0] if arguments else None
            self._require("ability", RIGHT_READ, name)
            return self.kernel.abilities.info(name, holder)

        if opcode == OP_SOFTBUS_SEND:
            target, kind, payload = arguments[:3]
            correlation_id = arguments[3] if len(arguments) == 4 else 0
            self._require("softbus", RIGHT_WRITE)
            return self.kernel.softbus.send(target, kind, payload, correlation_id)
        if opcode == OP_SOFTBUS_SUBSCRIBE:
            self._require("softbus", RIGHT_MANAGE)
            self.kernel.softbus.subscribe(arguments[0], arguments[1])
            return None
        if opcode == OP_SOFTBUS_INFO:
            self._require("softbus", RIGHT_READ)
            return self.kernel.softbus.info()

        if opcode == OP_REMOTE_CALL:
            ability_name = arguments[0]
            arguments_value = arguments[1] if len(arguments) > 1 else None
            node_id = arguments[2] if len(arguments) > 2 else None
            self._require("ability", RIGHT_EXECUTE, ability_name)
            self._require("softbus", RIGHT_WRITE)
            return self.kernel.distributed.call(
                ability_name, arguments_value, node_id, holder
            )
        if opcode == OP_REMOTE_ROUTE:
            self._require("distributed", RIGHT_MANAGE)
            return self.kernel.distributed.register_remote_ability(
                arguments[0], arguments[1], holder
            )

        if opcode == OP_NODE_REGISTER:
            node_id, name = arguments[:2]
            capacity = arguments[2] if len(arguments) > 2 else 100
            load = arguments[3] if len(arguments) > 3 else 0
            features = arguments[4] if len(arguments) > 4 else None
            self._require("distributed", RIGHT_MANAGE)
            return self.kernel.distributed.add_node(
                node_id, name, capacity, load, features, holder
            )["node"]
        if opcode == OP_NODE_INFO:
            node_id = arguments[0] if arguments else None
            self._require("distributed", RIGHT_READ)
            return self.kernel.scheduler.info(node_id, holder)
        if opcode == OP_SCHEDULE:
            ability_name = arguments[0]
            candidates = arguments[1] if len(arguments) > 1 else None
            self._require("distributed", RIGHT_READ)
            return {
                "node_id": self.kernel.scheduler.place(ability_name, candidates, holder),
                "ranking": self.kernel.scheduler.rank(ability_name, candidates, holder),
            }
        if opcode == OP_DISTRIBUTED_INFO:
            self._require("distributed", RIGHT_READ)
            return self.kernel.distributed.info()

        raise ValueError("unreachable operation")

    def get_info(self):
        capabilities = CAP_TASKS | CAP_IPC | CAP_SUBSYSTEMS | CAP_DISTRIBUTED
        if self.kernel.multicore_enabled:
            capabilities |= CAP_MULTICORE
        self.kernel._acquire()
        try:
            task_count = sum(task is not None for task in self.kernel._tasks)
        finally:
            self.kernel._release()
        return {
            "abi_version": ABI_VERSION,
            "capabilities": capabilities,
            "max_tasks": self.kernel.max_tasks,
            "task_count": task_count,
            "mailbox_depth": self.kernel.mailbox_depth,
            "core_count": self.kernel.core_count,
            "features": {
                "ports": True,
                "memory_pools": True,
                "interrupts": True,
                "drivers": True,
                "capabilities": True,
                "boot": True,
                "binary_execution": True,
                "subsystems": True,
                "softbus": True,
                "distributed": True,
            },
        }