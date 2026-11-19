# UmbraTS v0.1 implementation

This repository contains a working vertical slice of the UmbraTS design: a small
TypeScript-like frontend, an AOT compiler that emits portable Umbra Bytecode, a verifier,
and a stack-based interpreter running inside the NULLKernRT binary execution module.

The compiler is host-side CPython code in [`SDK/umbra/`](../SDK/umbra/). The runtime is
MicroPython-compatible code in [`internal/nullkern/binary/`](../internal/nullkern/binary/).
The kernel ABI remains on the `1.1` line (currently `1.1.1`); applications reach kernel
services through `kernel.binary` and SDK-backed native bindings rather than a new syscall
range.

## Supported language subset

| Feature | Status |
| --- | --- |
| `@Application` entry class with `static main(): void` | supported |
| `let` / `const`, lexical blocks, type inference | supported |
| Primitive types: `boolean`, `number`, `string`, `null`, `undefined`, explicit integer/float types | supported |
| `type` aliases | parsed and resolved |
| Functions and static class methods | supported |
| `if` / `else`, `while`, `for`, `return`, `yield` | supported |
| `++`, `--`, `+=`, `-=`, `*=`, `/=` | supported |
| Arrays and array indexing/assignment | supported (`uint8[256]` fixed-size syntax is not yet parsed) |
| `@Capability("...")` metadata | supported; validated at load |
| `static readonly` constant fields | supported for resource metadata |
| SDK namespaces: `System`, `Port`, `Memory`, `Interrupt`, `Task` | supported native bindings |
| Imports and `export` | parsed; single-package compilation only |
| Classes, interfaces, inheritance, generics, enums, async/await, exceptions | deferred |

The compiler rejects unsupported constructs instead of generating ambiguous code.

### Example

```typescript
@Application
@Capability("system.logging")
class Main {
    static readonly MEMORY_LIMIT: uint32 = 32768;

    static double(value: uint32): uint32 {
        return value * 2;
    }

    static main(): void {
        let values: uint32[] = [1, 2, 3];
        let total: uint32 = 0;
        for (let i: uint32 = 0; i < 3; i++) {
            total = total + Main.double(values[i]);
        }
        System.log(total);
    }
}
```

## Compilation pipeline

```text
.uts source
  -> lexer / parser / AST
  -> semantic checks and static types
  -> linear IR with labels
  -> constant folding
  -> Umbra Bytecode v0.1 container (.umb)
  -> runtime verifier
  -> BinaryExecutionModule VM
```

The optimizer folds constant arithmetic but preserves label markers, so jump targets
cannot shift. Instruction operands are resolved when the binary is encoded.

## Bytecode container

`SDK/umbra/format.py` and `internal/nullkern/binary/format.py` mirror each other and are
checked by tests.

- Magic: `UMB1`
- Version: `0.1`
- ABI: `1.1`
- 24-byte header with entry index, section sizes, and function/constant counts
- 8-byte function records with local count, parameter count, code offset/size, and flags
- Two-byte instructions: one opcode byte and one operand byte
- Constant tags: null, bool, int32, float64, UTF-8 string
- Metadata: JSON with application name, ABI, entry, functions, capabilities, resources

Instruction groups cover constants and locals, arithmetic/comparison/logic, branches and
calls, returns/halt/yield, arrays, and native calls. `OP_LABEL` is assembler-only and is
never emitted.

## Verification

Before a package can be loaded, the runtime validates:

- magic, version, ABI compatibility, and reserved fields
- section bounds and exact package length
- constant encoding and string bounds
- function records, local/parameter counts, and code boundaries
- opcodes, operand ranges, jump targets, native IDs, and function references
- control-flow graph fall-through and consistent stack depth at merge points
- metadata, declared entry, capabilities, and resource limits

Invalid packages are rejected with a `VerificationError`.

## Runtime model

`BinaryExecutionModule` is available as `kernel.binary`:

- `load(package, name=None, task_id=None)` verifies and creates an unspawned task
- `spawn(package, name=None)` verifies and registers the task with the scheduler
- `step(task, budget=None)` executes a bounded slice (default 128 instructions)
- `status(task)` / `stop(task)` inspect or terminate a task
- `run_package(package, max_steps=None, log=None)` is a host/CLI convenience helper

Task states are `READY`, `RUNNING`, `BLOCKED`, `STOPPED`, and `FAILED`. Each task has an
entry frame, bounded operand stack, bounded call depth, and declared native capabilities.
`yield` returns control to the cooperative scheduler and resumes on the next dispatch. An
empty `Port.receive` marks the VM task `BLOCKED` and retries on the next slice.

Native bindings are checked twice: at load, every native call must be covered by a
`@Capability` declaration and granted by the kernel; at call time, the corresponding
kernel capability is required again. Declaring a capability never grants it by itself.

`Port.receive` is dynamically typed because kernel ports can carry strings, integers and
other values. Its checker type is `unknown`, so it can be assigned to the declared UmbraTS
type that the application expects.

## CLI

```sh
# Compile and verify a single source file
gothicos-sdk umbra build main.uts --output main.umb --metadata main.meta.json
gothicos-sdk umbra check main.umb

# Run on a fresh kernel using the reference VM
gothicos-sdk umbra run main.uts --steps 1000

# Create and build a UmbraTS application manifest
gothicos-sdk new demo --language umbrats --directory SDK/examples
gothicos-sdk build SDK/examples/demo --output build/demo
```

`gothicos-sdk build` accepts `.uts` sources alongside `.py` sources. Umbra sources become
`.umb` packages; Python sources still use `mpy-cross`. Umbra-only manifests report
`bytecode: "umbra-0.1"`.

An application manifest may declare the target profile:

```json
{
  "name": "demo",
  "entry": "main.uts",
  "sources": ["main.uts"],
  "sdk": ">=1.0.0,<2.0.0",
  "abi": ">=1.0.0,<2.0.0",
  "target": { "bytecode": "umbra-0.1", "profile": "embedded" }
}
```

## Host API

```python
from umbra import compile_source

result = compile_source(source, "demo")
package = result.package        # bytes ready to write as .umb
metadata = result.metadata      # entry, capabilities, resource requirements
```

Compilation errors raise `CompileError` with source location information where available.

## Tests

```sh
.venv/bin/python -m unittest discover -s tests
.venv/bin/gothicos build --config internal/nullkern/build.json
.venv/bin/gothicos build --config SDK/build.json
```

`tests/test_umbra.py` covers lexer/parser diagnostics, type errors, bytecode roundtrips,
rejection of corrupted packages, VM execution, native bindings, capability enforcement,
kernel integration, and the SDK CLI.
