"""Capability tokens and rights enforcement for the nullkern runtime.

Capabilities are small immutable records stored in a fixed table. Every task receives
a default set at spawn time; the kernel itself is the ``-1`` holder and bypasses checks.
The ABI layer calls :meth:`CapabilityTable.require` before privileged operations, so a
task cannot act on an object it was not granted access to.
"""

CAP_TASK = 1
CAP_PORT = 2
CAP_MEMORY = 4
CAP_IRQ = 8
CAP_DRIVER = 16
CAP_BOOT = 64
CAP_ABILITY = 128
CAP_SOFTBUS = 256
CAP_DISTRIBUTED = 512

RIGHT_READ = 1
RIGHT_WRITE = 2
RIGHT_EXECUTE = 4
RIGHT_MANAGE = 8
FULL_RIGHTS = RIGHT_READ | RIGHT_WRITE | RIGHT_EXECUTE | RIGHT_MANAGE

ALL_CAPS = (
    CAP_TASK | CAP_PORT | CAP_MEMORY | CAP_IRQ | CAP_DRIVER |
    CAP_ABILITY | CAP_SOFTBUS | CAP_DISTRIBUTED
)
KERNEL_CAPS = ALL_CAPS | CAP_BOOT

_CAPABILITY_NAMES = (
    (CAP_TASK, "task"),
    (CAP_PORT, "port"),
    (CAP_MEMORY, "memory"),
    (CAP_IRQ, "irq"),
    (CAP_DRIVER, "driver"),
    (CAP_BOOT, "boot"),
    (CAP_ABILITY, "ability"),
    (CAP_SOFTBUS, "softbus"),
    (CAP_DISTRIBUTED, "distributed"),
)


def capability_name(bits):
    """Return a readable ``name|name`` representation of a capability bitmask."""
    names = []
    for value, name in _CAPABILITY_NAMES:
        if bits & value:
            names.append(name)
    return "|".join(names) if names else "none"


class Capability:
    __slots__ = ("cap_id", "holder", "name", "rights", "object_id")

    def __init__(self, cap_id, holder, name, rights, object_id=None):
        self.cap_id = cap_id
        self.holder = holder
        self.name = name
        self.rights = rights
        self.object_id = object_id

    def allows(self, right):
        return bool(self.rights & right)

    def describe(self):
        return {
            "cap_id": self.cap_id,
            "holder": self.holder,
            "name": self.name,
            "rights": self.rights,
            "object_id": self.object_id,
        }


class CapabilityTable:
    __slots__ = ("_capabilities", "_next_id")

    def __init__(self):
        self._capabilities = {}
        self._next_id = 1

    def grant(self, holder, name, rights, object_id=None):
        if not isinstance(name, str) or not name:
            raise ValueError("capability name must be a non-empty string")
        if not isinstance(rights, int) or rights < 1 or rights > 15:
            raise ValueError("capability rights must be between 1 and 15")
        cap_id = self._next_id
        self._next_id += 1
        capability = Capability(cap_id, holder, name, rights, object_id)
        self._capabilities[cap_id] = capability
        return capability

    def revoke(self, cap_id, holder=None):
        capability = self.lookup(cap_id)
        if holder is not None and capability.holder != holder:
            raise RuntimeError("capability denied")
        del self._capabilities[cap_id]
        return capability

    def revoke_holder(self, holder):
        for cap_id in list(self._capabilities):
            if self._capabilities[cap_id].holder == holder:
                del self._capabilities[cap_id]

    def lookup(self, cap_id, holder=None):
        if not isinstance(cap_id, int) or cap_id not in self._capabilities:
            raise ValueError("capability not found")
        capability = self._capabilities[cap_id]
        if holder is not None and capability.holder != holder:
            raise RuntimeError("capability denied")
        return capability

    def holder_capabilities(self, holder):
        result = []
        for cap_id in self._capabilities:
            capability = self._capabilities[cap_id]
            if capability.holder == holder:
                result.append(capability.describe())
        return result

    def require(self, holder, name, right, object_id=None):
        """Return the matching capability or raise ``RuntimeError``.

        Holder ``-1`` is the kernel and is always allowed. ``object_id`` of ``None``
        means "any instance of this capability type".
        """
        if holder == -1:
            return None
        for cap_id in self._capabilities:
            capability = self._capabilities[cap_id]
            if capability.holder != holder or capability.name != name:
                continue
            if not capability.allows(right):
                continue
            if capability.object_id is not None and object_id is not None:
                if capability.object_id != object_id:
                    continue
            return capability
        raise RuntimeError("capability denied")

    def info(self):
        return {"count": len(self._capabilities), "next_cap_id": self._next_id}
