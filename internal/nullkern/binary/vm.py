"""Stack-based Umbra Bytecode interpreter for the NULLKernRT binary module.

The interpreter is deliberately small: one operand stack, one frame per call, a bounded
instruction budget per scheduler dispatch, and no implicit type coercion beyond the
operations UmbraTS explicitly emits. It runs as ordinary MicroPython code, so it does not
depend on MicroPython internals or on a native Umbra JIT.
"""

try:
    import ustruct as struct
except ImportError:
    import struct

try:
    from . import format as fmt
    from .verifier import verify
    from ..abi import OP_GET_INFO, STATUS_OK
    from ..capability import RIGHT_READ, RIGHT_WRITE, RIGHT_MANAGE
except ImportError:
    import format as fmt
    from verifier import verify
    from abi import OP_GET_INFO, STATUS_OK
    from capability import RIGHT_READ, RIGHT_WRITE, RIGHT_MANAGE


READY = "READY"
RUNNING = "RUNNING"
BLOCKED = "BLOCKED"
STOPPED = "STOPPED"
FAILED = "FAILED"

NATIVE_CONTINUE = 0
NATIVE_YIELD = 1
NATIVE_BLOCK = 2
NATIVE_STOP = 3

DEFAULT_STEP_BUDGET = 128

NATIVE_NAMES = {
    fmt.NATIVE_LOG: "LOG",
    fmt.NATIVE_YIELD: "YIELD",
    fmt.NATIVE_INFO: "INFO",
    fmt.NATIVE_PORT_SEND: "PORT_SEND",
    fmt.NATIVE_PORT_RECEIVE: "PORT_RECEIVE",
    fmt.NATIVE_MEM_ALLOC: "MEM_ALLOC",
    fmt.NATIVE_MEM_FREE: "MEM_FREE",
    fmt.NATIVE_IRQ_TIMER: "IRQ_TIMER",
    fmt.NATIVE_TASK_STOP: "TASK_STOP",
}

# Capability strings that applications declare with @Capability.
DECLARED_CAPABILITIES = {
    "system.logging": (None, 0),
    "system.info": (None, 0),
    "system.yield": (None, 0),
    "port.send": ("port", RIGHT_WRITE),
    "port.receive": ("port", RIGHT_READ),
    "ports": ("port", RIGHT_READ | RIGHT_WRITE),
    "memory.alloc": ("memory", RIGHT_WRITE),
    "memory.free": ("memory", RIGHT_WRITE),
    "memory": ("memory", RIGHT_READ | RIGHT_WRITE),
    "irq.timer": ("irq", RIGHT_WRITE),
    "irq": ("irq", RIGHT_READ | RIGHT_WRITE),
    "task.stop": ("task", RIGHT_MANAGE),
    "task": ("task", RIGHT_MANAGE),
    "driver.io": ("driver", RIGHT_READ | RIGHT_WRITE),
    "driver": ("driver", RIGHT_READ | RIGHT_WRITE),
}

NATIVE_CAPABILITY = {
    fmt.NATIVE_PORT_SEND: ("port.send", "port", RIGHT_WRITE),
    fmt.NATIVE_PORT_RECEIVE: ("port.receive", "port", RIGHT_READ),
    fmt.NATIVE_MEM_ALLOC: ("memory.alloc", "memory", RIGHT_WRITE),
    fmt.NATIVE_MEM_FREE: ("memory.free", "memory", RIGHT_WRITE),
    fmt.NATIVE_IRQ_TIMER: ("irq.timer", "irq", RIGHT_WRITE),
    fmt.NATIVE_TASK_STOP: ("task.stop", "task", RIGHT_MANAGE),
}


class VMError(Exception):
    """Raised when a verified package cannot finish execution."""


class VerificationError(VMError):
    """Raised when a binary fails verification before loading."""


class CapabilityError(VMError):
    """Raised when a package declares or uses an ungranted capability."""


class Frame:
    __slots__ = ("function_index", "locals", "ip", "base")

    def __init__(self, function_index, locals_, base):
        self.function_index = function_index
        self.locals = locals_
        self.ip = 0
        self.base = base


class BinaryTask:
    """One loaded Umbra application and its execution state."""

    __slots__ = (
        "module", "binary_id", "kernel_task_id", "name", "package", "metadata",
        "header", "image", "constants", "functions", "code_offset", "frames",
        "stack", "state", "error", "step_budget", "declared_capabilities",
        "limits", "max_stack", "max_call_depth",
    )

    def __init__(self, module, binary_id, verification, name=None, step_budget=None, limits=None):
        self.module = module
        self.binary_id = binary_id
        self.kernel_task_id = None
        self.image = verification.image
        self.package = self.image["package"]
        self.metadata = verification.metadata or {}
        self.header = verification.header
        self.constants = self.image["constants"]
        self.functions = self.image["functions"]
        self.code_offset = self.image["code_offset"]
        self.limits = limits or {}
        self.max_stack = self.limits.get("max_stack", fmt.MAX_STACK_DEPTH)
        self.max_call_depth = self.limits.get("max_call_depth", fmt.MAX_CALL_DEPTH)
        self.step_budget = step_budget or DEFAULT_STEP_BUDGET
        self.name = name or self.metadata.get("name", "umbra-app")
        declared = self.metadata.get("capabilities", [])
        self.declared_capabilities = {}
        for capability in declared:
            self.declared_capabilities[capability] = True
        self.frames = []
        self.stack = []
        self.state = READY
        self.error = None
        self.reset()

    def reset(self):
        """Restart execution from the declared entry point."""
        entry = self.header["entry"]
        function = self.functions[entry]
        self.frames = [Frame(entry, [None] * function["nlocals"], 0)]
        self.stack = []
        self.state = READY
        self.error = None

    def step(self, kernel, task_id):
        """Scheduler callback: advance one bounded VM slice."""
        return self.module.step(self)

    def info(self):
        return {
            "binary_id": self.binary_id,
            "kernel_task_id": self.kernel_task_id,
            "name": self.name,
            "entry": self.metadata.get("entry"),
            "state": self.state,
            "frames": len(self.frames),
            "stack_depth": len(self.stack),
            "error": str(self.error) if self.error is not None else None,
            "capabilities": list(self.declared_capabilities),
        }


class BinaryExecutionModule:
    """Loads, verifies, schedules, and supervises Umbra bytecode applications."""

    def __init__(self, kernel, step_budget=None, limits=None, log=None):
        self.kernel = kernel
        self.step_budget = step_budget or DEFAULT_STEP_BUDGET
        self.limits = limits or {}
        self.log = log
        self._next_id = 1
        self._tasks = {}
        self._by_kernel_task = {}

    # --- application lifecycle -------------------------------------------

    def load(self, package, name=None, task_id=None, limits=None, check_capabilities=True):
        """Verify a package and return an unspawned :class:`BinaryTask`."""
        effective = dict(self.limits)
        if limits:
            effective.update(limits)
        verification = verify(package, effective)
        if not verification.ok:
            raise VerificationError("; ".join(verification.errors))
        binary_id = self._next_id
        self._next_id += 1
        task = BinaryTask(self, binary_id, verification, name, self.step_budget, effective)
        task.kernel_task_id = task_id
        self._tasks[binary_id] = task
        if task_id is not None:
            self._by_kernel_task[task_id] = task
        if check_capabilities:
            self.check_capabilities(task)
        return task

    def spawn(self, package, name=None, limits=None):
        """Load a package and register it as a scheduler task."""
        task = self.load(package, name, limits=limits, check_capabilities=False)
        try:
            kernel_task_id = self.kernel.spawn(task.name, task.step)
        except Exception:
            self.unload(task)
            raise
        task.kernel_task_id = kernel_task_id
        self._by_kernel_task[kernel_task_id] = task
        try:
            self.check_capabilities(task)
        except Exception:
            self.kernel.stop(kernel_task_id)
            self.unload(task)
            raise
        return task

    def unload(self, task):
        task = self._resolve(task)
        self._tasks.pop(task.binary_id, None)
        if task.kernel_task_id is not None:
            self._by_kernel_task.pop(task.kernel_task_id, None)
        task.state = STOPPED
        return task

    def step(self, task, budget=None):
        """Execute at most ``budget`` instructions and return True while active."""
        return self._step_task(self._resolve(task), budget)

    def status(self, task):
        return self._resolve(task).info()

    def stop(self, task):
        task = self._resolve(task)
        task.state = STOPPED
        task.frames = []
        if task.kernel_task_id is not None:
            try:
                self.kernel.stop(task.kernel_task_id)
            except ValueError:
                pass
        return task.info()

    def tasks(self):
        return list(self._tasks.values())

    def check_capabilities(self, task):
        """Ensure declarations are known, granted, and cover every native call."""
        for capability in task.declared_capabilities:
            mapping = DECLARED_CAPABILITIES.get(capability)
            if mapping is None:
                raise CapabilityError("unsupported capability declaration %r" % capability)
            name, rights = mapping
            if name is not None:
                self._require(task, name, rights)
        for function in task.functions:
            start = task.code_offset + function["code_offset"]
            count = function["code_size"] // fmt.INSTRUCTION_SIZE
            for position in range(count):
                offset = start + position * fmt.INSTRUCTION_SIZE
                opcode, operand = struct.unpack_from("<BB", task.package, offset)
                if opcode != fmt.OP_CALL_NATIVE:
                    continue
                required = NATIVE_CAPABILITY.get(operand)
                if required is not None and required[0] not in task.declared_capabilities:
                    raise CapabilityError(
                        "application did not declare %r required by native %s"
                        % (required[0], NATIVE_NAMES.get(operand, operand))
                    )
        return True

    # --- scheduler integration -------------------------------------------

    def _step_task(self, task, budget=None):
        if task.state in (STOPPED, FAILED):
            return False
        task.state = RUNNING
        limit = budget if budget is not None else task.step_budget
        executed = 0
        try:
            while executed < limit and task.state == RUNNING:
                if not task.frames:
                    task.state = STOPPED
                    break
                if not self._execute_one(task):
                    break
                executed += 1
        except VMError as error:
            task.error = error
            task.state = FAILED
            return False
        except (ArithmeticError, IndexError, KeyError, TypeError, ValueError) as error:
            task.error = VMError(str(error))
            task.state = FAILED
            return False
        if task.state == RUNNING:
            task.state = READY
        return task.state not in (STOPPED, FAILED)

    def _execute_one(self, task):
        frame = task.frames[-1]
        function = task.functions[frame.function_index]
        if frame.ip < 0 or frame.ip * fmt.INSTRUCTION_SIZE >= function["code_size"]:
            raise VMError("control flow left the function body")
        pc = frame.ip
        offset = task.code_offset + function["code_offset"] + pc * fmt.INSTRUCTION_SIZE
        opcode, operand = struct.unpack_from("<BB", task.package, offset)
        frame.ip += 1

        if opcode == fmt.OP_NOP:
            return True
        if opcode == fmt.OP_PUSH_CONST:
            if operand >= len(task.constants):
                raise VMError("constant index out of range")
            self._push(task, task.constants[operand])
            return True
        if opcode == fmt.OP_PUSH_TRUE:
            self._push(task, True)
            return True
        if opcode == fmt.OP_PUSH_FALSE:
            self._push(task, False)
            return True
        if opcode == fmt.OP_PUSH_NULL:
            self._push(task, None)
            return True
        if opcode == fmt.OP_POP:
            self._pop(task)
            return True
        if opcode == fmt.OP_DUP:
            if not task.stack:
                raise VMError("operand stack underflow")
            self._push(task, task.stack[-1])
            return True
        if opcode == fmt.OP_LOAD_LOCAL:
            if operand >= len(frame.locals):
                raise VMError("local index out of range")
            self._push(task, frame.locals[operand])
            return True
        if opcode == fmt.OP_STORE_LOCAL:
            if operand >= len(frame.locals):
                raise VMError("local index out of range")
            frame.locals[operand] = self._pop(task)
            return True
        if opcode in (fmt.OP_ADD, fmt.OP_SUB, fmt.OP_MUL, fmt.OP_DIV, fmt.OP_MOD,
                      fmt.OP_EQ, fmt.OP_NE, fmt.OP_LT, fmt.OP_LE, fmt.OP_GT, fmt.OP_GE):
            right = self._pop(task)
            left = self._pop(task)
            self._push(task, self._binary_op(opcode, left, right))
            return True
        if opcode in (fmt.OP_NEG, fmt.OP_NOT):
            value = self._pop(task)
            if opcode == fmt.OP_NEG:
                self._require_numeric(value)
                self._push(task, -value)
            else:
                self._push(task, not value)
            return True
        if opcode == fmt.OP_JMP:
            frame.ip = operand
            return True
        if opcode in (fmt.OP_JZ, fmt.OP_JNZ):
            value = self._pop(task)
            taken = (not value) if opcode == fmt.OP_JZ else bool(value)
            if taken:
                frame.ip = operand
            return True
        if opcode == fmt.OP_CALL:
            self._call(task, operand)
            return True
        if opcode == fmt.OP_CALL_NATIVE:
            arity = fmt.NATIVE_ARITY.get(operand, 0)
            if len(task.stack) < arity:
                raise VMError("operand stack underflow")
            arguments = task.stack[len(task.stack) - arity:] if arity else []
            value, action = self.call_native(task, operand, arguments, pc)
            if action == NATIVE_BLOCK:
                frame.ip = pc
                task.state = BLOCKED
                return False
            if arity:
                del task.stack[-arity:]
            self._push(task, value)
            if action == NATIVE_YIELD:
                task.state = READY
                return False
            if action == NATIVE_STOP:
                task.state = STOPPED
                return False
            return True
        if opcode in (fmt.OP_RET, fmt.OP_RET_VOID):
            value = None if opcode == fmt.OP_RET_VOID else self._pop(task)
            self._return(task, value)
            return True
        if opcode == fmt.OP_HALT:
            task.state = STOPPED
            return False
        if opcode == fmt.OP_ARRAY_NEW:
            if operand > fmt.MAX_ARRAY_LENGTH:
                raise VMError("array length exceeds runtime limit")
            self._push(task, [None] * operand)
            return True
        if opcode == fmt.OP_ARRAY_GET:
            index = self._pop(task)
            array = self._pop(task)
            self._check_array(array, index)
            self._push(task, array[index])
            return True
        if opcode == fmt.OP_ARRAY_SET:
            value = self._pop(task)
            index = self._pop(task)
            array = self._pop(task)
            self._check_array(array, index)
            array[index] = value
            return True
        if opcode == fmt.OP_YIELD:
            task.state = READY
            return False
        raise VMError("invalid opcode %d" % opcode)

    # --- helpers ----------------------------------------------------------

    def _resolve(self, task):
        if isinstance(task, BinaryTask):
            return task
        if task in self._tasks:
            return self._tasks[task]
        if task in self._by_kernel_task:
            return self._by_kernel_task[task]
        raise VMError("unknown binary task %r" % (task,))

    def _push(self, task, value):
        if len(task.stack) >= task.max_stack:
            raise VMError("operand stack overflow")
        task.stack.append(value)

    def _pop(self, task):
        if not task.stack:
            raise VMError("operand stack underflow")
        return task.stack.pop()

    @staticmethod
    def _require_numeric(value):
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise VMError("numeric operand required")

    @staticmethod
    def _check_array(array, index):
        if not isinstance(array, list):
            raise VMError("array operand required")
        if not isinstance(index, int) or isinstance(index, bool):
            raise VMError("array index must be an integer")
        if index < 0 or index >= len(array):
            raise VMError("array index out of range")

    def _binary_op(self, opcode, left, right):
        if opcode == fmt.OP_ADD:
            return left + right
        if opcode == fmt.OP_SUB:
            self._require_numeric(left)
            self._require_numeric(right)
            return left - right
        if opcode == fmt.OP_MUL:
            self._require_numeric(left)
            self._require_numeric(right)
            return left * right
        if opcode == fmt.OP_DIV:
            self._require_numeric(left)
            self._require_numeric(right)
            if right == 0:
                raise VMError("division by zero")
            if isinstance(left, int) and isinstance(right, int):
                return left // right
            return left / right
        if opcode == fmt.OP_MOD:
            self._require_numeric(left)
            self._require_numeric(right)
            if right == 0:
                raise VMError("modulo by zero")
            return left % right
        if opcode == fmt.OP_EQ:
            return left == right
        if opcode == fmt.OP_NE:
            return left != right
        if opcode in (fmt.OP_LT, fmt.OP_LE, fmt.OP_GT, fmt.OP_GE):
            self._require_numeric(left)
            self._require_numeric(right)
            if opcode == fmt.OP_LT:
                return left < right
            if opcode == fmt.OP_LE:
                return left <= right
            if opcode == fmt.OP_GT:
                return left > right
            return left >= right
        raise VMError("invalid binary opcode %d" % opcode)

    def _call(self, task, function_index):
        if function_index >= len(task.functions):
            raise VMError("call target out of range")
        if len(task.frames) >= task.max_call_depth:
            raise VMError("call depth limit exceeded")
        function = task.functions[function_index]
        parameter_count = function["nparams"]
        if len(task.stack) < parameter_count:
            raise VMError("operand stack underflow")
        arguments = task.stack[len(task.stack) - parameter_count:] if parameter_count else []
        if parameter_count:
            del task.stack[-parameter_count:]
        locals_ = [None] * function["nlocals"]
        for index in range(parameter_count):
            locals_[index] = arguments[index]
        base = len(task.stack)
        task.frames.append(Frame(function_index, locals_, base))

    def _return(self, task, value):
        frame = task.frames.pop()
        del task.stack[frame.base:]
        if not task.frames:
            task.state = STOPPED
            return
        self._push(task, value)

    def _require(self, task, name, rights):
        holder = task.kernel_task_id
        if holder is None:
            holder = -1
        try:
            self.kernel.capabilities.require(holder, name, rights)
        except RuntimeError as error:
            raise CapabilityError(str(error))

    # --- native bindings --------------------------------------------------

    def call_native(self, task, native_id, arguments, pc):
        name = NATIVE_NAMES.get(native_id)
        if name is None:
            raise VMError("unknown native %d" % native_id)
        required = NATIVE_CAPABILITY.get(native_id)
        if required is not None:
            declared, kernel_name, rights = required
            if declared not in task.declared_capabilities:
                raise CapabilityError("application did not declare %r" % declared)
            self._require(task, kernel_name, rights)
        handler = getattr(self, "_native_" + name.lower())
        return handler(task, arguments, pc)

    def _native_log(self, task, arguments, pc):
        if self.log is not None:
            self.log(arguments[0])
        else:
            print(arguments[0])
        return None, NATIVE_CONTINUE

    def _native_yield(self, task, arguments, pc):
        return None, NATIVE_YIELD

    def _native_info(self, task, arguments, pc):
        status, value = self.kernel.abi.invoke(OP_GET_INFO)
        if status != STATUS_OK:
            raise VMError("System.info failed with status %d" % status)
        return value, NATIVE_CONTINUE

    def _native_port_send(self, task, arguments, pc):
        try:
            sent = self.kernel.port_send(arguments[0], arguments[1])
        except KeyError:
            raise VMError("unknown port %r" % arguments[0])
        return bool(sent), NATIVE_CONTINUE

    def _native_port_receive(self, task, arguments, pc):
        try:
            message = self.kernel.ports.port(arguments[0]).receive()
        except KeyError:
            raise VMError("unknown port %r" % arguments[0])
        if message is None:
            return None, NATIVE_BLOCK
        if isinstance(message, tuple):
            return message[1], NATIVE_CONTINUE
        return message, NATIVE_CONTINUE

    def _native_mem_alloc(self, task, arguments, pc):
        owner = task.kernel_task_id
        if owner is None:
            owner = -1
        try:
            block = self.kernel.memory.allocate(arguments[0], owner, arguments[1])
        except (KeyError, ValueError, TypeError) as error:
            raise VMError("Memory.alloc failed: %s" % error)
        return block, NATIVE_CONTINUE

    def _native_mem_free(self, task, arguments, pc):
        try:
            self.kernel.memory.release(arguments[0], arguments[1])
        except (KeyError, ValueError, TypeError) as error:
            raise VMError("Memory.free failed: %s" % error)
        return None, NATIVE_CONTINUE

    def _native_irq_timer(self, task, arguments, pc):
        self.kernel.irq_raise("timer")
        return None, NATIVE_CONTINUE

    def _native_task_stop(self, task, arguments, pc):
        target = arguments[0]
        if not isinstance(target, int) or isinstance(target, bool):
            raise VMError("Task.stop expects an integer task id")
        if target == task.kernel_task_id:
            return None, NATIVE_STOP
        try:
            self.kernel.stop(target)
        except ValueError as error:
            raise VMError("Task.stop failed: %s" % error)
        return None, NATIVE_CONTINUE
