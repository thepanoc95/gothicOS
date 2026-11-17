# cython: language_level=3
"""Host-side build helpers shared by OSBuild and the SDK CLI."""

from pathlib import Path


def normalize_path(value):
    return Path(str(value)).expanduser().resolve()


def project_environment(abi_version="1.1.1", cpu_count=1):
    environment = {"GOTHICOS_KERNEL_ABI": abi_version}
    if cpu_count is not None:
        environment["GOTHICOS_CPU_COUNT"] = str(cpu_count)
        environment["GOTHICOS_MULTICORE"] = "1" if cpu_count > 1 else "0"
    return environment


def source_command(source, output, compiler="mpy-cross", arch=None):
    command = [compiler]
    if arch:
        command.append("-march=" + arch)
    command.extend(("-o", str(output), str(source)))
    return tuple(command)


def describe_build(abi_version="1.1.1", cpu_count=1, arch=None):
    return {
        "environment": project_environment(abi_version, cpu_count),
        "arch": arch,
        "compiler": "mpy-cross",
    }
