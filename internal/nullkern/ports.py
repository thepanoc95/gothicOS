"""Named IPC ports for the nullkern runtime.

Ports are kernel-owned endpoints with a fixed queue depth. A task opens a port, other
tasks send to it by name, and the owner receives messages. This complements the legacy
task-id mailbox API without changing its behaviour.
"""

PORT_OPEN = 1
PORT_CLOSED = 2
DEFAULT_PORT_CAPACITY = 4


class Port:
    __slots__ = ("name", "owner", "capacity", "state", "queue", "head", "count")

    def __init__(self, name, owner, capacity):
        self.name = name
        self.owner = owner
        self.capacity = capacity
        self.state = PORT_OPEN
        self.queue = [None] * capacity
        self.head = 0
        self.count = 0

    def send(self, sender_id, message):
        if self.state != PORT_OPEN or self.count == self.capacity:
            return False
        index = (self.head + self.count) % self.capacity
        self.queue[index] = (sender_id, message)
        self.count += 1
        return True

    def receive(self):
        if self.count == 0:
            return None
        message = self.queue[self.head]
        self.queue[self.head] = None
        self.head = (self.head + 1) % self.capacity
        self.count -= 1
        return message

    def info(self):
        return {
            "name": self.name,
            "owner": self.owner,
            "capacity": self.capacity,
            "state": self.state,
            "messages": self.count,
        }


class PortTable:
    __slots__ = ("_ports",)

    def __init__(self):
        self._ports = {}

    def open(self, name, owner, capacity=None):
        if not isinstance(name, str) or not name:
            raise ValueError("port name must be a non-empty string")
        if name in self._ports:
            raise ValueError("port already exists")
        if capacity is None:
            capacity = DEFAULT_PORT_CAPACITY
        if not isinstance(capacity, int) or capacity < 1 or capacity > 64:
            raise ValueError("port capacity must be between 1 and 64")
        port = Port(name, owner, capacity)
        self._ports[name] = port
        return port

    def close(self, name):
        self.port(name).state = PORT_CLOSED
        del self._ports[name]

    def port(self, name):
        if name not in self._ports:
            raise KeyError(name)
        return self._ports[name]

    def names(self):
        return sorted(self._ports)

    def info(self):
        result = {}
        for name in self._ports:
            result[name] = self._ports[name].info()
        return result
