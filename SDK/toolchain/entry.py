"""OSBuild entry point for the SDK recipe.

The real work is performed by OSBuild's source compilation steps. This entry validates
the kernel ABI exported by the recipe and fails early on an incompatible version.
"""

import os

SUPPORTED_MAJOR = 1
SUPPORTED_MINOR = (0, 1)


def main():
    version = os.environ.get("GOTHICOS_KERNEL_ABI", "1.1.1")
    parts = version.split(".")
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        raise SystemExit("invalid GOTHICOS_KERNEL_ABI: %s" % version)
    major, minor = int(parts[0]), int(parts[1])
    if major != SUPPORTED_MAJOR or minor not in SUPPORTED_MINOR:
        raise SystemExit("SDK does not support kernel ABI %s" % version)
    print("gothicOS SDK entry: kernel ABI %s accepted" % version)


if __name__ == "__main__":
    main()
