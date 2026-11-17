"""gothicOS application SDK."""

from .abi import ABI_VERSION, SUPPORTED_ABI, abi_info
from .app import AppError, build_app, load_manifest, new_app
from .client import KernelClient, KernelError, MemoryBlockHandle, PortHandle, TaskHandle

__version__ = "1.1.1"

__all__ = [
    "ABI_VERSION",
    "SUPPORTED_ABI",
    "AppError",
    "KernelClient",
    "KernelError",
    "MemoryBlockHandle",
    "PortHandle",
    "TaskHandle",
    "abi_info",
    "build_app",
    "load_manifest",
    "new_app",
]
