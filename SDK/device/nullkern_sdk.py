"""MicroPython-side client for the nullkernRT ABI.

This module is deliberately small: no classes with keyword arguments, no annotations,
and no dependencies outside the kernel. Compile it to ``.mpy`` and place it next to the
kernel on the device.
"""

ABI_VERSION = (1, 1, 1)

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

ABILITY_STOPPED = 0
ABILITY_STARTING = 1
ABILITY_RUNNING = 2
ABILITY_STOPPING = 3
ABILITY_FAILED = 4
SOFTBUS_REQUEST = 1
SOFTBUS_RESPONSE = 2
SOFTBUS_EVENT = 3

FULL_RIGHTS = 15
IRQ_TIMER = 1
IRQ_GPIO = 2
IRQ_PORT = 3
IRQ_CUSTOM = 4


class KernelClient:
    __slots__ = ("invoke",)

    def __init__(self, invoke):
        self.invoke = invoke

    @staticmethod
    def from_kernel(kernel):
        return KernelClient(kernel.abi.invoke)

    def call(self, opcode, *arguments):
        return self.invoke(opcode, *arguments)

    def checked(self, opcode, *arguments):
        status, value = self.invoke(opcode, *arguments)
        if status != STATUS_OK:
            raise RuntimeError("nullkern syscall %d failed with status %d" % (opcode, status))
        return value

    def info(self):
        return self.checked(OP_GET_INFO)

    def boot_info(self):
        return self.checked(OP_BOOT_INFO)

    def spawn(self, name, step):
        return self.checked(OP_TASK_CREATE, name, step)

    def send(self, receiver, message, sender=None):
        if sender is None:
            return self.checked(OP_IPC_SEND, receiver, message)
        return self.checked(OP_IPC_SEND, receiver, message, sender)

    def receive(self, task_id=None):
        if task_id is None:
            return self.checked(OP_IPC_RECEIVE)
        return self.checked(OP_IPC_RECEIVE, task_id)

    def open_port(self, name, capacity=None):
        if capacity is None:
            self.checked(OP_PORT_OPEN, name)
        else:
            self.checked(OP_PORT_OPEN, name, capacity)
        return name

    def port_send(self, name, message, sender=None):
        if sender is None:
            return self.checked(OP_PORT_SEND, name, message)
        return self.checked(OP_PORT_SEND, name, message, sender)

    def port_receive(self, name, task_id=None):
        if task_id is None:
            return self.checked(OP_PORT_RECEIVE, name)
        return self.checked(OP_PORT_RECEIVE, name, task_id)

    def yield_step(self):
        return self.checked(OP_YIELD)

    def run(self, max_steps=None):
        if max_steps is None:
            return self.checked(OP_RUN)
        return self.checked(OP_RUN, max_steps)

    def register_ability(self, name, subsystem, dependencies=None, invoke=None, description=""):
        deps = [] if dependencies is None else dependencies
        return self.checked(
            OP_ABILITY_REGISTER, name, subsystem, deps, None, None, invoke, description
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

    def remote_call(self, ability, arguments=None, node=None):
        args = [] if arguments is None else arguments
        return self.checked(OP_REMOTE_CALL, ability, args, node)

    def remote_route(self, ability, node):
        return self.checked(OP_REMOTE_ROUTE, ability, node)

    def register_node(self, node, name, capacity=None, load=None, features=None):
        if capacity is None and load is None and features is None:
            return self.checked(OP_NODE_REGISTER, node, name)
        cap = 100 if capacity is None else capacity
        current_load = 0 if load is None else load
        advertised = {} if features is None else features
        return self.checked(OP_NODE_REGISTER, node, name, cap, current_load, advertised)

    def add_node(self, node, name, capacity=None, load=None, features=None):
        return self.register_node(node, name, capacity, load, features)

    def node_info(self, node=None):
        if node is None:
            return self.checked(OP_NODE_INFO)
        return self.checked(OP_NODE_INFO, node)

    def schedule(self, ability, candidates=None):
        if candidates is None:
            return self.checked(OP_SCHEDULE, ability)
        return self.checked(OP_SCHEDULE, ability, candidates)

    def distributed_info(self):
        return self.checked(OP_DISTRIBUTED_INFO)
