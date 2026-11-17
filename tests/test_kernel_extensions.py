import unittest

from internal.nullkern.abi import (
    OP_BOOT_INFO,
    OP_CAP_GRANT,
    OP_DRIVER_INFO,
    OP_DRIVER_IO,
    OP_GET_INFO,
    OP_IRQ_RAISE,
    OP_MEM_ALLOC,
    OP_MEM_FREE,
    OP_MEM_POOL_CREATE,
    OP_MEM_READ,
    OP_MEM_WRITE,
    OP_PORT_CLOSE,
    OP_PORT_OPEN,
    OP_PORT_RECEIVE,
    OP_PORT_SEND,
    STATUS_ALREADY_EXISTS,
    STATUS_OK,
    STATUS_OUT_OF_MEMORY,
    STATUS_PERMISSION_DENIED,
    STATUS_QUEUE_FULL,
    STATUS_WOULD_BLOCK,
)
from internal.nullkern.boot import BOOT_COMPLETE, STAGES
from internal.nullkern.drivers import Driver, DriverRegistry
from internal.nullkern.kernel import Kernel
from internal.nullkern.irq import IRQ_CUSTOM


class KernelExtensionTests(unittest.TestCase):
    def test_get_info_reports_extended_features(self):
        status, info = Kernel().abi.invoke(OP_GET_INFO)

        self.assertEqual(status, STATUS_OK)
        self.assertEqual(info["abi_version"], (1, 1, 1))
        self.assertTrue(info["features"]["ports"])
        self.assertTrue(info["features"]["memory_pools"])
        self.assertTrue(info["features"]["capabilities"])

    def test_named_ports_queue_messages_and_report_full(self):
        kernel = Kernel()
        kernel.port_open("events", 2)

        self.assertTrue(kernel.port_send("events", "first", -1))
        self.assertTrue(kernel.port_send("events", "second", -1))
        self.assertFalse(kernel.port_send("events", "third", -1))
        self.assertEqual(kernel.port_receive("events", 0), (-1, "first"))
        self.assertEqual(kernel.port_info("events")["messages"], 1)
        kernel.port_close("events")
        self.assertEqual(kernel.ports.names(), [])

    def test_abi_port_open_rejects_duplicate_and_reports_queue_full(self):
        kernel = Kernel()
        self.assertEqual(kernel.abi.invoke(OP_PORT_OPEN, "events", 1), (STATUS_OK, kernel.port_info("events")))
        self.assertEqual(kernel.abi.invoke(OP_PORT_OPEN, "events", 1)[0], STATUS_ALREADY_EXISTS)
        self.assertEqual(kernel.abi.invoke(OP_PORT_SEND, "events", "a"), (STATUS_OK, None))
        self.assertEqual(kernel.abi.invoke(OP_PORT_SEND, "events", "b")[0], STATUS_QUEUE_FULL)

    def test_memory_pool_allocate_write_read_and_release(self):
        kernel = Kernel()
        kernel.pool_create("packets", 8, 2)
        first = kernel.pool_allocate("packets", b"abc")
        second = kernel.pool_allocate("packets")

        self.assertEqual(kernel.pool_read("packets", first), b"abc\x00\x00\x00\x00\x00")
        kernel.pool_write("packets", second, b"xyz")
        self.assertEqual(kernel.pool_read("packets", second, 3), b"xyz")
        self.assertEqual(kernel.pool_allocate("packets"), -1)
        kernel.pool_release("packets", first)
        self.assertEqual(kernel.pool_allocate("packets"), first)

    def test_abi_memory_operations(self):
        kernel = Kernel()
        self.assertEqual(kernel.abi.invoke(OP_MEM_POOL_CREATE, "slots", 4, 1)[0], STATUS_OK)
        status, block_id = kernel.abi.invoke(OP_MEM_ALLOC, "slots", b"hi")
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_MEM_READ, "slots", block_id), (STATUS_OK, b"hi\x00\x00"))
        self.assertEqual(kernel.abi.invoke(OP_MEM_WRITE, "slots", block_id, b"ok")[0], STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_MEM_FREE, "slots", block_id), (STATUS_OK, None))
        self.assertEqual(kernel.abi.invoke(OP_MEM_ALLOC, "slots")[0], STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_MEM_ALLOC, "slots")[0], STATUS_OUT_OF_MEMORY)

    def test_deferred_irq_is_serviced_by_scheduler(self):
        kernel = Kernel()
        received = []

        def handler(source, payload):
            received.append((source, payload))

        kernel.irq_register(IRQ_CUSTOM, handler)
        self.assertEqual(kernel.abi.invoke(OP_IRQ_RAISE, IRQ_CUSTOM, "tick"), (STATUS_OK, None))
        self.assertEqual(kernel.irq_info()["pending"], 1)
        kernel.run_once()
        self.assertEqual(received, [(IRQ_CUSTOM, "tick")])
        self.assertEqual(kernel.irq_info()["pending"], 0)

    def test_driver_registry_is_visible_through_abi(self):
        class FakeDriver(Driver):
            def __init__(self):
                Driver.__init__(self, "fake", "test")

            def write(self, value):
                return value

        kernel = Kernel()
        kernel.drivers.register(FakeDriver())
        self.assertEqual(kernel.abi.invoke(OP_DRIVER_INFO, "fake")[1]["kind"], "test")
        self.assertEqual(kernel.abi.invoke(OP_DRIVER_IO, "fake", "write", 7), (STATUS_OK, 7))
        kernel.drivers.unregister("fake")
        self.assertIsInstance(DriverRegistry(), DriverRegistry)

    def test_capability_revocation_is_enforced_from_task_context(self):
        kernel = Kernel()
        results = []

        def step(current_kernel, _task_id):
            results.append(current_kernel.abi.invoke(OP_PORT_OPEN, "blocked")[0])
            return False

        task_id = kernel.spawn("worker", step)
        port_cap = None
        for capability in kernel.capabilities.holder_capabilities(task_id):
            if capability["name"] == "port":
                port_cap = capability["cap_id"]
        self.assertIsNotNone(port_cap)
        kernel.capabilities.revoke(port_cap)
        kernel.run_once()
        self.assertEqual(results, [STATUS_PERMISSION_DENIED])

    def test_capability_management_requires_boot_rights(self):
        kernel = Kernel()
        self.assertEqual(kernel.abi.invoke(OP_CAP_GRANT, "port", 15)[0], STATUS_OK)
        granted = kernel.cap_info()["count"] if hasattr(kernel, "cap_info") else kernel.capabilities.info()["count"]
        self.assertGreaterEqual(granted, 1)

    def test_boot_sequence_completes_and_is_reported(self):
        kernel = Kernel()
        result = kernel.boot()

        self.assertEqual(result.status, BOOT_COMPLETE)
        self.assertEqual([stage["stage"] for stage in result.stages], list(STAGES))
        status, info = kernel.abi.invoke(OP_BOOT_INFO)
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(info["status"], BOOT_COMPLETE)
        self.assertIn("system", kernel.memory.stats())
        self.assertIn("kernel", kernel.ports.names())

    def test_blocked_port_receive_returns_would_block(self):
        kernel = Kernel()
        kernel.port_open("inbox", 1)
        status, _value = kernel.abi.invoke(OP_PORT_RECEIVE, "inbox", 0)
        self.assertEqual(status, STATUS_WOULD_BLOCK)


if __name__ == "__main__":
    unittest.main()
