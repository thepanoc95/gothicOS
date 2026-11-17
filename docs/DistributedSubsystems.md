# NULLKern distributed subsystems

NULLKern now includes a HarmonyOS-inspired subsystem slice that runs on both MicroPython
devices and CPython hosts. The implementation lives in
[`internal/nullkern/subsystems/`](../internal/nullkern/subsystems/).

## Components

- **System Ability Manager** (`kernel.abilities`): named abilities with subsystem labels,
  dependency lists, deterministic lifecycle states, on-demand start, dependent-first
  stop, capability checks, and invoke handlers.
- **Soft bus** (`kernel.softbus`): node-local endpoints with pluggable transports.
  The built-in `LoopbackTransport` routes request, response, and event envelopes between
  multiple in-process nodes without threads or sockets.
- **Remote ability proxy** (`kernel.remote`): maps ability names to nodes, sends requests
  over the soft bus, starts the remote ability on demand, and records placement counters.
- **Distributed scheduler** (`kernel.scheduler`): node capacity/load registry, ability
  advertisements, deterministic placement scores, and explicit routes.
- **Distributed facade** (`kernel.distributed`): ties the pieces together, installs core
  abilities during boot, and creates additional node endpoints.

Core abilities installed at boot:

| Ability | Subsystem |
| --- | --- |
| `system.ability` | `foundation` |
| `system.softbus` | `distributed` |
| `system.scheduler` | `distributed` |
| `system.remote.ability` | `distributed` |

Boot stages are now `platform`, `topology`, `memory`, `ports`, `irq`, `drivers`,
`subsystems`, `distributed`, `abi`.

## Usage

```python
from internal.nullkern.kernel import Kernel
from internal.nullkern.subsystems import MESSAGE_EVENT

kernel = Kernel()
result = kernel.boot()

kernel.add_node("phone-b", "phone-b", capacity=100, load=10)
kernel.distributed.register_local_ability(
    "camera.capture", lambda arguments: ["captured", arguments[0]]
)
kernel.distributed.register_remote_ability("camera.capture", "phone-b")

print(kernel.remote_call("camera.capture", [1024]))
# ['captured', 1024]

bus = kernel.distributed.add_node("watch", "watch")["bus"]
bus.subscribe(MESSAGE_EVENT, lambda envelope: print(envelope["payload"]))
kernel.softbus_send("watch", MESSAGE_EVENT, {"step": 1})
```

The loopback transport is intentionally synchronous: a request is delivered to the target
node, the target host invokes the ability, and the response is delivered back before
`remote_call` returns. This makes scheduling and routing deterministic and testable. Real
transports can implement `send(envelope)` (and optionally `register(node_id, bus)`) without
changing the ability manager.

## ABI

Subsystem operations are additive ABI v1.1.1 operations `30` through `43`:

| Range | Operations |
| --- | --- |
| 30–34 | ability register, start, stop, state, info |
| 35–37 | soft bus send, subscribe, info |
| 38–39 | remote ability call, route |
| 40–43 | node register, node info, scheduling hints, distributed info |

Abilities use the `ability` capability, soft bus operations use `softbus`, and scheduling
and routing use `distributed`. Spawned tasks receive all three capabilities; they can be
revoked with the existing capability ABI.

ABI v1.1.1 is a patch release: Umbra Bytecode continues to target ABI major/minor `1.1`,
so existing `.umb` packages remain valid.
