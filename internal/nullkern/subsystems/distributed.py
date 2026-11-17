"""Distributed subsystem facade for nullkernRT.

Combines the System Ability Manager, soft bus, remote ability proxy, and scheduler hints
into the kernel-visible object exposed as ``kernel.distributed``.
"""

try:
    from .ability_manager import ABILITY_RUNNING, SystemAbilityManager
    from .remote import RemoteAbilityManager
    from .scheduler import DistributedScheduler
    from .softbus import LoopbackTransport, MESSAGE_EVENT, MESSAGE_REQUEST, SoftBus
except ImportError:
    from ability_manager import ABILITY_RUNNING, SystemAbilityManager
    from remote import RemoteAbilityManager
    from scheduler import DistributedScheduler
    from softbus import LoopbackTransport, MESSAGE_EVENT, MESSAGE_REQUEST, SoftBus


class DistributedSubsystem:
    __slots__ = (
        "kernel", "node_id", "transport", "bus", "scheduler", "remote",
        "abilities", "_hosts", "_core_installed", "_core_names",
    )

    def __init__(self, kernel, node_id="local"):
        self.kernel = kernel
        self.node_id = node_id
        self.transport = LoopbackTransport()
        self.bus = SoftBus(node_id, self.transport)
        self.abilities = kernel.abilities if kernel is not None else SystemAbilityManager(kernel)
        self.scheduler = DistributedScheduler(kernel)
        self.scheduler.register_node(node_id, node_id, 100, 0, {})
        self.remote = RemoteAbilityManager(kernel, self.bus, self.scheduler, self.abilities)
        self._hosts = {node_id: self.remote}
        self._core_installed = False
        self._core_names = (
            "system.ability",
            "system.softbus",
            "system.scheduler",
            "system.remote.ability",
        )

    def attach(self, transport):
        self.transport = transport
        self.bus.attach(transport)
        return self.bus.info()

    def add_node(self, node_id, name, capacity=100, load=0, features=None, holder=-1):
        info = self.scheduler.register_node(node_id, name, capacity, load, features, holder)
        if node_id != self.node_id:
            bus = SoftBus(node_id, self.transport)
            self._hosts[node_id] = RemoteAbilityManager(
                self.kernel, bus, self.scheduler, self.abilities
            )
            return {"node": info, "bus": bus}
        return {"node": info, "bus": self.bus}

    def register_local_ability(self, name, handler, dependencies=None, subsystem="application",
                               description="", holder=-1):
        result = self.remote.register_local(
            name, handler, dependencies, subsystem, description, holder
        )
        self.scheduler.set_route(name, self.node_id, holder)
        return result

    def register_remote_ability(self, name, node_id, holder=-1):
        return self.remote.register_remote(name, node_id, holder)

    def call(self, ability_name, arguments=None, node_id=None, holder=-1):
        return self.remote.call(ability_name, arguments, node_id, holder)

    def publish(self, target, payload, kind=MESSAGE_EVENT):
        return self.bus.send(target, kind, payload)

    def subscribe(self, kind, handler):
        self.bus.subscribe(kind, handler)

    def install_core_abilities(self):
        if self._core_installed:
            return list(self._core_names)
        if "system.ability" not in self.abilities.names():
            self.abilities.register(
                "system.ability", "foundation", [], None, None,
                lambda arguments: self.abilities.info(), "System Ability Manager"
            )
        if "system.softbus" not in self.abilities.names():
            self.abilities.register(
                "system.softbus", "distributed", ["system.ability"], None, None,
                lambda arguments: self.bus.info(), "Soft bus endpoint"
            )
        if "system.scheduler" not in self.abilities.names():
            self.abilities.register(
                "system.scheduler", "distributed", ["system.ability"], None, None,
                lambda arguments: self.scheduler.info(), "Distributed scheduler"
            )
        if "system.remote.ability" not in self.abilities.names():
            self.abilities.register(
                "system.remote.ability", "distributed",
                ["system.softbus", "system.scheduler"], None, None,
                lambda arguments: self.remote.info(), "Remote ability proxy"
            )
        for name in self._core_names:
            if self.abilities.state(name) != ABILITY_RUNNING:
                self.abilities.start(name)
        self._core_installed = True
        return list(self._core_names)

    def info(self):
        return {
            "node_id": self.node_id,
            "abilities": self.abilities.info(),
            "softbus": self.bus.info(),
            "scheduler": self.scheduler.info(),
            "remote": self.remote.info(),
        }
