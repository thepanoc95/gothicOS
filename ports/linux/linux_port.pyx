# cython: language_level=3
"""Cython userspace runtime for NULLKern on Linux.

This extension implements the deterministic NULLKern scheduling model in Cython and
optionally drives a small ``nullkern_lkm`` character device for kernel-side hints. The
kernel module never runs Cython or CPython code; it is only a transport boundary.
"""

from libc.stdint cimport uint64_t

cdef extern from "gothicos_linux_port.h":
    unsigned int GOTHICOS_LKM_IOC_GET_INFO
    unsigned int GOTHICOS_LKM_IOC_RAISE_IRQ
    unsigned int GOTHICOS_LKM_IOC_PORT_SEND
    unsigned int GOTHICOS_LKM_IOC_SCHED_HINT
    unsigned int GOTHICOS_LKM_IOC_HANDOFF_STATUS
    unsigned int GOTHICOS_LKM_IOC_HANDOFF_START
    unsigned int GOTHICOS_LKM_IOC_HANDOFF_STOP
    int GOTHICOS_LKM_OP_RAISE_IRQ
    int GOTHICOS_LKM_OP_PORT_SEND
    int GOTHICOS_LKM_OP_SCHED_HINT

import fcntl
import struct

ABI_MAJOR = 1
ABI_MINOR = 1
ABI_PATCH = 1
ABI_VERSION = (ABI_MAJOR, ABI_MINOR, ABI_PATCH)

STATUS_OK = 0
STATUS_INVALID_ARGUMENT = 1
STATUS_NOT_FOUND = 2
STATUS_WOULD_BLOCK = 3
STATUS_PERMISSION_DENIED = 7
STATUS_INTERNAL_ERROR = 6

TASK_READY = 1
TASK_RUNNING = 2
TASK_BLOCKED = 3
TASK_STOPPED = 4

ABILITY_STOPPED = 0
ABILITY_STARTING = 1
ABILITY_RUNNING = 2
ABILITY_FAILED = 4

SOFTBUS_REQUEST = 1
SOFTBUS_RESPONSE = 2
SOFTBUS_EVENT = 3

OP_GET_INFO = 0
OP_TASK_CREATE = 1
OP_TASK_STATE = 4
OP_YIELD = 7
OP_RUN = 8
OP_ABILITY_REGISTER = 30
OP_ABILITY_START = 31
OP_ABILITY_STOP = 32
OP_ABILITY_STATE = 33
OP_ABILITY_INFO = 34
OP_SOFTBUS_SEND = 35
OP_SOFTBUS_INFO = 37
OP_REMOTE_CALL = 38
OP_NODE_REGISTER = 40
OP_NODE_INFO = 41
OP_SCHEDULE = 42
OP_DISTRIBUTED_INFO = 43

MAX_TASKS = 8
MAILBOX_DEPTH = 4


cdef class LinuxPort:
    """Character-device client for the optional thin C kernel module."""

    cdef public object device_path
    cdef object _file

    def __cinit__(self, device_path="/dev/nullkern"):
        self.device_path = device_path
        self._file = None

    def open(self):
        if self._file is None:
            try:
                self._file = open(self.device_path, "rb+", buffering=0)
            except OSError:
                return False
        return True

    def close(self):
        if self._file is not None:
            self._file.close()
            self._file = None

    def available(self):
        return self._file is not None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
        return False

    def control(self, opcode, arg0=0, arg1=0, payload=0):
        cdef int c_opcode = int(opcode)
        cdef int c_arg0 = int(arg0)
        cdef int c_arg1 = int(arg1)
        cdef uint64_t c_payload = int(payload)
        cdef unsigned int request
        cdef bytes command
        cdef object result

        if c_opcode == GOTHICOS_LKM_OP_RAISE_IRQ:
            request = GOTHICOS_LKM_IOC_RAISE_IRQ
        elif c_opcode == GOTHICOS_LKM_OP_PORT_SEND:
            request = GOTHICOS_LKM_IOC_PORT_SEND
        elif c_opcode == GOTHICOS_LKM_OP_SCHED_HINT:
            request = GOTHICOS_LKM_IOC_SCHED_HINT
        else:
            raise ValueError("unknown NULLKern LKM opcode")

        if not self.open():
            return {"available": False, "result": None}
        command = struct.pack("<iiiiQ", c_opcode, c_arg0, c_arg1, 0, c_payload)
        result = fcntl.ioctl(self._file.fileno(), request, command)
        return {"available": True, "result": result}

    def info(self):
        cdef bytes response
        cdef object values

        if not self.open():
            return {
                "available": False,
                "device": self.device_path,
                "abi_version": ABI_VERSION,
            }
        response = fcntl.ioctl(
            self._file.fileno(), GOTHICOS_LKM_IOC_GET_INFO, bytearray(32)
        )
        values = struct.unpack("<8I", response)
        return {
            "available": True,
            "device": self.device_path,
            "abi_version": (values[0], values[1], values[2]),
            "capabilities": values[3],
            "irq_count": values[4],
            "message_count": values[5],
            "schedule_hints": values[6],
            "active_nodes": values[7],
        }

    def signal_irq(self, source, payload=0):
        return self.control(GOTHICOS_LKM_OP_RAISE_IRQ, source, 0, payload)

    def port_send(self, node_id, length=0):
        return self.control(GOTHICOS_LKM_OP_PORT_SEND, node_id, length, 0)

    def schedule_hint(self, node_id, score):
        return self.control(GOTHICOS_LKM_OP_SCHED_HINT, node_id, score, 0)

    def handoff_status(self):
        cdef bytes response

        if not self.open():
            return {"available": False, "device": self.device_path, "state": "unavailable"}
        response = fcntl.ioctl(
            self._file.fileno(), GOTHICOS_LKM_IOC_HANDOFF_STATUS, bytearray(512)
        )
        values = struct.unpack("<8I", response[:32])

        def text(offset):
            raw = response[offset:offset + 160]
            return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")

        return {
            "available": True,
            "device": self.device_path,
            "state": values[0],
            "enabled": bool(values[1]),
            "first_managed_cpu": values[2],
            "managed_cpu_count": values[3],
            "bound_cpu_count": values[4],
            "started_services": values[5],
            "init_image_size": values[6],
            "init_path": text(32),
            "shell_path": text(192),
            "error": text(352),
        }

    def handoff_start(self, enabled=True, first_managed_cpu=1, managed_cpu_count=0):
        cdef bytes command

        if not enabled:
            return {"available": self.open(), "started": False,
                    "error": "handoff is opt-in and requires enabled=True"}
        if first_managed_cpu == 0:
            return {"available": self.open(), "started": False,
                    "error": "CPU 0 is reserved for Linux"}
        if not self.open():
            return {"available": False, "started": False, "error": "device unavailable"}
        command = struct.pack("<4I", 1, first_managed_cpu, managed_cpu_count, 0)
        fcntl.ioctl(self._file.fileno(), GOTHICOS_LKM_IOC_HANDOFF_START, command)
        return self.handoff_status()

    def handoff_stop(self):
        if not self.open():
            return {"available": False, "device": self.device_path, "state": "unavailable"}
        fcntl.ioctl(self._file.fileno(), GOTHICOS_LKM_IOC_HANDOFF_STOP)
        return self.handoff_status()


cdef class LinuxKernel:
    """Cython implementation of the NULLKern runtime on a Linux host."""

    cdef object _tasks
    cdef object _ports
    cdef object _abilities
    cdef object _nodes
    cdef object _routes
    cdef object _messages
    cdef object _host
    cdef object _node_id
    cdef int _cursor
    cdef int _next_message_id

    def __cinit__(self, host=None, node_id="local"):
        self._tasks = [None] * MAX_TASKS
        self._ports = {}
        self._abilities = {}
        self._nodes = {}
        self._routes = {}
        self._messages = []
        self._host = None
        self._node_id = node_id
        self._cursor = 0
        self._next_message_id = 1
        self.register_node(node_id, node_id, 100, 0, {})
        self.attach_host(host)

    def attach_host(self, host):
        self._host = host
        return host

    cdef object _control(self, int opcode, int arg0=0, int arg1=0, uint64_t payload=0):
        if self._host is not None and hasattr(self._host, "control"):
            return self._host.control(opcode, arg0, arg1, payload)
        return None

    # --- task scheduling -------------------------------------------------

    def spawn(self, name, step):
        cdef int task_id
        if not callable(step):
            raise TypeError("step must be callable")
        for task_id in range(MAX_TASKS):
            if self._tasks[task_id] is None or self._tasks[task_id]["state"] == TASK_STOPPED:
                self._tasks[task_id] = {
                    "name": name,
                    "step": step,
                    "state": TASK_READY,
                    "mailbox": [],
                }
                return task_id
        raise RuntimeError("task table is full")

    def send(self, receiver_id, message, sender_id=-1):
        cdef object task
        if receiver_id < 0 or receiver_id >= MAX_TASKS:
            raise ValueError("invalid task id")
        task = self._tasks[receiver_id]
        if task is None or task["state"] == TASK_STOPPED:
            return False
        if len(task["mailbox"]) >= MAILBOX_DEPTH:
            return False
        task["mailbox"].append((sender_id, message))
        if task["state"] == TASK_BLOCKED:
            task["state"] = TASK_READY
        return True

    def receive(self, task_id):
        cdef object task
        if task_id < 0 or task_id >= MAX_TASKS:
            raise ValueError("invalid task id")
        task = self._tasks[task_id]
        if task is None or not task["mailbox"]:
            if task is not None and task["state"] == TASK_RUNNING:
                task["state"] = TASK_BLOCKED
            return None
        return task["mailbox"].pop(0)

    def run_once(self):
        cdef int offset
        cdef int task_id
        cdef object task
        cdef object keep_running

        for offset in range(MAX_TASKS):
            task_id = (self._cursor + offset) % MAX_TASKS
            task = self._tasks[task_id]
            if task is None or task["state"] != TASK_READY:
                continue
            self._cursor = (task_id + 1) % MAX_TASKS
            task["state"] = TASK_RUNNING
            try:
                keep_running = task["step"](self, task_id)
            except Exception:
                task["state"] = TASK_READY
                raise
            if task["state"] == TASK_RUNNING:
                if keep_running is False:
                    task["state"] = TASK_STOPPED
                else:
                    task["state"] = TASK_READY
            return task_id
        return None

    def run(self, max_steps=None):
        cdef int steps = 0
        while max_steps is None or steps < max_steps:
            if self.run_once() is None:
                break
            steps += 1
        return steps

    def task_info(self, task_id):
        cdef object task
        if task_id < 0 or task_id >= MAX_TASKS:
            raise ValueError("invalid task id")
        task = self._tasks[task_id]
        if task is None:
            raise KeyError("task slot is empty")
        return {
            "task_id": task_id,
            "name": task["name"],
            "state": task["state"],
            "mailbox_messages": len(task["mailbox"]),
        }

    # --- system abilities ------------------------------------------------

    def register_ability(self, name, subsystem, dependencies=None, start=None,
                         stop=None, invoke=None, description=""):
        cdef object depends = [] if dependencies is None else list(dependencies)
        if name in self._abilities:
            raise ValueError("ability already exists")
        self._abilities[name] = {
            "name": name,
            "subsystem": subsystem,
            "dependencies": depends,
            "start": start,
            "stop": stop,
            "invoke": invoke,
            "description": description,
            "state": ABILITY_STOPPED,
        }
        return self.ability_info(name)

    def ability_info(self, name=None):
        cdef object ability
        if name is not None:
            ability = self._abilities[name]
            return {
                "name": ability["name"],
                "subsystem": ability["subsystem"],
                "dependencies": list(ability["dependencies"]),
                "state": ability["state"],
                "description": ability["description"],
            }
        return {
            "abilities": [self.ability_info(key) for key in self._abilities],
        }

    def _start_ability(self, name, chain):
        cdef object ability
        cdef object dependency
        ability = self._abilities[name]
        if ability["state"] == ABILITY_RUNNING:
            return ability
        if name in chain:
            raise ValueError("ability dependency cycle")
        chain.append(name)
        try:
            for dependency in ability["dependencies"]:
                self._start_ability(dependency, chain)
            if ability["state"] == ABILITY_STARTING:
                ability["state"] = ABILITY_FAILED
                raise ValueError("ability is already starting")
            ability["state"] = ABILITY_STARTING
            try:
                if ability["start"] is not None:
                    ability["start"](self)
            except Exception:
                ability["state"] = ABILITY_FAILED
                raise
            ability["state"] = ABILITY_RUNNING
            return ability
        finally:
            chain.pop()

    def start_ability(self, name):
        return self.ability_info(self._start_ability(name, [])["name"])

    def stop_ability(self, name):
        cdef object ability = self._abilities[name]
        if ability["stop"] is not None:
            ability["stop"](self)
        ability["state"] = ABILITY_STOPPED
        return self.ability_info(name)

    def ability_state(self, name):
        return self._abilities[name]["state"]

    def invoke_ability(self, name, arguments=None):
        cdef object ability = self._abilities[name]
        if ability["state"] != ABILITY_RUNNING:
            self._start_ability(name, [])
        if ability["invoke"] is None:
            raise RuntimeError("ability has no invoke handler")
        return ability["invoke"]([] if arguments is None else arguments)

    # --- soft bus and distributed nodes ---------------------------------

    def register_node(self, node_id, name, capacity=100, load=0, features=None):
        cdef object info = {
            "node_id": node_id,
            "name": name,
            "capacity": capacity,
            "load": load,
            "features": dict(features or {}),
            "abilities": {},
        }
        self._nodes[node_id] = info
        return self.node_info(node_id)

    def node_info(self, node_id=None):
        cdef object info
        if node_id is not None:
            info = self._nodes[node_id]
            return {
                "node_id": node_id,
                "name": info["name"],
                "capacity": info["capacity"],
                "load": info["load"],
                "features": dict(info["features"]),
            }
        return {"nodes": [self.node_info(key) for key in self._nodes]}

    def register_local_ability(self, name, handler, dependencies=None, subsystem="application"):
        cdef object info = self.register_ability(
            name, subsystem, dependencies, None, None, handler, ""
        )
        self._nodes[self._node_id]["abilities"][name] = handler
        self._routes[name] = self._node_id
        self._nodes[self._node_id]["features"][name] = True
        return info

    def register_remote_ability(self, name, node_id):
        if node_id not in self._nodes:
            raise KeyError("node not found")
        self._routes[name] = node_id
        self._nodes[node_id]["features"][name] = True
        return {"ability": name, "node_id": node_id}

    def rank_nodes(self, ability_name, candidates=None):
        cdef object node_ids = list(self._nodes.keys()) if candidates is None else list(candidates)
        cdef object ranking = []
        cdef object node
        cdef object node_id
        cdef int score
        for node_id in node_ids:
            node = self._nodes[node_id]
            score = node["capacity"] - node["load"]
            if ability_name in node["features"]:
                score += 20
            ranking.append({"node_id": node_id, "score": score, "load": node["load"]})
        return sorted(ranking, key=lambda item: (-item["score"], item["node_id"]))

    def schedule(self, ability_name, candidates=None):
        cdef object ranking = self.rank_nodes(ability_name, candidates)
        cdef object node_id = ranking[0]["node_id"] if ranking else None
        return {"node_id": node_id, "ranking": ranking}

    def softbus_send(self, target, kind, payload, correlation_id=0):
        cdef object message
        if target not in self._nodes:
            raise KeyError("node not found")
        message = {
            "id": self._next_message_id,
            "source": self._node_id,
            "target": target,
            "kind": kind,
            "payload": payload,
            "correlation_id": correlation_id,
        }
        self._next_message_id += 1
        self._messages.append(message)
        self._control(GOTHICOS_LKM_OP_PORT_SEND, 0, len(str(payload)), 0)
        return message["id"]

    def softbus_info(self):
        return {
            "node_id": self._node_id,
            "messages": len(self._messages),
            "nodes": list(self._nodes.keys()),
        }

    def remote_call(self, ability_name, arguments=None, node_id=None):
        cdef object target
        cdef object node
        cdef object result
        target = node_id if node_id is not None else self._routes.get(ability_name)
        if target is None:
            target = self.schedule(ability_name)["node_id"]
        if target is None:
            raise LookupError("no route to ability")
        node = self._nodes[target]
        if ability_name not in node["abilities"]:
            raise KeyError("ability not hosted on node")
        result = self.invoke_ability(ability_name, arguments)
        self._control(GOTHICOS_LKM_OP_SCHED_HINT, 0, 0, 0)
        return result

    # --- ABI-compatible entry point -------------------------------------

    def _abi_dispatch(self, opcode, arguments):
        if opcode == OP_GET_INFO:
            return self.info()
        if opcode == OP_TASK_CREATE:
            name, step = arguments
            return self.spawn(name, step)
        if opcode == OP_TASK_STATE:
            return self.task_info(arguments[0])["state"]
        if opcode == OP_YIELD:
            return self.run_once()
        if opcode == OP_RUN:
            return self.run(arguments[0] if arguments else None)
        if opcode == OP_ABILITY_REGISTER:
            return self.register_ability(*arguments)
        if opcode == OP_ABILITY_START:
            return self.start_ability(arguments[0])
        if opcode == OP_ABILITY_STOP:
            return self.stop_ability(arguments[0])
        if opcode == OP_ABILITY_STATE:
            return self.ability_state(arguments[0])
        if opcode == OP_ABILITY_INFO:
            return self.ability_info(arguments[0] if arguments else None)
        if opcode == OP_SOFTBUS_SEND:
            return self.softbus_send(*arguments)
        if opcode == OP_SOFTBUS_INFO:
            return self.softbus_info()
        if opcode == OP_REMOTE_CALL:
            return self.remote_call(*arguments)
        if opcode == OP_NODE_REGISTER:
            return self.register_node(*arguments)
        if opcode == OP_NODE_INFO:
            return self.node_info(arguments[0] if arguments else None)
        if opcode == OP_SCHEDULE:
            return self.schedule(arguments[0], arguments[1] if len(arguments) > 1 else None)
        if opcode == OP_DISTRIBUTED_INFO:
            return {
                "node_id": self._node_id,
                "routes": dict(self._routes),
                "nodes": list(self._nodes.keys()),
            }
        raise ValueError("unsupported opcode")

    def abi_invoke(self, opcode, *arguments):
        try:
            return STATUS_OK, self._abi_dispatch(opcode, arguments)
        except LookupError:
            return STATUS_NOT_FOUND, None
        except (TypeError, ValueError):
            return STATUS_INVALID_ARGUMENT, None
        except RuntimeError as error:
            if "no route" in str(error):
                return STATUS_NOT_FOUND, None
            return STATUS_INTERNAL_ERROR, None
        except Exception:
            return STATUS_INTERNAL_ERROR, None

    def info(self):
        host_info = None
        if self._host is not None and hasattr(self._host, "info"):
            host_info = self._host.info()
        return {
            "abi_version": ABI_VERSION,
            "capabilities": 31,
            "max_tasks": MAX_TASKS,
            "mailbox_depth": MAILBOX_DEPTH,
            "node_id": self._node_id,
            "features": {
                "tasks": True,
                "subsystems": True,
                "softbus": True,
                "distributed": True,
                "lkm_transport": host_info is not None,
            },
            "lkm": host_info,
        }


def kernel_from_device(device_path="/dev/nullkern", node_id="local"):
    """Create a LinuxKernel backed by the optional char-device transport."""
    port = LinuxPort(device_path)
    return LinuxKernel(port, node_id)
