# OSBuild

OSBuild validates board configurations and builds local recipes. Use a recipe directly
with `gothicos build --config path/to/build.json`, or reference it from a board's local
`path` or `presoft.kernel.path`. Board recipes are processed in boot-then-software order;
duplicate recipe paths run once. Remote `url` entries are descriptive only.

Board documents may include `@fdt: {"path": "relative/or/absolute/in-root.dtb"}`.
OSBuild validates the FDT and counts enabled `/cpus` nodes, then sets
`GOTHICOS_CPU_COUNT`, `GOTHICOS_MULTICORE`, and `GOTHICOS_FDT_PATH` for each referenced
recipe action. Without an FDT, the count is inferred from the board's `@cpu` entries.
The FDT path must stay inside the project root.

## Command recipes

Command recipes are ordered command steps:

```json
{
  "$schema": "../src/gothicos/OSBuild/schema/osbuild.json",
  "@build": {
    "name": "example-firmware",
    "steps": [
      {
        "name": "compile",
        "run": ["make", "all"],
        "cwd": "%PROJROOT/firmware",
        "env": {
          "BUILD_MODE": "release"
        }
      }
    ]
  }
}
```

`run` is an argument array, not a shell command string. Commands run directly and
sequentially; OSBuild stops at the first launch or nonzero-exit error. `cwd` defaults
to the project root and `env` adds variables to the inherited environment.

## Source recipes

Source recipes compile declared files into a named output collection:

```json
{
  "$schema": "../src/gothicos/OSBuild/schema/osbuild.json",
  "@build": {
    "recipe": {
      "name": "nullkernRT",
      "type": "Runtime Kernel",
      "kernel-abi": "1.0.0",
      "entry": ["build.py"],
      "outputs": {
        "KernelCollection": {
          "micropython-bytecode": "v6",
          "mpy-cross-arch": "armv6m",
          "source.files": ["kernel.py", "oshardware.pyx"]
        }
      }
    }
  }
}
```

Entry scripts run in order, from the recipe's directory, using the current Python
interpreter. For each output, `.py` files are compiled by `mpy-cross`; the compiler
release determines the emitted bytecode version, which must match the declared
`micropython-bytecode` target. Optional `mpy-cross-arch` is forwarded as `-march` for
native and Viper emitters (for example, `armv6m` for RP2040). `.pyx` files are translated
to C with Cython. Files retain their project-relative paths under
`build/<recipe>/<output>/`; the generated suffix is `.mpy` or `.c`. The `.c` files are
generated C sources, not linked firmware. Other source extensions are rejected. All
declared inputs must exist inside the project root.

The recipe `name` and output names are also used as directory names and may contain
letters, digits, `.`, `_`, and `-`. The `entry` and `source.files` arrays must be
non-empty, and `outputs` must contain at least one collection. `.py` inputs require
`mpy-cross`; `.pyx` inputs require Cython.

Optional `kernel-abi` metadata must be `MAJOR.MINOR.PATCH`; OSBuild exports it to entry
and compile actions as `GOTHICOS_KERNEL_ABI`. The nullkern v1 ABI is defined in
`internal/nullkern/abi.py` and mirrored by [`SDK/`](../SDK/README.md). It exposes
numeric operations for info, task lifecycle and inspection, IPC, yielding, dispatch,
named ports, fixed-block memory, deferred interrupts, drivers, capabilities, and boot
metadata. Every call returns `(status, value)` with stable status codes; callers should
use the exported constants rather than interpreting exceptions.

The v1.0.0 operations are `GET_INFO=0`, `TASK_CREATE=1` (`name, callback`),
`TASK_STOP=2` (`task_id`), `TASK_RESUME=3` (`task_id`), `TASK_STATE=4` (`task_id`),
`IPC_SEND=5` (`receiver_id, message[, sender_id]`), `IPC_RECEIVE=6` (`[task_id]`),
`YIELD=7`, `RUN=8` (`[max_steps]`), and `TASK_INFO=9` (`task_id`). The v1.1.0 extensions
are `PORT_OPEN=10`, `PORT_CLOSE=11`, `PORT_SEND=12`, `PORT_RECEIVE=13`, `PORT_INFO=14`,
`MEM_POOL_CREATE=15`, `MEM_ALLOC=16`, `MEM_FREE=17`, `MEM_WRITE=18`, `MEM_READ=19`,
`IRQ_REGISTER=20`, `IRQ_UNREGISTER=21`, `IRQ_RAISE=22`, `IRQ_SERVICE=23`,
`DRIVER_INFO=24`, `DRIVER_IO=25`, `CAP_GRANT=26`, `CAP_REVOKE=27`, `CAP_INFO=28`, and
`BOOT_INFO=29`. The v1.1.1 subsystem operations are `ABILITY_REGISTER=30` through
`DISTRIBUTED_INFO=43` (abilities, soft bus, remote calls, nodes, and scheduling); see
[Distributed Subsystems](DistributedSubsystems.md) for the full list. Statuses are
`OK=0`, `INVALID_ARGUMENT=1`, `NOT_FOUND=2`,
`WOULD_BLOCK=3`, `QUEUE_FULL=4`, `UNSUPPORTED=5`, `INTERNAL_ERROR=6`,
`PERMISSION_DENIED=7`, `ALREADY_EXISTS=8`, and `OUT_OF_MEMORY=9`. `GET_INFO` reports the
ABI version tuple, feature bitmask, task/mailbox limits, detected core count, and a
`features` map. Task states are `READY=1`, `RUNNING=2`, `BLOCKED=3`, and `STOPPED=4`.

`SDK/build.json` is a source recipe that compiles the Cython host tools and the device
client; `SDK/examples/hello/build.json` shows the same pattern for an application.

FDT CPU topology is available to recipe entry scripts through the `GOTHICOS_*`
environment values above. Runtime code can parse the same DTB with
`Kernel.from_fdt(path_or_bytes)`; the DTB must also be present on the device at that path.

## Paths, validation, and building

Relative config and recipe-file paths resolve from the project root (current directory
by default). Source recipe entry and input paths resolve from the recipe directory.
`%PROJROOT` expands to the root in paths, command arguments, working directories, and
environment values. Recipe files, entry scripts, and source files must stay inside the
project root.

```sh
uv run gothicos validate --config recipes/build.json
uv run gothicos build --config recipes/build.json --dry-run
uv run gothicos build --config recipes/build.json
```

Validation resolves referenced recipes and source inputs without running commands.
`--dry-run` prints the planned steps without executing them or creating output
directories. Build outputs are written under the project root's `build/` directory.
