"""High-level client for the nullkernRT ABI."""

from .abi import (
    FULL_RIGHTS,
    OP_ABILITY_INFO,
    OP_ABILITY_REGISTER,
    OP_ABILITY_START,
    OP_ABILITY_STATE,
    OP_ABILITY_STOP,
    OP_BOOT_INFO,
    OP_CAP_GRANT,
    OP_CAP_INFO,
    OP_CAP_REVOKE,
    OP_DISTRIBUTED_INFO,
    OP_DRIVER_INFO,
    OP_DRIVER_IO,
    OP_GET_INFO,
    OP_IPC_RECEIVE,
    OP_IPC_SEND,
    OP_IRQ_RAISE,
    OP_IRQ_REGISTER,
    OP_IRQ_SERVICE,
    OP_IRQ_UNREGISTER,
    OP_MEM_ALLOC,
    OP_MEM_FREE,
    OP_MEM_POOL_CREATE,
    OP_MEM_READ,
    OP_MEM_WRITE,
    OP_NODE_INFO,
    OP_NODE_REGISTER,
    OP_PORT_CLOSE,
    OP_PORT_INFO,
    OP_PORT_OPEN,
    OP_PORT_RECEIVE,
    OP_PORT_SEND,
    OP_REMOTE_CALL,
    OP_REMOTE_ROUTE,
    OP_RUN,
    OP_SCHEDULE,
    OP_SOFTBUS_INFO,
    OP_SOFTBUS_SEND,
    OP_SOFTBUS_SUBSCRIBE,
    OP_TASK_CREATE,
    OP_TASK_INFO,
    OP_TASK_RESUME,
    OP_TASK_STATE,
    OP_TASK_STOP,
    OP_YIELD,
    STATUS_OK,
    status_name,
)


class KernelError(Exception):
    """Raised by :meth:`KernelClient.checked` when a syscall returns a status."""

    def __init__(self, status, value=None):
        self.status = status
        self.value = value
        Exception.__init__(self, "nullkern syscall failed: %s" % status_name(status))


class KernelClient:
    """Wraps any ``invoke(opcode, *args)`` boundary.

    Use :meth:`from_kernel` on a device, or pass a transport callable when the kernel
    runs behind a serial/RPC bridge.
    """

    def __init__(self, invoke):
        if not callable(invoke):
            raise TypeError("invoke must be callable")
        self._invoke = invoke

    @classmethod
    def from_kernel(cls, kernel):
        return cls(kernel.abi.invoke)

    def call(self, opcode, *arguments):
        return self._invoke(opcode, *arguments)

    def checked(self, opcode, *arguments):
        status, value = self.call(opcode, *arguments)
        if status != STATUS_OK:
            raise KernelError(status, value)
        return value

    def get_info(self):
        return self.checked(OP_GET_INFO)

    def boot_info(self):
        return self.checked(OP_BOOT_INFO)

    def create_task(self, name, step):
        return TaskHandle(self, self.checked(OP_TASK_CREATE, name, step))

    def stop_task(self, task_id):
        return self.checked(OP_TASK_STOP, task_id)

    def resume_task(self, task_id):
        return self.checked(OP_TASK_RESUME, task_id)

    def task_state(self, task_id):
        return self.checked(OP_TASK_STATE, task_id)

    def task_info(self, task_id):
        return self.checked(OP_TASK_INFO, task_id)

    def send(self, receiver_id, message, sender_id=None):
        if sender_id is None:
            return self.checked(OP_IPC_SEND, receiver_id, message)
        return self.checked(OP_IPC_SEND, receiver_id, message, sender_id)

    def receive(self, task_id=None):
        if task_id is None:
            return self.checked(OP_IPC_RECEIVE)
        return self.checked(OP_IPC_RECEIVE, task_id)

    def yield_step(self):
        return self.checked(OP_YIELD)

    def run(self, max_steps=None):
        if max_steps is None:
            return self.checked(OP_RUN)
        return self.checked(OP_RUN, max_steps)

    def open_port(self, name, capacity=None):
        if capacity is None:
            self.checked(OP_PORT_OPEN, name)
        else:
            self.checked(OP_PORT_OPEN, name, capacity)
        return PortHandle(self, name)

    def close_port(self, name):
        return self.checked(OP_PORT_CLOSE, name)

    def port_info(self, name):
        return self.checked(OP_PORT_INFO, name)

    def create_pool(self, name, block_size, block_count):
        self.checked(OP_MEM_POOL_CREATE, name, block_size, block_count)
        return MemoryPoolHandle(self, name)

    def allocate(self, pool_name, data=None):
        if data is None:
            block_id = self.checked(OP_MEM_ALLOC, pool_name)
        else:
            block_id = self.checked(OP_MEM_ALLOC, pool_name, data)
        return MemoryBlockHandle(self, pool_name, block_id)

    def release(self, pool_name, block_id):
        return self.checked(OP_MEM_FREE, pool_name, block_id)

    def read_block(self, pool_name, block_id, length=None):
        if length is None:
            return self.checked(OP_MEM_READ, pool_name, block_id)
        return self.checked(OP_MEM_READ, pool_name, block_id, length)

    def write_block(self, pool_name, block_id, data):
        return self.checked(OP_MEM_WRITE, pool_name, block_id, data)

    def register_irq(self, source, handler):
        return self.checked(OP_IRQ_REGISTER, source, handler)

    def unregister_irq(self, irq_id):
        return self.checked(OP_IRQ_UNREGISTER, irq_id)

    def raise_irq(self, source, payload=None):
        if payload is None:
            return self.checked(OP_IRQ_RAISE, source)
        return self.checked(OP_IRQ_RAISE, source, payload)

    def service_irqs(self, limit=4):
        return self.checked(OP_IRQ_SERVICE, limit)

    def driver_info(self, name):
        return self.checked(OP_DRIVER_INFO, name)

    def driver_io(self, name, command, argument=None):
        if argument is None:
            return self.checked(OP_DRIVER_IO, name, command)
        return self.checked(OP_DRIVER_IO, name, command, argument)

    def grant_capability(self, name, rights=FULL_RIGHTS, object_id=None):
        if object_id is None:
            return self.checked(OP_CAP_GRANT, name, rights)
        return self.checked(OP_CAP_GRANT, name, rights, object_id)

    def revoke_capability(self, cap_id):
        return self.checked(OP_CAP_REVOKE, cap_id)

    def capability_info(self):
        return self.checked(OP_CAP_INFO)

    def register_ability(self, name, subsystem, dependencies=None, start=None,
                         stop=None, invoke=None, description=""):
        depends = [] if dependencies is None else dependencies
        return self.checked(
            OP_ABILITY_REGISTER, name, subsystem, depends, start, stop, invoke, description
        )

    def start_ability(self, name):
        return self.checked(OP_ABILITY_START, name)

    def stop_ability(self, name):
        return self.checked(OP_ABILITY_STOP, name)

    def ability_state(self, name):
        return self.checked(OP_ABILITY_STATE, name)

    def ability_info(self, name=None):
        if name is None:
            return self.checked(OP_ABILITY_INFO)
        return self.checked(OP_ABILITY_INFO, name)

    def softbus_send(self, target, kind, payload, correlation_id=None):
        if correlation_id is None:
            return self.checked(OP_SOFTBUS_SEND, target, kind, payload)
        return self.checked(OP_SOFTBUS_SEND, target, kind, payload, correlation_id)

    def softbus_subscribe(self, kind, handler):
        return self.checked(OP_SOFTBUS_SUBSCRIBE, kind, handler)

    def softbus_info(self):
        return self.checked(OP_SOFTBUS_INFO)

    def remote_call(self, ability_name, arguments=None, node_id=None):
        arguments_value = [] if arguments is None else arguments
        return self.checked(OP_REMOTE_CALL, ability_name, arguments_value, node_id)

    def remote_route(self, ability_name, node_id):
        return self.checked(OP_REMOTE_ROUTE, ability_name, node_id)

    def register_node(self, node_id, name, capacity=None, load=None, features=None):
        if capacity is None and load is None and features is None:
            return self.checked(OP_NODE_REGISTER, node_id, name)
        current_capacity = 100 if capacity is None else capacity
        current_load = 0 if load is None else load
        advertised = {} if features is None else features
        return self.checked(
            OP_NODE_REGISTER, node_id, name, current_capacity, current_load, advertised
        )

    def add_node(self, node_id, name, capacity=None, load=None, features=None):
        return self.register_node(node_id, name, capacity, load, features)

    def node_info(self, node_id=None):
        if node_id is None:
            return self.checked(OP_NODE_INFO)
        return self.checked(OP_NODE_INFO, node_id)

    def schedule(self, ability_name, candidates=None):
        if candidates is None:
            return self.checked(OP_SCHEDULE, ability_name)
        return self.checked(OP_SCHEDULE, ability_name, candidates)

    def distributed_info(self):
        return self.checked(OP_DISTRIBUTED_INFO)


class TaskHandle:
    __slots__ = ("client", "task_id")

    def __init__(self, client, task_id):
        self.client = client
        self.task_id = task_id

    def state(self):
        return self.client.task_state(self.task_id)

    def info(self):
        return self.client.task_info(self.task_id)

    def stop(self):
        return self.client.stop_task(self.task_id)

    def resume(self):
        return self.client.resume_task(self.task_id)


class PortHandle:
    __slots__ = ("client", "name")

    def __init__(self, client, name):
        self.client = client
        self.name = name

    def send(self, message, sender_id=None):
        if sender_id is None:
            return self.client.checked(OP_PORT_SEND, self.name, message)
        return self.client.checked(OP_PORT_SEND, self.name, message, sender_id)

    def receive(self, task_id=None):
        if task_id is None:
            return self.client.checked(OP_PORT_RECEIVE, self.name)
        return self.client.checked(OP_PORT_RECEIVE, self.name, task_id)

    def info(self):
        return self.client.port_info(self.name)

    def close(self):
        return self.client.close_port(self.name)


class MemoryPoolHandle:
    __slots__ = ("client", "name")

    def __init__(self, client, name):
        self.client = client
        self.name = name

    def allocate(self, data=None):
        return self.client.allocate(self.name, data)

    def read(self, block_id, length=None):
        return self.client.read_block(self.name, block_id, length)


class MemoryBlockHandle:
    __slots__ = ("client", "pool", "block_id")

    def __init__(self, client, pool, block_id):
        self.client = client
        self.pool = pool
        self.block_id = block_id

    def read(self, length=None):
        return self.client.read_block(self.pool, self.block_id, length)

    def write(self, data):
        return self.client.write_block(self.pool, self.block_id, data)

    def release(self):
        return self.client.release(self.pool, self.block_id)
