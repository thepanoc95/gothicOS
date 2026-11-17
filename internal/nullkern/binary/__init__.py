"""NULLKernRT Binary Execution Module for Umbra Bytecode applications."""

try:
    from .format import ABI_MAJOR, ABI_MINOR, MAGIC, VERSION_MAJOR, VERSION_MINOR
    from .package import ApplicationPackage, read_package, verify_package, write_package
    from .verifier import VerificationResult, verify
    from .vm import (
        BLOCKED,
        DEFAULT_STEP_BUDGET,
        FAILED,
        READY,
        RUNNING,
        STOPPED,
        BinaryExecutionModule,
        BinaryTask,
        CapabilityError,
        VerificationError,
        VMError,
    )
except ImportError:
    from format import ABI_MAJOR, ABI_MINOR, MAGIC, VERSION_MAJOR, VERSION_MINOR
    from package import ApplicationPackage, read_package, verify_package, write_package
    from verifier import VerificationResult, verify
    from vm import (
        BLOCKED,
        DEFAULT_STEP_BUDGET,
        FAILED,
        READY,
        RUNNING,
        STOPPED,
        BinaryExecutionModule,
        BinaryTask,
        CapabilityError,
        VerificationError,
        VMError,
    )


def run_package(package, max_steps=None, log=None):
    """Run a package on a fresh kernel; useful for tests and host-side checks.

    This helper imports :class:`Kernel` lazily so importing the binary module from
    ``kernel.py`` cannot create a circular import.
    """
    try:
        from ..kernel import Kernel
    except ImportError:
        from kernel import Kernel

    kernel = Kernel()
    module = BinaryExecutionModule(kernel, log=log)
    task = module.spawn(package, "umbra-app")
    steps = 0
    while task.state not in (STOPPED, FAILED):
        module.step(task)
        steps += 1
        if max_steps is not None and steps >= max_steps:
            break
    return kernel, module, task


__all__ = [
    "ABI_MAJOR",
    "ABI_MINOR",
    "MAGIC",
    "VERSION_MAJOR",
    "VERSION_MINOR",
    "READY",
    "RUNNING",
    "BLOCKED",
    "STOPPED",
    "FAILED",
    "DEFAULT_STEP_BUDGET",
    "ApplicationPackage",
    "BinaryExecutionModule",
    "BinaryTask",
    "CapabilityError",
    "VerificationError",
    "VerificationResult",
    "VMError",
    "read_package",
    "run_package",
    "verify",
    "verify_package",
    "write_package",
]
