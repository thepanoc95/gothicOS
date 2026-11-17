import unittest
import _thread
import time

from internal.nullkern.kernel import (
    MAILBOX_DEPTH,
    TASK_BLOCKED,
    TASK_READY,
    TASK_STOPPED,
    Kernel,
)


class KernelTests(unittest.TestCase):
    def test_run_once_dispatches_ready_tasks_round_robin(self):
        kernel = Kernel()
        dispatched = []

        first = kernel.spawn("first", lambda _kernel, task_id: (dispatched.append(task_id), False)[1])
        second = kernel.spawn("second", lambda _kernel, task_id: (dispatched.append(task_id), False)[1])

        self.assertEqual(kernel.run(max_steps=8), 2)
        self.assertEqual(dispatched, [first, second])
        self.assertEqual(kernel.task_state(first), TASK_STOPPED)
        self.assertEqual(kernel.task_state(second), TASK_STOPPED)

    def test_send_wakes_blocked_receiver(self):
        kernel = Kernel()
        received = []

        def receive_step(current_kernel, task_id):
            message = current_kernel.receive(task_id)
            if message is None:
                return
            received.append(message)
            return False

        receiver = kernel.spawn("receiver", receive_step)

        def send_step(current_kernel, task_id):
            self.assertTrue(current_kernel.send(receiver, "hello"))
            return False

        sender = kernel.spawn("sender", send_step)

        self.assertEqual(kernel.run(max_steps=4), 3)
        self.assertEqual(received, [(sender, "hello")])
        self.assertEqual(kernel.task_state(receiver), TASK_STOPPED)

    def test_mailbox_is_bounded(self):
        kernel = Kernel()
        receiver = kernel.spawn("receiver", lambda _kernel, _task_id: None)

        for value in range(MAILBOX_DEPTH):
            self.assertTrue(kernel.send(receiver, value, sender_id=7))
        self.assertFalse(kernel.send(receiver, "overflow", sender_id=7))

    def test_empty_receive_blocks_current_task(self):
        kernel = Kernel()

        def receive_step(current_kernel, task_id):
            self.assertIsNone(current_kernel.receive(task_id))

        task_id = kernel.spawn("reader", receive_step)

        self.assertEqual(kernel.run_once(), task_id)
        self.assertEqual(kernel.task_state(task_id), TASK_BLOCKED)
        self.assertIsNone(kernel.run_once())
        self.assertEqual(kernel._tasks[task_id].state, TASK_BLOCKED)

    def test_task_exception_leaves_kernel_consistent(self):
        kernel = Kernel()
        attempts = []

        def fail(_kernel, _task_id):
            if not attempts:
                attempts.append(True)
                raise RuntimeError("task failed")
            return False

        task_id = kernel.spawn("failure", fail)

        with self.assertRaisesRegex(RuntimeError, "task failed"):
            kernel.run_once()

        self.assertEqual(kernel.current_task, None)
        self.assertEqual(kernel.task_state(task_id), TASK_READY)
        self.assertEqual(kernel.run_once(), task_id)

    def test_multicore_scheduler_runs_callbacks_on_both_threads(self):
        kernel = Kernel(core_count=2)
        dispatch_counts = [0, 0]
        thread_ids = set()

        def make_step(index):
            def step(_kernel, _task_id):
                thread_ids.add(_thread.get_ident())
                dispatch_counts[index] += 1
                time.sleep(0.001)
                return dispatch_counts[index] < 5

            return step

        first = kernel.spawn("first", make_step(0))
        second = kernel.spawn("second", make_step(1))

        kernel.run()

        self.assertEqual(dispatch_counts, [5, 5])
        self.assertEqual(kernel.task_state(first), TASK_STOPPED)
        self.assertEqual(kernel.task_state(second), TASK_STOPPED)
        self.assertEqual(len(thread_ids), 2)


if __name__ == "__main__":
    unittest.main()