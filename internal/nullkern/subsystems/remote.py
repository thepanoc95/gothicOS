"""Remote ability proxy layered on the soft bus."""

try:
    from ..capability import RIGHT_EXECUTE, RIGHT_MANAGE, RIGHT_READ, RIGHT_WRITE
    from .ability_manager import ABILITY_RUNNING
    from .softbus import MESSAGE_REQUEST
except ImportError:
    from capability import RIGHT_EXECUTE, RIGHT_MANAGE, RIGHT_READ, RIGHT_WRITE
    from ability_manager import ABILITY_RUNNING
    from softbus import MESSAGE_REQUEST


class RemoteAbilityManager:
    """Routes ability calls across soft-bus nodes and hosts local abilities."""

    __slots__ = ("kernel", "abilities", "bus", "scheduler", "routes", "calls")

    def __init__(self, kernel, bus, scheduler, abilities=None):
        self.kernel = kernel
        self.abilities = abilities if abilities is not None else kernel.abilities
        self.bus = bus
        self.scheduler = scheduler
        self.routes = {}
        self.calls = []
        self.install()

    def _require(self, holder, name, right, object_id=None):
        if self.kernel is not None and self.kernel.capabilities is not None:
            self.kernel.capabilities.require(holder, name, right, object_id)

    def install(self):
        self.bus.subscribe(MESSAGE_REQUEST, self._on_request)

    def register_local(self, name, handler, dependencies=None, subsystem="application",
                       description="", holder=-1):
        self._require(holder, "ability", RIGHT_MANAGE, name)
        result = self.abilities.register(
            name, subsystem, dependencies, None, None, handler, description, holder
        )
        self.routes[name] = self.bus.node_id
        return result

    def register_remote(self, name, node_id, holder=-1):
        self._require(holder, "distributed", RIGHT_MANAGE)
        if node_id not in self.scheduler.nodes:
            raise KeyError("node not found")
        self.routes[name] = node_id
        return self.scheduler.set_route(name, node_id, holder)

    def route(self, ability_name, holder=-1):
        self._require(holder, "distributed", RIGHT_READ)
        node_id = self.routes.get(ability_name)
        if node_id is None:
            node_id = self.scheduler.route(ability_name, holder)
        if node_id is None:
            node_id = self.scheduler.place(ability_name, None, holder)
        return node_id

    def call(self, ability_name, arguments=None, node_id=None, holder=-1):
        self._require(holder, "ability", RIGHT_EXECUTE, ability_name)
        self._require(holder, "softbus", RIGHT_WRITE)
        target = node_id if node_id is not None else self.route(ability_name, holder)
        if target is None:
            raise LookupError("no route to ability")
        payload = {
            "ability": ability_name,
            "arguments": arguments if arguments is not None else [],
            "caller": holder,
        }
        response = self.bus.call(target, MESSAGE_REQUEST, payload)
        if isinstance(response, dict) and response.get("ok"):
            result = response.get("result")
        else:
            result = response
        self.scheduler.record_placement(ability_name, target, holder)
        self.calls.append({
            "ability": ability_name,
            "node_id": target,
            "arguments": arguments if arguments is not None else [],
        })
        return result

    def _on_request(self, envelope):
        payload = envelope.get("payload")
        if not isinstance(payload, dict):
            return {"ok": False, "error": "invalid remote request"}
        ability_name = payload.get("ability")
        arguments = payload.get("arguments", [])
        try:
            if ability_name not in self.abilities.names():
                raise KeyError("ability not found")
            if self.abilities.state(ability_name) != ABILITY_RUNNING:
                self.abilities.start(ability_name)
            result = self.abilities.invoke(ability_name, arguments)
            return {"ok": True, "result": result}
        except Exception as error:
            return {"ok": False, "error": str(error)}

    def info(self, holder=-1):
        self._require(holder, "softbus", RIGHT_READ)
        return {
            "node_id": self.bus.node_id,
            "routes": dict(self.routes),
            "calls": len(self.calls),
        }
