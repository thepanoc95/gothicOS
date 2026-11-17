# gothicOS SDK / toolchain

The SDK packages everything needed to write applications for `nullkernRT`:

| Path | Purpose |
| --- | --- |
| `gothicos_sdk/` | Host-side Python client, app manifest tooling, and `gothicos-sdk` CLI |
| `umbra/` | UmbraTS lexer, parser, type checker, optimizer, and bytecode compiler |
| `device/nullkern_sdk.py` | Tiny MicroPython client compiled alongside your app |
| `toolchain/` | Cython host tools and the toolchain build script |
| `include/gothicos/` | Stable C header contract for native clients |
| `examples/hello/` | Minimal application using ports and the client |
| `build.json` | OSBuild source recipe for the SDK itself |

## Install and check

```sh
uv pip install -e SDK
gothicos-sdk version
gothicos-sdk abi
gothicos-sdk abi --header
```

## Create and build an app

```sh
gothicos-sdk new hello --directory SDK/examples
gothicos-sdk check SDK/examples/hello
gothicos-sdk build SDK/examples/hello --output build/hello
```

`build` compiles each `.py` source with `mpy-cross` and writes `app.json` into the
output directory. Use `--arch armv6m` for RP2040 native/Viper emitters, or declare it
in the manifest under `target.arch`.

An app manifest looks like this:

```json
{
  "name": "hello",
  "entry": "main.py",
  "sources": ["main.py"],
  "sdk": ">=1.0.0,<2.0.0",
  "abi": ">=1.0.0,<2.0.0",
  "target": { "bytecode": "v6", "arch": "armv6m" }
}
```

## UmbraTS applications

The SDK also compiles the supported UmbraTS subset directly to Umbra Bytecode:

```sh
gothicos-sdk new demo --language umbrats --directory SDK/examples
gothicos-sdk build SDK/examples/demo --output build/demo
gothicos-sdk umbra build SDK/examples/demo/main.uts --output demo.umb
gothicos-sdk umbra check demo.umb
gothicos-sdk umbra run demo.umb
```

`build` writes `.umb` files for `.uts` sources and leaves the existing `mpy-cross` path
in place for `.py` sources. Umbra-only manifests report `bytecode: "umbra-0.1"`. See
[UmbraTS documentation](../docs/UmbraTS.md) for the language subset, bytecode container,
capability rules, and runtime model.

On the device, `nullkern_sdk.KernelClient` wraps `kernel.abi.invoke`, so every call
returns `(status, value)` and callbacks stay ordinary one-step Python functions.

## Toolchain

Build the Cython host tools directly, or through OSBuild:

```sh
python SDK/toolchain/build.py --out build/SDK/Toolchain
gothicos build --config SDK/build.json
```

`SDK/build.json` also compiles `device/nullkern_sdk.py` to `nullkern_sdk.mpy` with the
`armv6m` emitter. Copy that file next to your app on the device.

## ABI compatibility

The SDK targets kernel ABI `>=1.0.0,<2.0.0` and mirrors v1.1.1 constants in
`gothicos_sdk.abi`. `gothicos-sdk check` parses the `sdk`/`abi` constraints before a
build. The C contract lives in `include/gothicos/kernel_abi.h`.
