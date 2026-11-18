"""OSBuild entry point for the native component recipe.

Compile flags are emitted by OSBuild itself; this entry point only checks that
the shared C contract and the example component are present.
"""

from pathlib import Path

RECIPE_ROOT = Path(__file__).resolve().parent
REQUIRED_FILES = (
    "../SDK/include/gothicos/kernel_abi.h",
    "include/gothicos/native_component.h",
    "src/gothicos_native.c",
    "components/echo/echo_component.c",
)


def main() -> None:
    missing = [name for name in REQUIRED_FILES if not (RECIPE_ROOT / name).is_file()]
    if missing:
        raise SystemExit("missing native sources: " + ", ".join(missing))


if __name__ == "__main__":
    main()
