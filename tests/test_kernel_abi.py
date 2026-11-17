import unittest

from internal.nullkern.abi import (
    ABI_VERSION,
    CAP_DISTRIBUTED,
    CAP_IPC,
    CAP_MULTICORE,
    CAP_SUBSYSTEMS,
    CAP_TASKS,
    OP_GET_INFO,
    OP_IPC_RECEIVE,
    OP_IPC_SEND,
    OP_TASK_CREATE,
    OP_TASK_INFO,
    OP_TASK_RESUME,
    OP_TASK_STOP,
    OP_TASK_STATE,
    OP_YIELD,
    STATUS_INVALID_ARGUMENT,
    STATUS_NOT_FOUND,
    STATUS_OK,
    STATUS_QUEUE_FULL,
    STATUS_UNSUPPORTED,
    STATUS_WOULD_BLOCK,
)
from internal.nullkern.kernel import MAILBOX_DEPTH, TASK_BLOCKED, TASK_STOPPED, Kernel


class KernelABITests(unittest.TestCase):
    def test_get_info_reports_stable_version_and_capabilities(self):
        kernel = Kernel(core_count=2)

        status, info = kernel.abi.invoke(OP_GET_INFO)

        self.assertEqual(status, STATUS_OK)
        self.assertEqual(info["abi_version"], ABI_VERSION)
        self.assertEqual(
            info["capabilities"],
            CAP_TASKS | CAP_IPC | CAP_MULTICORE | CAP_SUBSYSTEMS | CAP_DISTRIBUTED,
        )
        self.assertEqual(info["core_count"], 2)
        self.assertEqual(info["mailbox_depth"], MAILBOX_DEPTH)

    def test_create_yield_and_inspect_task(self):
        kernel = Kernel()
        status, task_id = kernel.abi.invoke(OP_TASK_CREATE, "worker", lambda _k, _id: False)

        self.assertEqual(status, STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_TASK_INFO, task_id)[1]["name"], "worker")
        self.assertEqual(kernel.abi.invoke(OP_TASK_STATE, task_id), (STATUS_OK, 1))
        self.assertEqual(kernel.abi.invoke(OP_YIELD), (STATUS_OK, task_id))
        self.assertEqual(kernel.abi.invoke(OP_TASK_STATE, task_id)[1], TASK_STOPPED)

    def test_ipc_reports_empty_and_full_mailboxes(self):
        kernel = Kernel()
        status, receiver = kernel.abi.invoke(OP_TASK_CREATE, "receiver", lambda _k, _id: None)

        self.assertEqual(status, STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_IPC_RECEIVE, receiver)[0], STATUS_WOULD_BLOCK)
        for value in range(MAILBOX_DEPTH):
            self.assertEqual(kernel.abi.invoke(OP_IPC_SEND, receiver, value)[0], STATUS_OK)
        self.assertEqual(kernel.abi.invoke(OP_IPC_SEND, receiver, "overflow")[0], STATUS_QUEUE_FULL)
        self.assertEqual(kernel.abi.invoke(OP_IPC_RECEIVE, receiver), (STATUS_OK, (-1, 0)))

    def test_resume_blocked_task(self):
        kernel = Kernel()
        task_id = kernel.spawn("reader", lambda current_kernel, current_id: current_kernel.receive(current_id))
        kernel.run_once()

        self.assertEqual(kernel.task_state(task_id), TASK_BLOCKED)
        self.assertEqual(kernel.abi.invoke(OP_TASK_RESUME, task_id), (STATUS_OK, None))
        self.assertEqual(kernel.task_state(task_id), 1)

    def test_abi_returns_stable_errors_for_bad_calls(self):
        kernel = Kernel()

        self.assertEqual(kernel.abi.invoke(999), (STATUS_UNSUPPORTED, None))
        self.assertEqual(kernel.abi.invoke(OP_TASK_CREATE, "missing callback"), (STATUS_INVALID_ARGUMENT, None))
        self.assertEqual(kernel.abi.invoke(OP_TASK_STOP, 7), (STATUS_NOT_FOUND, None))
        self.assertEqual(kernel.abi.invoke(OP_TASK_STATE, 99)[0], STATUS_INVALID_ARGUMENT)
        self.assertEqual(kernel.abi.invoke(OP_TASK_INFO, 0)[0], STATUS_NOT_FOUND)

    def test_task_callback_exceptions_map_to_internal_error(self):
        kernel = Kernel()

        def fail(_kernel, _task_id):
            raise TypeError("task error")

        kernel.abi.invoke(OP_TASK_CREATE, "broken", fail)

        self.assertEqual(kernel.abi.invoke(OP_YIELD)[0], 6)


if __name__ == "__main__":
    unittest.main()