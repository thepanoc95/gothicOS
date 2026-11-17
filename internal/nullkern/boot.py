"""Deterministic boot stages for nullkernRT."""

BOOT_PENDING = 0
BOOT_COMPLETE = 1
BOOT_FAILED = 2

STAGE_PLATFORM = "platform"
STAGE_TOPOLOGY = "topology"
STAGE_MEMORY = "memory"
STAGE_PORTS = "ports"
STAGE_IRQ = "irq"
STAGE_DRIVERS = "drivers"
STAGE_SUBSYSTEMS = "subsystems"
STAGE_DISTRIBUTED = "distributed"
STAGE_ABI = "abi"

STAGES = (
    STAGE_PLATFORM,
    STAGE_TOPOLOGY,
    STAGE_MEMORY,
    STAGE_PORTS,
    STAGE_IRQ,
    STAGE_DRIVERS,
    STAGE_SUBSYSTEMS,
    STAGE_DISTRIBUTED,
    STAGE_ABI,
)


class BootResult:
    __slots__ = ("status", "stages", "error")

    def __init__(self, status, stages, error=None):
        self.status = status
        self.stages = stages
        self.error = error

    def ok(self):
        return self.status == BOOT_COMPLETE

    def info(self):
        return {"status": self.status, "stages": list(self.stages), "error": self.error}


class BootLoader:
    """Records a fixed boot sequence over an already-constructed kernel."""

    def __init__(self, kernel):
        self.kernel = kernel
        self._stages = []

    def stage_platform(self):
        try:
            from .OSPlatform import describe
        except ImportError:
            from OSPlatform import describe
        self._stages.append({
            "stage": STAGE_PLATFORM,
            "arch": describe()["arch"],
        })

    def stage_topology(self):
        self._stages.append({
            "stage": STAGE_TOPOLOGY,
            "core_count": self.kernel.core_count,
        })

    def stage_memory(self):
        self.kernel.memory.create_pool("system", 32, 8)
        self._stages.append({"stage": STAGE_MEMORY, "pool": "system"})

    def stage_ports(self):
        self.kernel.ports.open("kernel", -1, 8)
        self._stages.append({"stage": STAGE_PORTS, "port": "kernel"})

    def stage_irq(self):
        self._stages.append({"stage": STAGE_IRQ, "handlers": 0})

    def stage_drivers(self):
        self._stages.append({
            "stage": STAGE_DRIVERS,
            "drivers": self.kernel.drivers.names(),
        })

    def stage_subsystems(self):
        names = self.kernel.distributed.install_core_abilities()
        self._stages.append({
            "stage": STAGE_SUBSYSTEMS,
            "abilities": names,
        })

    def stage_distributed(self):
        self._stages.append({
            "stage": STAGE_DISTRIBUTED,
            "node_id": self.kernel.distributed.node_id,
            "transport": self.kernel.softbus.info()["transport"],
        })

    def stage_abi(self):
        self._stages.append({
            "stage": STAGE_ABI,
            "abi_version": self.kernel.abi.get_info()["abi_version"],
        })

    def boot(self):
        self._stages = []
        steps = (
            self.stage_platform,
            self.stage_topology,
            self.stage_memory,
            self.stage_ports,
            self.stage_irq,
            self.stage_drivers,
            self.stage_subsystems,
            self.stage_distributed,
            self.stage_abi,
        )
        try:
            for step in steps:
                step()
        except Exception as error:
            return BootResult(BOOT_FAILED, list(self._stages), str(error))
        self.kernel.boot_result = BootResult(BOOT_COMPLETE, list(self._stages))
        self.kernel.boot_status = BOOT_COMPLETE
        return self.kernel.boot_result


def boot(kernel):
    return BootLoader(kernel).boot()
