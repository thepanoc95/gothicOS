"""Minimal device-driver framework for the nullkern runtime.

Drivers expose a uniform ``open``/``close``/``read``/``write``/``ioctl`` surface. The
registry gives the kernel a single place to discover hardware without letting tasks
touch ``machine`` peripherals directly.
"""

DRIVER_CLOSED = 0
DRIVER_OPEN = 1
DRIVER_ERROR = 2


class Driver:
    __slots__ = ("name", "kind", "owner", "state")

    def __init__(self, name, kind, owner=-1):
        if not isinstance(name, str) or not name:
            raise ValueError("driver name must be a non-empty string")
        self.name = name
        self.kind = kind
        self.owner = owner
        self.state = DRIVER_CLOSED

    def open(self):
        self.state = DRIVER_OPEN
        return True

    def close(self):
        self.state = DRIVER_CLOSED
        return True

    def read(self, length=None):
        raise RuntimeError("read is not supported by %s" % self.kind)

    def write(self, value):
        raise RuntimeError("write is not supported by %s" % self.kind)

    def ioctl(self, command, argument=None):
        raise RuntimeError("ioctl is not supported by %s" % self.kind)

    def info(self):
        return {
            "name": self.name,
            "kind": self.kind,
            "owner": self.owner,
            "state": self.state,
        }


class GpioDriver(Driver):
    __slots__ = ("_pin", "_pin_name")

    def __init__(self, name, pin_name, pin=None, owner=-1):
        Driver.__init__(self, name, "gpio", owner)
        if pin is None:
            raise RuntimeError("machine.Pin is not available on this platform")
        self._pin = pin
        self._pin_name = pin_name

    def read(self, length=None):
        return self._pin.value()

    def write(self, value):
        self._pin.value(1 if value else 0)
        return True


class DriverRegistry:
    __slots__ = ("_drivers",)

    def __init__(self):
        self._drivers = {}

    def register(self, driver):
        if not isinstance(driver, Driver):
            raise TypeError("driver must derive from Driver")
        if driver.name in self._drivers:
            raise ValueError("driver already exists")
        self._drivers[driver.name] = driver
        return driver

    def unregister(self, name):
        driver = self.get(name)
        driver.close()
        del self._drivers[name]
        return driver.info()

    def get(self, name):
        if name not in self._drivers:
            raise KeyError(name)
        return self._drivers[name]

    def names(self):
        return sorted(self._drivers)

    def info(self):
        result = {}
        for name in self._drivers:
            result[name] = self._drivers[name].info()
        return result
