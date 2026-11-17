"""Hello world for nullkernRT.

The loader injects the kernel as a global before importing this module. `start()` is
called once during boot; each `step()` must return quickly because the scheduler runs
one step per dispatch.
"""

import nullkern_sdk


def hello_step(kernel, task_id):
    info = kernel.abi.invoke(nullkern_sdk.OP_GET_INFO)[1]
    if info is not None and info["task_count"] > 0:
        return False
    return True


def start(kernel):
    client = nullkern_sdk.KernelClient.from_kernel(kernel)
    info = client.info()
    port = client.open_port("hello", 8)
    task_id = client.spawn("hello", hello_step)
    client.port_send(port, "hello from nullkernRT", -1)
    return {"task_id": task_id, "info": info, "port": port}
