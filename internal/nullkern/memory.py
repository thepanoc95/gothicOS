"""Deterministic fixed-block memory pools for the nullkern runtime.

Pools are backed by a ``bytearray`` and a free list, so allocation cost is bounded and
independent of the interpreter garbage collector. Blocks are addressed by numeric
offsets rather than raw pointers, which keeps the API usable from MicroPython bytecode.
"""

class MemoryPool:
    __slots__ = (
        "name", "block_size", "block_count", "_storage", "_free",
        "_allocated", "_owners",
    )

    def __init__(self, name, block_size, block_count):
        if not isinstance(name, str) or not name:
            raise ValueError("pool name must be a non-empty string")
        if not isinstance(block_size, int) or block_size < 1:
            raise ValueError("block_size must be a positive integer")
        if not isinstance(block_count, int) or block_count < 1:
            raise ValueError("block_count must be a positive integer")
        if block_size * block_count > 1 << 20:
            raise ValueError("pool is too large")
        self.name = name
        self.block_size = block_size
        self.block_count = block_count
        self._storage = bytearray(block_size * block_count)
        self._free = list(range(block_count))
        self._allocated = [False] * block_count
        self._owners = [-1] * block_count

    def _check_block(self, block_id):
        if not isinstance(block_id, int) or block_id < 0 or block_id >= self.block_count:
            raise ValueError("invalid block id")
        if not self._allocated[block_id]:
            raise ValueError("block is not allocated")

    def allocate(self, owner=-1, data=None):
        if not self._free:
            return -1
        block_id = self._free.pop()
        self._allocated[block_id] = True
        self._owners[block_id] = owner
        if data is not None:
            self.write(block_id, data)
        return block_id

    def release(self, block_id):
        self._check_block(block_id)
        start = block_id * self.block_size
        end = start + self.block_size
        for index in range(start, end):
            self._storage[index] = 0
        self._allocated[block_id] = False
        self._owners[block_id] = -1
        self._free.append(block_id)

    def offset(self, block_id):
        self._check_block(block_id)
        return block_id * self.block_size

    def read(self, block_id, length=None):
        self._check_block(block_id)
        if length is None:
            length = self.block_size
        if not isinstance(length, int) or length < 0 or length > self.block_size:
            raise ValueError("invalid read length")
        start = block_id * self.block_size
        return bytes(self._storage[start:start + length])

    def write(self, block_id, data):
        self._check_block(block_id)
        if not isinstance(data, (bytes, bytearray, str)):
            raise TypeError("data must be bytes-like")
        if isinstance(data, str):
            data = data.encode()
        if len(data) > self.block_size:
            raise ValueError("data exceeds block size")
        start = block_id * self.block_size
        for index in range(len(data)):
            self._storage[start + index] = data[index]

    def owner(self, block_id):
        self._check_block(block_id)
        return self._owners[block_id]

    def stats(self):
        allocated = self.block_count - len(self._free)
        return {
            "name": self.name,
            "block_size": self.block_size,
            "block_count": self.block_count,
            "allocated": allocated,
            "free": len(self._free),
        }


class MemoryManager:
    __slots__ = ("_pools",)

    def __init__(self):
        self._pools = {}

    def create_pool(self, name, block_size, block_count):
        if name in self._pools:
            raise ValueError("pool already exists")
        pool = MemoryPool(name, block_size, block_count)
        self._pools[name] = pool
        return pool

    def close_pool(self, name):
        pool = self.pool(name)
        del self._pools[name]
        return pool.stats()

    def pool(self, name):
        if name not in self._pools:
            raise KeyError(name)
        return self._pools[name]

    def allocate(self, name, owner=-1, data=None):
        return self.pool(name).allocate(owner, data)

    def release(self, name, block_id):
        self.pool(name).release(block_id)

    def read(self, name, block_id, length=None):
        return self.pool(name).read(block_id, length)

    def write(self, name, block_id, data):
        self.pool(name).write(block_id, data)

    def stats(self):
        result = {}
        for name in self._pools:
            result[name] = self._pools[name].stats()
        return result
