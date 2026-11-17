"""Deferred software interrupts for the nullkern runtime.

Python callbacks on MicroPython cannot safely be preempted from a hardware ISR, so
interrupts are recorded here and serviced cooperatively by the scheduler. Handlers run
as bounded deferred work with a fixed queue, which keeps the kernel deterministic.
"""

IRQ_TIMER = 1
IRQ_GPIO = 2
IRQ_PORT = 3
IRQ_CUSTOM = 4
MAX_IRQ_HANDLERS = 16
MAX_PENDING_IRQ = 16

_IRQ_NAMES = {
    IRQ_TIMER: "timer",
    IRQ_GPIO: "gpio",
    IRQ_PORT: "port",
    IRQ_CUSTOM: "custom",
}


def irq_name(source):
    return _IRQ_NAMES.get(source, "unknown")


class InterruptController:
    __slots__ = ("_handlers", "_pending", "_errors", "_next_id")

    def __init__(self):
        self._handlers = {}
        self._pending = []
        self._errors = []
        self._next_id = 1

    def register(self, source, handler, owner=-1):
        if source not in _IRQ_NAMES:
            raise ValueError("invalid interrupt source")
        if not callable(handler):
            raise TypeError("interrupt handler must be callable")
        if len(self._handlers) >= MAX_IRQ_HANDLERS:
            raise RuntimeError("interrupt table is full")
        irq_id = self._next_id
        self._next_id += 1
        self._handlers[irq_id] = {
            "irq_id": irq_id,
            "source": source,
            "handler": handler,
            "owner": owner,
        }
        return irq_id

    def unregister(self, irq_id, owner=None):
        entry = self.lookup(irq_id)
        if owner is not None and entry["owner"] != owner:
            raise RuntimeError("capability denied")
        del self._handlers[irq_id]

    def lookup(self, irq_id):
        if irq_id not in self._handlers:
            raise KeyError(irq_id)
        return self._handlers[irq_id]

    def raise_irq(self, source, payload=None):
        if source not in _IRQ_NAMES:
            raise ValueError("invalid interrupt source")
        if len(self._pending) >= MAX_PENDING_IRQ:
            return False
        self._pending.append((source, payload))
        return True

    def pending(self):
        return len(self._pending)

    def service(self, limit=4):
        if not isinstance(limit, int) or limit < 0:
            raise ValueError("limit must be a non-negative integer")
        handled = 0
        while handled < limit and self._pending:
            source, payload = self._pending.pop(0)
            for irq_id in self._handlers:
                entry = self._handlers[irq_id]
                if entry["source"] != source:
                    continue
                try:
                    entry["handler"](source, payload)
                except Exception as error:
                    if len(self._errors) >= 4:
                        self._errors.pop(0)
                    self._errors.append(str(error))
                handled += 1
        return handled

    def last_error(self):
        return self._errors[-1] if self._errors else None

    def info(self):
        return {
            "handlers": len(self._handlers),
            "pending": len(self._pending),
            "last_error": self.last_error(),
        }
