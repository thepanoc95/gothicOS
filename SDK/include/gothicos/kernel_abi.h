/* nullkernRT ABI v1.1.1 -- generated contract for native SDK clients. */
#ifndef GOTHICOS_KERNEL_ABI_H
#define GOTHICOS_KERNEL_ABI_H

#define GOTHICOS_ABI_MAJOR 1
#define GOTHICOS_ABI_MINOR 1
#define GOTHICOS_ABI_PATCH 1
#define GOTHICOS_ABI_VERSION ((GOTHICOS_ABI_MAJOR << 16) | \
                              (GOTHICOS_ABI_MINOR << 8) | GOTHICOS_ABI_PATCH)

/* Status codes returned in the high half of every ABI response. */
#define GOTHICOS_STATUS_OK 0
#define GOTHICOS_STATUS_INVALID_ARGUMENT 1
#define GOTHICOS_STATUS_NOT_FOUND 2
#define GOTHICOS_STATUS_WOULD_BLOCK 3
#define GOTHICOS_STATUS_QUEUE_FULL 4
#define GOTHICOS_STATUS_UNSUPPORTED 5
#define GOTHICOS_STATUS_INTERNAL_ERROR 6
#define GOTHICOS_STATUS_PERMISSION_DENIED 7
#define GOTHICOS_STATUS_ALREADY_EXISTS 8
#define GOTHICOS_STATUS_OUT_OF_MEMORY 9

/* Operation opcodes. Values are stable for ABI v1. */
#define GOTHICOS_OP_GET_INFO 0
#define GOTHICOS_OP_TASK_CREATE 1
#define GOTHICOS_OP_TASK_STOP 2
#define GOTHICOS_OP_TASK_RESUME 3
#define GOTHICOS_OP_TASK_STATE 4
#define GOTHICOS_OP_IPC_SEND 5
#define GOTHICOS_OP_IPC_RECEIVE 6
#define GOTHICOS_OP_YIELD 7
#define GOTHICOS_OP_RUN 8
#define GOTHICOS_OP_TASK_INFO 9
#define GOTHICOS_OP_PORT_OPEN 10
#define GOTHICOS_OP_PORT_CLOSE 11
#define GOTHICOS_OP_PORT_SEND 12
#define GOTHICOS_OP_PORT_RECEIVE 13
#define GOTHICOS_OP_PORT_INFO 14
#define GOTHICOS_OP_MEM_POOL_CREATE 15
#define GOTHICOS_OP_MEM_ALLOC 16
#define GOTHICOS_OP_MEM_FREE 17
#define GOTHICOS_OP_MEM_WRITE 18
#define GOTHICOS_OP_MEM_READ 19
#define GOTHICOS_OP_IRQ_REGISTER 20
#define GOTHICOS_OP_IRQ_UNREGISTER 21
#define GOTHICOS_OP_IRQ_RAISE 22
#define GOTHICOS_OP_IRQ_SERVICE 23
#define GOTHICOS_OP_DRIVER_INFO 24
#define GOTHICOS_OP_DRIVER_IO 25
#define GOTHICOS_OP_CAP_GRANT 26
#define GOTHICOS_OP_CAP_REVOKE 27
#define GOTHICOS_OP_CAP_INFO 28
#define GOTHICOS_OP_BOOT_INFO 29
#define GOTHICOS_OP_ABILITY_REGISTER 30
#define GOTHICOS_OP_ABILITY_START 31
#define GOTHICOS_OP_ABILITY_STOP 32
#define GOTHICOS_OP_ABILITY_STATE 33
#define GOTHICOS_OP_ABILITY_INFO 34
#define GOTHICOS_OP_SOFTBUS_SEND 35
#define GOTHICOS_OP_SOFTBUS_SUBSCRIBE 36
#define GOTHICOS_OP_SOFTBUS_INFO 37
#define GOTHICOS_OP_REMOTE_CALL 38
#define GOTHICOS_OP_REMOTE_ROUTE 39
#define GOTHICOS_OP_NODE_REGISTER 40
#define GOTHICOS_OP_NODE_INFO 41
#define GOTHICOS_OP_SCHEDULE 42
#define GOTHICOS_OP_DISTRIBUTED_INFO 43

/* Feature bits reported by GET_INFO. */
#define GOTHICOS_CAP_TASKS 1
#define GOTHICOS_CAP_IPC 2
#define GOTHICOS_CAP_MULTICORE 4
#define GOTHICOS_CAP_SUBSYSTEMS 8
#define GOTHICOS_CAP_DISTRIBUTED 16

/* Task states. */
#define GOTHICOS_TASK_READY 1
#define GOTHICOS_TASK_RUNNING 2
#define GOTHICOS_TASK_BLOCKED 3
#define GOTHICOS_TASK_STOPPED 4

/* Ability lifecycle states. */
#define GOTHICOS_ABILITY_STOPPED 0
#define GOTHICOS_ABILITY_STARTING 1
#define GOTHICOS_ABILITY_RUNNING 2
#define GOTHICOS_ABILITY_STOPPING 3
#define GOTHICOS_ABILITY_FAILED 4

/* Soft bus message kinds. */
#define GOTHICOS_SOFTBUS_REQUEST 1
#define GOTHICOS_SOFTBUS_RESPONSE 2
#define GOTHICOS_SOFTBUS_EVENT 3

/* Capability rights used by CAP_GRANT. */
#define GOTHICOS_RIGHT_READ 1
#define GOTHICOS_RIGHT_WRITE 2
#define GOTHICOS_RIGHT_EXECUTE 4
#define GOTHICOS_RIGHT_MANAGE 8
#define GOTHICOS_FULL_RIGHTS 15

/* Deferred interrupt sources. */
#define GOTHICOS_IRQ_TIMER 1
#define GOTHICOS_IRQ_GPIO 2
#define GOTHICOS_IRQ_PORT 3
#define GOTHICOS_IRQ_CUSTOM 4

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int abi_version;
    unsigned int capabilities;
    unsigned int max_tasks;
    unsigned int task_count;
    unsigned int mailbox_depth;
    unsigned int core_count;
} gothicos_kernel_info_t;

typedef struct {
    int status;
    int value;
} gothicos_response_t;

typedef int (*gothicos_invoke_fn)(int opcode, const void *arguments, void *result);

const char *gothicos_status_name(int status);
const char *gothicos_opcode_name(int opcode);
gothicos_response_t gothicos_call(gothicos_invoke_fn transport,
                                  int opcode, const void *arguments);

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_KERNEL_ABI_H */
