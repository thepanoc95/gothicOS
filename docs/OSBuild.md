# OSBuild recipe format

An OSBuild recipe is a JSON file that describes an ordered list of command steps.
Use it directly with `gothicos build --config path/to/build.json`, or reference it
from a board configuration's local `path` or `presoft.kernel.path`.

## Structure

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
      },
      {
        "name": "package",
        "run": ["python3", "scripts/package.py", "build/firmware.bin"]
      }
    ]
  }
}
```

The `$schema` value should point to `src/gothicos/OSBuild/schema/osbuild.json`;
the example assumes the recipe is saved in a top-level `recipes/` directory.
The `@build` object and its `name` and `steps` properties are required. A recipe
must contain at least one step.

### Build fields

| Field | Required | Description |
| --- | --- | --- |
| `name` | Yes | Descriptive recipe name shown in dry-run output. |
| `steps` | Yes | Non-empty array of steps, executed in listed order. |

### Step fields

| Field | Required | Description |
| --- | --- | --- |
| `name` | Yes | Descriptive step name used in output and error messages. |
| `run` | Yes | Non-empty array of command and arguments. |
| `cwd` | No | Existing working directory, resolved from the project root. Defaults to the project root. |
| `env` | No | String-to-string environment variables added to the inherited process environment. |

`run` is an argument array, not a shell command string. OSBuild invokes it directly,
so shell syntax such as pipes, redirects, glob expansion, and variable expansion is
not interpreted. Invoke a shell explicitly as a command if a step needs shell behavior.

## Paths and variables

Relative config, recipe, and `cwd` paths are resolved from the project root, which
defaults to the current working directory. Use `--project-root` to select another
root. The `%PROJROOT` token expands to that root in recipe paths, `cwd`, command
arguments, and environment values. Referenced recipes and step working directories
must exist inside the project root.

## Running recipes

```sh
# Check a standalone recipe and resolve its steps
uv run gothicos validate --config recipes/build.json

# Show commands without running them
uv run gothicos build --config recipes/build.json --dry-run

# Run the recipe
uv run gothicos build --config recipes/build.json
```

Board configs can reference the same recipe format. OSBuild follows local JSON
recipe paths from the boot configuration and software entries, running their steps
in that order. Duplicate recipe paths are run once per board build. Entries that
only provide a `url` are descriptive and are not fetched or built automatically.

Steps run sequentially. If a command cannot be started or exits unsuccessfully,
the build stops and reports the failing step.