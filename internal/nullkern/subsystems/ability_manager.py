"""System Ability Manager for nullkernRT subsystems.

Abilities are named lifecycle objects. Dependencies are started before an ability and
stopped after its dependents. The manager is MicroPython-compatible: no dataclasses,
annotations, f-strings, or keyword-only parameters are used.
"""

try:
    from ..capability import RIGHT_EXECUTE, RIGHT_MANAGE, RIGHT_READ
except ImportError:
    from capability import RIGHT_EXECUTE, RIGHT_MANAGE, RIGHT_READ

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


class Ability:
    __slots__ = (
        "name", "subsystem", "dependencies", "start", "stop", "invoke",
        "description", "state",
    )

    def __init__(self, name, subsystem, dependencies, start, stop, invoke, description):
        self.name = name
        self.subsystem = subsystem
        self.dependencies = list(dependencies)
        self.start = start
        self.stop = stop
        self.invoke = invoke
        self.description = description
        self.state = ABILITY_STOPPED

    def info(self):
        return {
            "name": self.name,
            "subsystem": self.subsystem,
            "dependencies": list(self.dependencies),
            "state": self.state,
            "state_name": ABILITY_STATE_NAMES[self.state],
            "description": self.description,
        }


class SystemAbilityManager:
    """Registry and deterministic lifecycle manager for named abilities."""

    __slots__ = ("kernel", "_abilities", "_start_order")

    def __init__(self, kernel=None):
        self.kernel = kernel
        self._abilities = {}
        self._start_order = []

    def _require(self, holder, right, name=None):
        if self.kernel is not None and self.kernel.capabilities is not None:
            self.kernel.capabilities.require(holder, "ability", right, name)

    def register(self, name, subsystem, dependencies=None, start=None, stop=None,
                 invoke=None, description="", holder=-1):
        if not isinstance(name, str) or not name:
            raise ValueError("ability name must be a non-empty string")
        if not isinstance(subsystem, str) or not subsystem:
            raise ValueError("subsystem must be a non-empty string")
        if name in self._abilities:
            raise ValueError("ability already exists")
        self._require(holder, RIGHT_MANAGE, name)
        ability = Ability(
            name, subsystem, dependencies or (), start, stop, invoke, description
        )
        self._abilities[name] = ability
        return ability.info()

    def unregister(self, name, holder=-1):
        ability = self.get(name)
        self._require(holder, RIGHT_MANAGE, name)
        if ability.state in (ABILITY_STARTING, ABILITY_RUNNING):
            self.stop(name, holder)
        del self._abilities[name]
        if name in self._start_order:
            self._start_order.remove(name)

    def get(self, name, holder=-1, right=RIGHT_READ):
        if name not in self._abilities:
            raise KeyError("ability not found")
        self._require(holder, right, name)
        return self._abilities[name]

    def names(self):
        return list(self._abilities.keys())

    def started(self):
        return list(self._start_order)

    def state(self, name, holder=-1):
        return self.get(name, holder, RIGHT_READ).state

    def info(self, name=None, holder=-1):
        if name is not None:
            return self.get(name, holder, RIGHT_READ).info()
        return {
            "abilities": [self._abilities[key].info() for key in self._abilities],
            "start_order": list(self._start_order),
        }

    def start(self, name, holder=-1):
        return self._start(name, holder, []).info()

    def _start(self, name, holder, chain):
        ability = self.get(name, holder, RIGHT_EXECUTE)
        if ability.state == ABILITY_RUNNING:
            return ability
        if name in chain:
            raise ValueError("ability dependency cycle")
        chain.append(name)
        try:
            for dependency in ability.dependencies:
                self._start(dependency, holder, chain)
            if ability.state == ABILITY_STARTING:
                ability.state = ABILITY_FAILED
                raise ValueError("ability is already starting")
            ability.state = ABILITY_STARTING
            try:
                if ability.start is not None:
                    ability.start(self.kernel)
            except Exception:
                ability.state = ABILITY_FAILED
                raise
            ability.state = ABILITY_RUNNING
            if name not in self._start_order:
                self._start_order.append(name)
            return ability
        finally:
            chain.pop()

    def stop(self, name, holder=-1):
        target = self.get(name, holder, RIGHT_MANAGE)
        dependents = self._dependent_names(name)
        for dependent in reversed(dependents):
            if dependent != name:
                self._stop_one(dependent, holder)
        self._stop_one(name, holder)
        return target.info()

    def _dependent_names(self, name):
        pending = [name]
        found = []
        while pending:
            current = pending.pop(0)
            for key in self._abilities:
                ability = self._abilities[key]
                if current in ability.dependencies and key not in found:
                    found.append(key)
                    pending.append(key)
        return found

    def _stop_one(self, name, holder):
        ability = self.get(name, holder, RIGHT_MANAGE)
        if ability.state in (ABILITY_STOPPED, ABILITY_FAILED):
            return ability
        ability.state = ABILITY_STOPPING
        try:
            if ability.stop is not None:
                ability.stop(self.kernel)
        except Exception:
            ability.state = ABILITY_FAILED
            raise
        ability.state = ABILITY_STOPPED
        if name in self._start_order:
            self._start_order.remove(name)
        return ability

    def invoke(self, name, arguments=None, holder=-1):
        ability = self.get(name, holder, RIGHT_EXECUTE)
        if ability.state != ABILITY_RUNNING:
            self._start(name, holder, [])
        if ability.invoke is None:
            raise RuntimeError("ability has no invoke handler")
        return ability.invoke(arguments if arguments is not None else [])
