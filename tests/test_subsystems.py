import unittest

from internal.nullkern.abi import (
    OP_ABILITY_INFO,
    OP_ABILITY_REGISTER,
    OP_ABILITY_START,
    OP_DISTRIBUTED_INFO,
    OP_REMOTE_CALL,
    OP_SCHEDULE,
    OP_SOFTBUS_SEND,
    STATUS_ALREADY_EXISTS,
    STATUS_NOT_FOUND,
    STATUS_OK,
)
from internal.nullkern.boot import STAGE_DISTRIBUTED, STAGE_SUBSYSTEMS
from internal.nullkern.kernel import Kernel
from internal.nullkern.subsystems import (
    ABILITY_RUNNING,
    ABILITY_STOPPED,
    MESSAGE_EVENT,
    MESSAGE_REQUEST,
)


class AbilityManagerTests(unittest.TestCase):
    def test_dependencies_start_before_parent_and_stop_after_dependents(self):
        kernel = Kernel()
        events = []

        kernel.abilities.register(
            "storage", "foundation",
            start=lambda _kernel: events.append("storage:start"),
            stop=lambda _kernel: events.append("storage:stop"),
        )
        kernel.abilities.register(
            "api", "application", ["storage"],
            start=lambda _kernel: events.append("api:start"),
            stop=lambda _kernel: events.append("api:stop"),
            invoke=lambda arguments: "api:" + str(arguments[0]),
        )

        self.assertEqual(kernel.ability_state("storage"), ABILITY_STOPPED)
        kernel.ability_start("api")

        self.assertEqual(events, ["storage:start", "api:start"])
        self.assertEqual(kernel.ability_state("api"), ABILITY_RUNNING)
        self.assertEqual(kernel.abilities.invoke("api", [7]), "api:7")
        self.assertEqual(kernel.abilities.started(), ["storage", "api"])

        kernel.ability_stop("storage")
        self.assertEqual(events, ["storage:start", "api:start", "api:stop", "storage:stop"])
        self.assertEqual(kernel.abilities.started(), [])

    def test_cyclic_dependencies_are_rejected(self):
        kernel = Kernel()
        kernel.abilities.register("alpha", "test", ["beta"])
        kernel.abilities.register("beta", "test", ["alpha"])

        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            kernel.abilities.start("alpha")

    def test_duplicate_ability_registration_is_rejected(self):
        kernel = Kernel()
        kernel.abilities.register("camera", "hardware")
        with self.assertRaisesRegex(ValueError, "already exists"):
            kernel.abilities.register("camera", "hardware")


class SoftBusTests(unittest.TestCase):
    def test_loopback_delivers_events_between_nodes(self):
        kernel = Kernel()
        node = kernel.add_node("phone-b", "phone-b")["bus"]
        received = []
        node.subscribe(MESSAGE_EVENT, lambda envelope: received.append(envelope["payload"]))

        kernel.softbus_send("phone-b", MESSAGE_EVENT, {"temperature": 31})

        self.assertEqual(received, [{"temperature": 31}])
        self.assertEqual(kernel.softbus_info()["transport"]["nodes"], ["local", "phone-b"])

    def test_missing_node_is_reported(self):
        kernel = Kernel()
        status, _value = kernel.abi.invoke(OP_SOFTBUS_SEND, "missing", MESSAGE_EVENT, "ping")
        self.assertEqual(status, STATUS_NOT_FOUND)


class DistributedTests(unittest.TestCase):
    def test_remote_ability_call_uses_scheduler_route(self):
        kernel = Kernel()
        kernel.distributed.add_node("phone-b", "phone-b", 80, 10)
        kernel.distributed.register_local_ability(
            "camera.capture", lambda arguments: ["captured", arguments[0]]
        )
        kernel.distributed.register_remote_ability("camera.capture", "phone-b")

        result = kernel.distributed.call("camera.capture", [1024])

        self.assertEqual(result, ["captured", 1024])
        self.assertEqual(kernel.ability_state("camera.capture"), ABILITY_RUNNING)
        self.assertEqual(kernel.scheduler.placements, {"camera.capture@phone-b": 1})
        self.assertEqual(kernel.distributed.remote.info()["calls"], 1)

    def test_scheduler_prefers_lower_load_and_advertised_ability(self):
        kernel = Kernel()
        kernel.scheduler.update_load("local", 70)
        kernel.distributed.add_node("busy", "busy", 100, 90)
        kernel.distributed.add_node("camera-node", "camera-node", 100, 20)
        kernel.scheduler.advertise("camera-node", "camera.capture")

        choice = kernel.scheduler.place("camera.capture")
        ranking = kernel.scheduler.rank("camera.capture")

        self.assertEqual(choice, "camera-node")
        self.assertEqual(ranking[0]["node_id"], "camera-node")
        self.assertGreater(ranking[0]["score"], ranking[1]["score"])

    def test_boot_installs_core_abilities(self):
        kernel = Kernel()
        result = kernel.boot()

        self.assertTrue(result.ok())
        stages = [stage["stage"] for stage in result.stages]
        self.assertIn(STAGE_SUBSYSTEMS, stages)
        self.assertIn(STAGE_DISTRIBUTED, stages)
        for name in ("system.ability", "system.softbus", "system.scheduler", "system.remote.ability"):
            self.assertEqual(kernel.ability_state(name), ABILITY_RUNNING)


class SubsystemABITests(unittest.TestCase):
    def test_ability_lifecycle_through_abi(self):
        kernel = Kernel()
        self.assertEqual(
            kernel.abi.invoke(OP_ABILITY_REGISTER, "timer.tick", "foundation", [])[0],
            STATUS_OK,
        )
        self.assertEqual(
            kernel.abi.invoke(OP_ABILITY_REGISTER, "timer.tick", "foundation", [])[0],
            STATUS_ALREADY_EXISTS,
        )
        self.assertEqual(kernel.abi.invoke(OP_ABILITY_START, "timer.tick")[0], STATUS_OK)
        status, info = kernel.abi.invoke(OP_ABILITY_INFO, "timer.tick")
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(info["state"], ABILITY_RUNNING)

    def test_remote_call_and_schedule_through_abi(self):
        kernel = Kernel()
        kernel.add_node("phone-b", "phone-b")
        self.assertEqual(
            kernel.abi.invoke(OP_ABILITY_REGISTER, "echo", "test", [], None, None, lambda args: args)[0],
            STATUS_OK,
        )
        self.assertEqual(
            kernel.abi.invoke(OP_REMOTE_CALL, "echo", ["hello"], "phone-b"),
            (STATUS_OK, ["hello"]),
        )
        status, schedule = kernel.abi.invoke(OP_SCHEDULE, "echo")
        self.assertEqual(status, STATUS_OK)
        self.assertIn("phone-b", [item["node_id"] for item in schedule["ranking"]])

    def test_distributed_info_is_available(self):
        kernel = Kernel()
        status, info = kernel.abi.invoke(OP_DISTRIBUTED_INFO)
        self.assertEqual(status, STATUS_OK)
        self.assertEqual(info["node_id"], "local")
        self.assertIn("softbus", info)


if __name__ == "__main__":
    unittest.main()
