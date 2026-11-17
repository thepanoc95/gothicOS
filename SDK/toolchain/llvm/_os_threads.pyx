# cython: language_level=3
"""Thread and task model helpers for gothicOS host tools."""

MAX_TASKS = 8
MAX_CORES = 2
STATE_NAMES = {1: "READY", 2: "RUNNING", 3: "BLOCKED", 4: "STOPPED"}


cpdef str state_name(int state):
    return STATE_NAMES.get(state, "UNKNOWN")


cpdef int core_for_slot(int slot, int core_count):
    if slot < 0 or core_count < 1:
        return -1
    return slot % core_count


def describe_thread(int task_id, int state, int core_count):
    return {
        "task_id": task_id,
        "state": state_name(state),
        "core": core_for_slot(task_id, core_count),
    }
