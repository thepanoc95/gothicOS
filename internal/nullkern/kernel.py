"""nullkernRT: a small microkernel that runs as MicroPython code.

The kernel provides deterministic task dispatch, bounded task and port IPC, fixed-block
memory pools, capability checks, deferred interrupt handlers, a driver registry, and a
staged boot sequence. It keeps the v1 task ABI working while exposing the extended
operations through ``Kernel.abi``.
"""

import time

try:
    import micropython
except ImportError:
    class _MicroPythonCompat:
        @staticmethod
        def native(function):
            return function

        @staticmethod
        def viper(function):
            return function

    micropython = _MicroPythonCompat()

try:
    from machine import Pin
except ImportError:
    Pin = None

try:
    import _thread
except ImportError:
    _thread = None

try:
    from .fdt import cpu_count_from_blob, cpu_count_from_file
    from .abi import KernelABI
    from .capability import FULL_RIGHTS, CapabilityTable
    from .memory import MemoryManager
    from .ports import PortTable
    from .irq import InterruptController
    from .drivers import DriverRegistry
    from .boot import BOOT_PENDING, BootLoader
    from .binary import BinaryExecutionModule
    from .subsystems import DistributedSubsystem, SystemAbilityManager
except ImportError:
    from fdt import cpu_count_from_blob, cpu_count_from_file
    from abi import KernelABI
    from capability import FULL_RIGHTS, CapabilityTable
    from memory import MemoryManager
    from ports import PortTable
    from irq import InterruptController
    from drivers import DriverRegistry
    from boot import BOOT_PENDING, BootLoader
    from binary import BinaryExecutionModule
    from subsystems import DistributedSubsystem, SystemAbilityManager

MAX_TASKS = 8
MAILBOX_DEPTH = 4
TASK_READY = 1
TASK_RUNNING = 2
TASK_BLOCKED = 3
TASK_STOPPED = 4


@micropython.native
def _find_ready_task(ready_mask: int, start: int, task_count: int) -> int:
    for offset in range(task_count):
        slot = start + offset
        if slot >= task_count:
            slot -= task_count
        if ready_mask & (1 << slot):
            return slot
    return -1


@micropython.viper
def _task_bit(task_id: int) -> int:
    return 1 << task_id


if hasattr(micropython, "asm_thumb"):
    @micropython.asm_thumb
    def _advance_slot_asm(r0, r1):
        add(r0, 1)
        cmp(r0, r1)
        blt(slot_ready)
        mov(r0, 0)
        label(slot_ready)
else:
    def _advance_slot_asm(slot, task_count):
        slot += 1
        return 0 if slot >= task_count else slot


class _Task:
    __slots__ = (
        "name", "step", "state", "mailbox", "head", "tail", "count",
        "thread_started",
    )

    def __init__(self, name, step):
        self.name = name
        self.step = step
        self.state = TASK_READY
        self.mailbox = [None] * MAILBOX_DEPTH
        self.head = 0
        self.tail = 0
        self.count = 0
        self.thread_started = False


class HardwareMode:
    @staticmethod
    def power_led(on=True):
        if Pin is None:
            raise RuntimeError("machine.Pin is only available on MicroPython hardware")
        led = Pin("LED", Pin.OUT)
        led.value(1 if on else 0)


class Kernel:
    """Two-core threaded scheduler with fixed slots and bounded IPC mailboxes.

    MicroPython's RP2 port exposes one interpreter thread per hardware core. This
    scheduler runs on core 0 and starts one scheduler thread on core 1 when the FDT
    reports multiple CPUs. Task callbacks are still short steps: Python cannot safely
    switch an arbitrary interpreter stack from a timer interrupt.
    """

    def __init__(self, max_tasks=MAX_TASKS, core_count=1, node_id="local"):
        if max_tasks < 1 or max_tasks > MAX_TASKS:
            raise ValueError("max_tasks must be between 1 and %d" % MAX_TASKS)
        if not isinstance(core_count, int) or core_count < 1:
            raise ValueError("core_count must be a positive integer")
        self.max_tasks = max_tasks
        self.core_count = core_count
        self.multicore_enabled = core_count > 1
        self._tasks = [None] * max_tasks
        self._ready_mask = 0
        self._cursor = 0
        self._current_task = None
        self._thread_task_ids = {}
        self._lock = _thread.allocate_lock() if _thread is not None else None
        self._secondary_started = False
        self._stopping = False
        self._failure = None
        self.boot_status = BOOT_PENDING
        self.boot_result = None
        self.capabilities = CapabilityTable()
        self.memory = MemoryManager()
        self.ports = PortTable()
        self.interrupts = InterruptController()
        self.drivers = DriverRegistry()
        self.abilities = SystemAbilityManager(self)
        self.distributed = DistributedSubsystem(self, node_id)
        self.softbus = self.distributed.bus
        self.scheduler = self.distributed.scheduler
        self.remote = self.distributed.remote
        self.capabilities.grant(-1, "kernel", FULL_RIGHTS)
        self.abi = KernelABI(self)
        self.binary = BinaryExecutionModule(self)

    @property
    def mailbox_depth(self):
        return MAILBOX_DEPTH

    @classmethod
    def from_fdt(cls, source, max_tasks=MAX_TASKS):
        if isinstance(source, (bytes, bytearray)):
            core_count = cpu_count_from_blob(source)
        elif hasattr(source, "read"):
            core_count = cpu_count_from_blob(source.read())
        else:
            core_count = cpu_count_from_file(source)
        return cls(max_tasks=max_tasks, core_count=core_count)

    @property
    def current_task(self):
        if _thread is not None and hasattr(_thread, "get_ident"):
            self._acquire()
            try:
                task_id = self._thread_task_ids.get(_thread.get_ident())
            finally:
                self._release()
            if task_id is not None:
                return task_id
        return self._current_task

    def _acquire(self):
        if self._lock is not None:
            self._lock.acquire()

    def _release(self):
        if self._lock is not None:
            self._lock.release()

    def spawn(self, name, step):
        """Register a callback that performs one bounded task step per dispatch."""
        if not callable(step):
            raise TypeError("step must be callable")
        self._acquire()
        try:
            self._stopping = False
            for task_id in range(self.max_tasks):
                if self._tasks[task_id] is None or self._tasks[task_id].state == TASK_STOPPED:
                    self._tasks[task_id] = _Task(name, step)
                    self.capabilities.revoke_holder(task_id)
                    self.capabilities.grant(task_id, "task", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "port", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "memory", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "irq", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "driver", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "ability", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "softbus", FULL_RIGHTS)
                    self.capabilities.grant(task_id, "distributed", FULL_RIGHTS)
                    self._ready_mask |= _task_bit(task_id)
                    return task_id
        finally:
            self._release()
        raise RuntimeError("task table is full")

    def run_once(self):
        """Service deferred IRQs, then dispatch one task step."""
        self.interrupts.service(4)
        return self._dispatch_one()

    def _dispatch_one(self, stop_on_error=False):
        self._acquire()
        task_id = _find_ready_task(self._ready_mask, self._cursor, self.max_tasks)
        if task_id < 0:
            self._release()
            return None
        task = self._tasks[task_id]
        self._cursor = _advance_slot_asm(task_id, self.max_tasks)
        self._ready_mask &= ~_task_bit(task_id)
        task.state = TASK_RUNNING
        self._release()

        thread_id = _thread.get_ident() if _thread is not None and hasattr(_thread, "get_ident") else None
        if thread_id is not None:
            self._acquire()
            self._thread_task_ids[thread_id] = task_id
            self._release()
        else:
            self._current_task = task_id
        try:
            keep_running = task.step(self, task_id)
        except Exception as error:
            self._acquire()
            if task.state == TASK_RUNNING:
                task.state = TASK_STOPPED if stop_on_error else TASK_READY
                if not stop_on_error:
                    self._ready_mask |= _task_bit(task_id)
            if stop_on_error:
                self._failure = error
            self._release()
            if not stop_on_error:
                raise
            return task_id
        finally:
            if thread_id is not None:
                self._acquire()
                self._thread_task_ids.pop(thread_id, None)
                self._release()
            else:
                self._current_task = None

        self._acquire()
        if task.state == TASK_RUNNING:
            if keep_running is False:
                task.state = TASK_STOPPED
            else:
                task.state = TASK_READY
                self._ready_mask |= _task_bit(task_id)
        self._release()
        return task_id

    def run(self, max_steps=None):
        """Run schedulers until idle; max_steps selects deterministic single-core mode."""
        if max_steps is None and self.multicore_enabled:
            return self._run_multicore()
        steps = 0
        while max_steps is None or steps < max_steps:
            if self.run_once() is None:
                break
            steps += 1
        return steps

    def _secondary_scheduler(self):
        try:
            while not self._stopping:
                if self._failure is not None:
                    return
                if self._dispatch_one(stop_on_error=True) is None:
                    self._acquire()
                    all_stopped = all(
                        task is None or task.state == TASK_STOPPED
                        for task in self._tasks
                    )
                    if all_stopped:
                        self._stopping = True
                    self._release()
                    if all_stopped:
                        return
                    time.sleep_ms(1) if hasattr(time, "sleep_ms") else time.sleep(0.001)
        finally:
            self._acquire()
            self._secondary_started = False
            self._release()

    def _run_multicore(self):
        if _thread is None:
            raise RuntimeError("multicore scheduling requires MicroPython _thread support")
        if not hasattr(_thread, "start_new_thread"):
            raise RuntimeError("the active MicroPython port cannot start a secondary thread")

        self._acquire()
        should_start = not self._secondary_started
        if should_start:
            self._secondary_started = True
        self._release()
        if should_start:
            try:
                _thread.start_new_thread(self._secondary_scheduler, ())
            except Exception as error:
                self._acquire()
                self._secondary_started = False
                self._release()
                raise RuntimeError("unable to start the secondary-core scheduler") from error

        steps = 0
        while True:
            if self._failure is not None:
                raise RuntimeError("task failed on a scheduler core") from self._failure
            task_id = self._dispatch_one(stop_on_error=True)
            if task_id is not None:
                steps += 1
                continue

            self._acquire()
            active = any(
                task is not None and task.state in (TASK_READY, TASK_RUNNING)
                for task in self._tasks
            )
            all_stopped = all(
                task is None or task.state == TASK_STOPPED for task in self._tasks
            )
            if all_stopped:
                self._stopping = True
            self._release()
            if not active:
                if self._failure is not None:
                    raise RuntimeError("task failed on a scheduler core") from self._failure
                break
            time.sleep_ms(1) if hasattr(time, "sleep_ms") else time.sleep(0.001)
        return steps

    def stop(self, task_id):
        self._acquire()
        try:
            task = self._get_task(task_id)
            task.state = TASK_STOPPED
            self._ready_mask &= ~_task_bit(task_id)
            self.capabilities.revoke_holder(task_id)
        finally:
            self._release()

    def resume(self, task_id):
        self._acquire()
        try:
            task = self._get_task(task_id)
            if task.state != TASK_BLOCKED:
                raise ValueError("task is not blocked")
            task.state = TASK_READY
            self._ready_mask |= _task_bit(task_id)
        finally:
            self._release()

    def block_current(self):
        if self.current_task is None:
            raise RuntimeError("no task is currently running")
        task_id = self.current_task
        self._acquire()
        try:
            self._tasks[task_id].state = TASK_BLOCKED
            self._ready_mask &= ~_task_bit(task_id)
        finally:
            self._release()

    def send(self, receiver_id, message, sender_id=None):
        """Queue a message; return False when the receiver's mailbox is full."""
        if sender_id is None:
            sender_id = self.current_task if self.current_task is not None else -1
        self._acquire()
        try:
            receiver = self._get_task(receiver_id)
            if receiver.state == TASK_STOPPED or receiver.count == MAILBOX_DEPTH:
                return False
            receiver.mailbox[receiver.tail] = (sender_id, message)
            receiver.tail = (receiver.tail + 1) % MAILBOX_DEPTH
            receiver.count += 1
            if receiver.state == TASK_BLOCKED:
                receiver.state = TASK_READY
                self._ready_mask |= _task_bit(receiver_id)
            return True
        finally:
            self._release()

    def receive(self, task_id=None):
        """Return the next (sender_id, message), or block an empty mailbox reader."""
        running_task_id = self.current_task
        if task_id is None:
            task_id = running_task_id
        if task_id is None:
            raise RuntimeError("receive must be called by a running task")
        self._acquire()
        try:
            task = self._get_task(task_id)
            if task.count == 0:
                if task_id == running_task_id:
                    task.state = TASK_BLOCKED
                    self._ready_mask &= ~_task_bit(task_id)
                return None
            message = task.mailbox[task.head]
            task.mailbox[task.head] = None
            task.head = (task.head + 1) % MAILBOX_DEPTH
            task.count -= 1
            return message
        finally:
            self._release()

    def task_state(self, task_id):
        self._acquire()
        try:
            return self._get_task(task_id).state
        finally:
            self._release()

    def task_info(self, task_id):
        self._acquire()
        try:
            task = self._get_task(task_id)
            return {
                "task_id": task_id,
                "name": task.name,
                "state": task.state,
                "mailbox_messages": task.count,
            }
        finally:
            self._release()

    # --- named IPC ports -------------------------------------------------

    def port_open(self, name, capacity=None):
        owner = self.current_task
        owner = -1 if owner is None else owner
        return self.ports.open(name, owner, capacity).info()

    def port_close(self, name):
        self.ports.close(name)

    def port_send(self, name, message, sender_id=None):
        if sender_id is None:
            sender_id = self.current_task if self.current_task is not None else -1
        return self.ports.port(name).send(sender_id, message)

    def port_receive(self, name, task_id=None):
        running_task_id = self.current_task
        if task_id is None:
            task_id = running_task_id
        if task_id is None:
            raise RuntimeError("receive must be called by a running task")
        port = self.ports.port(name)
        message = port.receive()
        if message is None:
            if task_id == running_task_id:
                self.block_current()
            return None
        return message

    def port_info(self, name):
        return self.ports.port(name).info()

    def port_names(self):
        return self.ports.names()

    # --- fixed-block memory ----------------------------------------------

    def pool_create(self, name, block_size, block_count):
        return self.memory.create_pool(name, block_size, block_count).stats()

    def pool_allocate(self, name, data=None):
        owner = self.current_task
        return self.memory.allocate(name, -1 if owner is None else owner, data)

    def pool_release(self, name, block_id):
        self.memory.release(name, block_id)

    def pool_read(self, name, block_id, length=None):
        return self.memory.read(name, block_id, length)

    def pool_write(self, name, block_id, data):
        self.memory.write(name, block_id, data)

    def memory_info(self):
        return self.memory.stats()

    # --- deferred interrupts ---------------------------------------------

    def irq_register(self, source, handler):
        owner = self.current_task
        return self.interrupts.register(source, handler, -1 if owner is None else owner)

    def irq_unregister(self, irq_id):
        self.interrupts.unregister(irq_id)

    def irq_raise(self, source, payload=None):
        return self.interrupts.raise_irq(source, payload)

    def irq_service(self, limit=4):
        return self.interrupts.service(limit)

    def irq_info(self):
        return self.interrupts.info()

    # --- drivers ----------------------------------------------------------

    def driver_info(self, name):
        return self.drivers.get(name).info()

    def driver_io(self, name, command, argument=None):
        driver = self.drivers.get(name)
        if command == "open":
            return driver.open()
        if command == "close":
            return driver.close()
        if command == "read":
            return driver.read(argument)
        if command == "write":
            return driver.write(argument)
        if command == "ioctl":
            return driver.ioctl(argument)
        raise ValueError("invalid driver command")

    def install_led_driver(self, name="led", pin_name="LED", pin=None):
        if pin is None:
            if Pin is None:
                raise RuntimeError("machine.Pin is not available on this platform")
            pin = Pin(pin_name, Pin.OUT)
        try:
            from .drivers import GpioDriver
        except ImportError:
            from drivers import GpioDriver
        return self.drivers.register(GpioDriver(name, pin_name, pin))

    # --- subsystems and distributed runtime -------------------------------

    def ability_register(self, name, subsystem, dependencies=None, start=None,
                         stop=None, invoke=None, description=""):
        return self.abilities.register(
            name, subsystem, dependencies, start, stop, invoke, description
        )

    def ability_start(self, name):
        return self.abilities.start(name)

    def ability_stop(self, name):
        return self.abilities.stop(name)

    def ability_state(self, name):
        return self.abilities.state(name)

    def ability_info(self, name=None):
        return self.abilities.info(name)

    def softbus_send(self, target, kind, payload, correlation_id=0):
        return self.softbus.send(target, kind, payload, correlation_id)

    def softbus_subscribe(self, kind, handler):
        self.softbus.subscribe(kind, handler)

    def softbus_info(self):
        return self.softbus.info()

    def add_node(self, node_id, name, capacity=100, load=0, features=None):
        return self.distributed.add_node(node_id, name, capacity, load, features)

    def register_node(self, node_id, name, capacity=100, load=0, features=None):
        return self.scheduler.register_node(node_id, name, capacity, load, features)

    def node_info(self, node_id=None):
        return self.scheduler.info(node_id)

    def remote_route(self, ability_name, node_id):
        return self.distributed.register_remote_ability(ability_name, node_id)

    def remote_call(self, ability_name, arguments=None, node_id=None):
        return self.distributed.call(ability_name, arguments, node_id)

    def schedule(self, ability_name, candidates=None):
        if candidates is None:
            return self.scheduler.place(ability_name)
        return self.scheduler.rank(ability_name, candidates)

    # --- capabilities and boot -------------------------------------------

    def cap_grant(self, name, rights, object_id=None):
        holder = self.current_task
        return self.capabilities.grant(
            -1 if holder is None else holder, name, rights, object_id
        ).describe()

    def cap_revoke(self, cap_id):
        holder = self.current_task
        self.capabilities.revoke(cap_id, None if holder is None else holder)

    def cap_info(self):
        holder = self.current_task
        if holder is None:
            return self.capabilities.info()
        return {"holder_capabilities": self.capabilities.holder_capabilities(holder)}

    def boot(self):
        return BootLoader(self).boot()

    # --- Umbra binary execution ------------------------------------------

    def binary_load(self, package, name=None, task_id=None):
        """Verify an Umbra package and return an unspawned binary task."""
        return self.binary.load(package, name, task_id)

    def binary_spawn(self, package, name=None):
        """Verify a package and register it with the task scheduler."""
        return self.binary.spawn(package, name)

    def binary_step(self, task, budget=None):
        """Advance a binary task by one bounded execution slice."""
        return self.binary.step(task, budget)

    def binary_status(self, task):
        return self.binary.status(task)

    def binary_stop(self, task):
        return self.binary.stop(task)

    def _get_task(self, task_id):
        if not isinstance(task_id, int) or task_id < 0 or task_id >= self.max_tasks:
            raise ValueError("invalid task id")
        task = self._tasks[task_id]
        if task is None:
            raise ValueError("task slot is empty")
        return task

    def _os_kernel_boot(self, max_steps=None):
        result = self.boot()
        if not result.ok():
            raise RuntimeError("nullkern boot failed: %s" % result.error)
        return self.run(max_steps)


NullKernelUpper = Kernel
