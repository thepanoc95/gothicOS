"""Helpers for reading and describing packaged Umbra applications on the device."""

try:
    from .verifier import verify
except ImportError:
    from verifier import verify


class ApplicationPackage:
    """A verified Umbra package plus its metadata."""

    __slots__ = ("data", "verification", "metadata", "header")

    def __init__(self, data, verification):
        self.data = data
        self.verification = verification
        self.metadata = verification.metadata or {}
        self.header = verification.header

    @property
    def name(self):
        return self.metadata.get("name", "umbra-app")

    @property
    def capabilities(self):
        return list(self.metadata.get("capabilities", []))

    @property
    def resources(self):
        return dict(self.metadata.get("resources", {}))

    def info(self):
        return {
            "name": self.name,
            "language": self.metadata.get("language"),
            "language_version": self.metadata.get("language_version"),
            "abi": self.metadata.get("abi"),
            "entry": self.metadata.get("entry"),
            "capabilities": self.capabilities,
            "resources": self.resources,
            "size": len(self.data),
        }


def verify_package(data, limits=None):
    """Return an :class:`ApplicationPackage` or raise ``ValueError``."""
    verification = verify(data, limits)
    if not verification.ok:
        raise ValueError("; ".join(verification.errors))
    return ApplicationPackage(data, verification)


def read_package(path, limits=None):
    """Read and verify a ``.umb`` file from the device filesystem."""
    with open(path, "rb") as package_file:
        data = package_file.read()
    return verify_package(data, limits)


def write_package(path, data):
    """Write an Umbra package to the device filesystem."""
    with open(path, "wb") as package_file:
        package_file.write(data)
    return len(data)
