"""In-process soft bus with pluggable transports.

The default loopback transport is deterministic and works on MicroPython and CPython.
Other transports only need ``send(envelope)`` and ``register(node_id, bus)`` methods.
"""

MESSAGE_REQUEST = 1
MESSAGE_RESPONSE = 2
MESSAGE_EVENT = 3

MESSAGE_KIND_NAMES = {
    MESSAGE_REQUEST: "REQUEST",
    MESSAGE_RESPONSE: "RESPONSE",
    MESSAGE_EVENT: "EVENT",
}

SOFTBUS_QUEUE_LIMIT = 16
_MISSING = object()


class LoopbackTransport:
    """Synchronous multi-node transport for one interpreter."""

    __slots__ = ("nodes", "delivered", "dropped")

    def __init__(self):
        self.nodes = {}
        self.delivered = []
        self.dropped = 0

    def register(self, node_id, bus):
        if not isinstance(node_id, str) or not node_id:
            raise ValueError("node id must be a non-empty string")
        self.nodes[node_id] = bus

    def unregister(self, node_id):
        if node_id in self.nodes:
            del self.nodes[node_id]

    def send(self, envelope):
        self.delivered.append(envelope)
        bus = self.nodes.get(envelope["target"])
        if bus is None:
            self.dropped += 1
            return False
        return bus.deliver(envelope)

    def info(self):
        return {
            "transport": "loopback",
            "nodes": list(self.nodes.keys()),
            "delivered": len(self.delivered),
            "dropped": self.dropped,
        }


class SoftBus:
    """Node-local endpoint for request, response, and event messages."""

    __slots__ = (
        "node_id", "transport", "queue_limit", "_subscriptions", "_queue",
        "_pending", "_next_message_id", "_sent", "_delivered",
    )

    def __init__(self, node_id, transport=None, queue_limit=SOFTBUS_QUEUE_LIMIT):
        if not isinstance(node_id, str) or not node_id:
            raise ValueError("node id must be a non-empty string")
        if queue_limit < 1:
            raise ValueError("queue_limit must be positive")
        self.node_id = node_id
        self.transport = None
        self.queue_limit = queue_limit
        self._subscriptions = {}
        self._queue = []
        self._pending = {}
        self._next_message_id = 1
        self._sent = []
        self._delivered = []
        if transport is not None:
            self.attach(transport)

    def attach(self, transport):
        if transport is None or not hasattr(transport, "send"):
            raise ValueError("invalid soft bus transport")
        self.transport = transport
        if hasattr(transport, "register"):
            transport.register(self.node_id, self)
        return self

    def subscribe(self, kind, handler):
        if not callable(handler):
            raise TypeError("soft bus handler must be callable")
        self._subscriptions[kind] = handler

    def unsubscribe(self, kind):
        if kind in self._subscriptions:
            del self._subscriptions[kind]

    def post(self, target, kind, payload, correlation_id=0):
        if not isinstance(target, str) or not target:
            raise ValueError("target node must be a non-empty string")
        if kind not in MESSAGE_KIND_NAMES:
            raise ValueError("invalid soft bus message kind")
        if self.transport is None:
            raise RuntimeError("no soft bus transport attached")
        envelope = {
            "id": self._next_message_id,
            "source": self.node_id,
            "target": target,
            "kind": kind,
            "payload": payload,
            "correlation_id": correlation_id,
            "delivered": False,
        }
        self._next_message_id += 1
        if kind == MESSAGE_REQUEST and not correlation_id:
            envelope["correlation_id"] = envelope["id"]
        self._sent.append(envelope)
        envelope["delivered"] = bool(self.transport.send(envelope))
        return envelope

    def send(self, target, kind, payload, correlation_id=0):
        envelope = self.post(target, kind, payload, correlation_id)
        if not envelope["delivered"]:
            raise RuntimeError("soft bus target unavailable")
        return envelope["id"]

    def deliver(self, envelope):
        self._delivered.append(envelope)
        kind = envelope.get("kind")
        if kind == MESSAGE_RESPONSE:
            self._pending[envelope.get("correlation_id")] = envelope.get("payload")
            return True
        handler = self._subscriptions.get(kind)
        if handler is None:
            if len(self._queue) >= self.queue_limit:
                return False
            self._queue.append(envelope)
            return True
        try:
            result = handler(envelope)
        except Exception as error:
            result = {"ok": False, "error": str(error)}
        if kind == MESSAGE_REQUEST and result is not None:
            self.post(
                envelope["source"], MESSAGE_RESPONSE, result, envelope.get("correlation_id", 0)
            )
        return True

    def poll(self, limit=None):
        processed = 0
        while self._queue and (limit is None or processed < limit):
            envelope = self._queue.pop(0)
            self.deliver(envelope)
            processed += 1
        return processed

    def call(self, target, kind, payload):
        envelope = self.post(target, kind, payload)
        if not envelope["delivered"]:
            raise RuntimeError("soft bus target unavailable")
        response = self._pending.pop(envelope["id"], _MISSING)
        if response is _MISSING:
            self.poll()
            response = self._pending.pop(envelope["id"], _MISSING)
        if response is _MISSING:
            raise RuntimeError("soft bus request timed out")
        if isinstance(response, dict) and not response.get("ok", True):
            raise RuntimeError(response.get("error", "soft bus request failed"))
        return response

    def info(self):
        transport_info = None
        if self.transport is not None and hasattr(self.transport, "info"):
            transport_info = self.transport.info()
        return {
            "node_id": self.node_id,
            "subscriptions": list(self._subscriptions.keys()),
            "queued": len(self._queue),
            "sent": len(self._sent),
            "delivered": len(self._delivered),
            "transport": transport_info,
        }
