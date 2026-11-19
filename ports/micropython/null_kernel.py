"""NullKern takeover facade for the MicroPython chainloader.

The Python mirror of kernel/linux/handoff.c. CPU 0 is never claimed, and no host process
is terminated: takeover only boots the NullKern core and registers its own init task and
services. If a native `gothicos_nk` module is available it is used for the core; otherwise
the pure-Python `internal.nullkern.kernel.Kernel` is used so the chain works on stock
MicroPython firmware as well.
"""

STATE_DISABLED = "disabled"
STATE_READY = "ready"
STATE_BINDING_CORES = "binding-cores"
STATE_CORE_ONLINE = "core-online"
STATE_ELF_LOADED = "elf-loaded"
STATE_INIT_STARTED = "init-started"
STATE_SERVICES_STARTED = "services-started"
STATE_FAILED = "failed"


class TakeoverResult:
    def __init__(self, state, core=None, bound_cpus=None, services=None, error=None):
        self.state = state
        self.core = core
        self.bound_cpus = bound_cpus or []
        self.services = services or []
        self.error = error

    def ok(self):
        return self.state == STATE_SERVICES_STARTED

    def info(self):
        return {
            "state": self.state,
            "bound_cpus": list(self.bound_cpus),
            "services": list(self.services),
            "error": self.error,
        }


def _load_python_kernel():
    try:
        from internal.nullkern.kernel import Kernel
    except ImportError:
        try:
            from kernel import Kernel
        except ImportError:
            return None
    return Kernel


def _make_core(core_count, first_managed_cpu):
    """Return (core, backend_name) or (None, reason)."""
    try:
        import gothicos_nk
    except ImportError:
        gothicos_nk = None
    if gothicos_nk is not None and hasattr(gothicos_nk, "Kernel"):
        try:
            return gothicos_nk.Kernel(core_count, first_managed_cpu), "native"
        except Exception as error:
            return None, "native core init failed: %s" % error
    kernel_class = _load_python_kernel()
    if kernel_class is None:
        return None, "no NullKern core module available"
    try:
        return kernel_class(core_count=core_count), "micropython"
    except TypeError:
        try:
            return kernel_class(), "micropython"
        except Exception as error:
            return None, "python core init failed: %s" % error


def _init_step(kernel, task_id):
    """One-shot init anchor; the scheduler stops the task after this step."""
    return False


class NullKernel:
    def __init__(self, options, init_image=None, core_count=1, first_managed_cpu=1):
        self.options = options
        self.init_image = init_image
        self.core_count = core_count if core_count > 0 else 1
        self.first_managed_cpu = first_managed_cpu
        self.state = STATE_READY
        self.bound_cpus = []
        self.services = []
        self.core = None
        self.backend = None
        self.error = None

    def _fail(self, message):
        self.state = STATE_FAILED
        self.error = message
        return TakeoverResult(self.state, self.core, self.bound_cpus, self.services, message)

    def bind_cpu(self, cpu):
        if cpu == 0:
            return False
        self.bound_cpus.append(cpu)
        return True

    def boot_core(self):
        core, backend = _make_core(self.core_count, self.first_managed_cpu)
        if core is None:
            return False, backend
        result = core.boot()
        if hasattr(result, "ok") and not result.ok():
            return False, "core boot failed: %s" % result.info()
        self.core = core
        self.backend = backend
        return True, backend

    def start_init(self):
        if self.core is None or not hasattr(self.core, "spawn"):
            return False
        self.core.spawn("init", _init_step)
        return True

    def take_over(self):
        if self.state != STATE_READY:
            return TakeoverResult(self.state, self.core, self.bound_cpus, self.services,
                                  self.error)

        self.state = STATE_BINDING_CORES
        for cpu in range(self.first_managed_cpu, self.first_managed_cpu + self.core_count):
            if not self.bind_cpu(cpu):
                return self._fail("CPU 0 is reserved for Linux")
        self.state = STATE_CORE_ONLINE

        ok, reason = self.boot_core()
        if not ok:
            return self._fail(reason)

        if self.init_image is not None:
            if len(self.init_image) < 4 or bytes(self.init_image[:4]) != b"\x7fELF":
                return self._fail("init image is not an ELF file")
        self.state = STATE_ELF_LOADED

        if not self.start_init():
            return self._fail("unable to register init task")
        self.state = STATE_INIT_STARTED

        self.services.append(self.options.resolved_shell_path())
        self.state = STATE_SERVICES_STARTED
        return TakeoverResult(self.state, self.core, self.bound_cpus, self.services)


def takeover(options, init_image=None, core_count=1, first_managed_cpu=1):
    return NullKernel(options, init_image, core_count, first_managed_cpu).take_over()
