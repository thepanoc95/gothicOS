# gothicOS

## OSBuild

OSBuild validates a board configuration and runs the local build recipes referenced by it.
Recipe commands are argument arrays and run directly (without a shell), in order, from the
board's boot configuration followed by its software entries. External `url` entries are
descriptive only; OSBuild does not clone or build them.

```sh
uv run gothicos validate --config src/gothicos/OSBuild/boards/rp2-rpiw.json
uv run gothicos build --config src/gothicos/OSBuild/boards/rp2-rpiw.json --dry-run
uv run gothicos build --config src/gothicos/OSBuild/boards/rp2-rpiw.json
uv run gothicos validate --config internal/nullkern/build.json
uv run gothicos build --config internal/nullkern/build.json --dry-run
uv run gothicos build --config internal/nullkern/build.json
```

Paths in configs are relative to the project root (the current directory by default).
`%PROJROOT` can be used in recipe paths, command arguments, working directories, and
environment values. Recipe files and working directories must stay inside the project root.

Each referenced `build.json` uses the same schema as board configs. For a recipe saved as
`recipes/build.json`, a recipe has this shape:

```json
{
  "$schema": "../src/gothicos/OSBuild/schema/osbuild.json",
  "@build": {
    "name": "example",
    "steps": [
      {
        "name": "compile",
        "run": ["make", "all"],
        "cwd": "%PROJROOT",
        "env": {
          "BUILD_MODE": "release"
        }
      }
    ]
  }
}
```

The bundled Pico W board references `internal/nullkern/build.json` as its local recipe.
Populate it with a valid `@build` document before building that board.
