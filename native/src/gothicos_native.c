/* Implementation of the stable C client contract declared by the SDK headers.
 *
 * No Python headers, no MicroPython runtime, no allocator assumptions beyond
 * the C library. Everything crosses the ABI as gothicos_arguments_t /
 * gothicos_result_t through a caller-supplied gothicos_invoke_fn.
 */
#include "gothicos/native_component.h"

#include <string.h>

static gothicos_invoke_fn active_transport = 0;

static const char *const STATUS_NAMES[] = {
    "OK",
    "INVALID_ARGUMENT",
    "NOT_FOUND",
    "WOULD_BLOCK",
    "QUEUE_FULL",
    "UNSUPPORTED",
    "INTERNAL_ERROR",
    "PERMISSION_DENIED",
    "ALREADY_EXISTS",
    "OUT_OF_MEMORY",
};

static const char *const OPCODE_NAMES[] = {
    "GET_INFO",
    "TASK_CREATE",
    "TASK_STOP",
    "TASK_RESUME",
    "TASK_STATE",
    "IPC_SEND",
    "IPC_RECEIVE",
    "YIELD",
    "RUN",
    "TASK_INFO",
    "PORT_OPEN",
    "PORT_CLOSE",
    "PORT_SEND",
    "PORT_RECEIVE",
    "PORT_INFO",
    "MEM_POOL_CREATE",
    "MEM_ALLOC",
    "MEM_FREE",
    "MEM_WRITE",
    "MEM_READ",
    "IRQ_REGISTER",
    "IRQ_UNREGISTER",
    "IRQ_RAISE",
    "IRQ_SERVICE",
    "DRIVER_INFO",
    "DRIVER_IO",
    "CAP_GRANT",
    "CAP_REVOKE",
    "CAP_INFO",
    "BOOT_INFO",
    "ABILITY_REGISTER",
    "ABILITY_START",
    "ABILITY_STOP",
    "ABILITY_STATE",
    "ABILITY_INFO",
    "SOFTBUS_SEND",
    "SOFTBUS_SUBSCRIBE",
    "SOFTBUS_INFO",
    "REMOTE_CALL",
    "REMOTE_ROUTE",
    "NODE_REGISTER",
    "NODE_INFO",
    "SCHEDULE",
    "DISTRIBUTED_INFO",
};

static gothicos_value_t make_int(int value)
{
    gothicos_value_t result;
    result.kind = GOTHICOS_VALUE_INT;
    result.integer = value;
    result.string = 0;
    return result;
}

static gothicos_value_t make_string(const char *value)
{
    gothicos_value_t result;
    result.kind = GOTHICOS_VALUE_STRING;
    result.integer = 0;
    result.string = value;
    return result;
}

const char *gothicos_status_name(int status)
{
    if (status < 0 || (unsigned int)status >= sizeof(STATUS_NAMES) / sizeof(STATUS_NAMES[0])) {
        return "UNKNOWN";
    }
    return STATUS_NAMES[status];
}

const char *gothicos_opcode_name(int opcode)
{
    if (opcode < 0 || (unsigned int)opcode >= sizeof(OPCODE_NAMES) / sizeof(OPCODE_NAMES[0])) {
        return "UNKNOWN";
    }
    return OPCODE_NAMES[opcode];
}

void gothicos_native_set_transport(gothicos_invoke_fn transport)
{
    active_transport = transport;
}

gothicos_invoke_fn gothicos_native_transport(void)
{
    return active_transport;
}

int gothicos_native_call(int opcode, const gothicos_arguments_t *arguments, gothicos_result_t *result)
{
    gothicos_result_t local;
    memset(&local, 0, sizeof(local));

    if (result == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (active_transport == 0) {
        local.status = GOTHICOS_STATUS_UNSUPPORTED;
        *result = local;
        return local.status;
    }

    local.status = active_transport(opcode, arguments, &local);
    if (local.status < GOTHICOS_STATUS_OK || local.status > GOTHICOS_STATUS_OUT_OF_MEMORY) {
        local.status = GOTHICOS_STATUS_INTERNAL_ERROR;
    }
    *result = local;
    return local.status;
}

gothicos_response_t gothicos_call(gothicos_invoke_fn transport, int opcode, const void *arguments)
{
    gothicos_response_t response;
    gothicos_result_t result;

    response.status = GOTHICOS_STATUS_INVALID_ARGUMENT;
    response.value = 0;
    if (transport == 0) {
        return response;
    }

    memset(&result, 0, sizeof(result));
    response.status = transport(opcode, arguments, &result);
    if (response.status < GOTHICOS_STATUS_OK || response.status > GOTHICOS_STATUS_OUT_OF_MEMORY) {
        response.status = GOTHICOS_STATUS_INTERNAL_ERROR;
    }
    if (result.value.kind == GOTHICOS_VALUE_INT) {
        response.value = result.value.integer;
    }
    return response;
}

int gothicos_sdk_abi_supported(int major, int minor)
{
    return major == GOTHICOS_ABI_MAJOR && minor <= GOTHICOS_ABI_MINOR;
}

int gothicos_sdk_init(gothicos_invoke_fn transport)
{
    gothicos_result_t result;
    unsigned int version;

    if (transport == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    gothicos_native_set_transport(transport);
    if (gothicos_native_call(GOTHICOS_OP_GET_INFO, 0, &result) != GOTHICOS_STATUS_OK) {
        return result.status;
    }
    if (result.value.kind != GOTHICOS_VALUE_INT) {
        return GOTHICOS_STATUS_UNSUPPORTED;
    }

    version = (unsigned int)result.value.integer;
    return gothicos_sdk_abi_supported((int)((version >> 16) & 0xff), (int)((version >> 8) & 0xff))
               ? GOTHICOS_STATUS_OK
               : GOTHICOS_STATUS_UNSUPPORTED;
}

int gothicos_native_port_open(const char *name, int capacity)
{
    gothicos_value_t values[2];
    gothicos_arguments_t arguments;
    gothicos_result_t result;

    if (name == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    values[0] = make_string(name);
    values[1] = make_int(capacity);
    arguments.items = values;
    arguments.count = 2;
    return gothicos_native_call(GOTHICOS_OP_PORT_OPEN, &arguments, &result);
}

int gothicos_native_port_close(const char *name)
{
    gothicos_value_t values[1];
    gothicos_arguments_t arguments;
    gothicos_result_t result;

    if (name == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    values[0] = make_string(name);
    arguments.items = values;
    arguments.count = 1;
    return gothicos_native_call(GOTHICOS_OP_PORT_CLOSE, &arguments, &result);
}

int gothicos_native_port_send(const char *name, const char *message)
{
    gothicos_value_t values[2];
    gothicos_arguments_t arguments;
    gothicos_result_t result;

    if (name == 0 || message == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    values[0] = make_string(name);
    values[1] = make_string(message);
    arguments.items = values;
    arguments.count = 2;
    return gothicos_native_call(GOTHICOS_OP_PORT_SEND, &arguments, &result);
}

int gothicos_native_port_receive(const char *name, int task_id, char *buffer, unsigned int length)
{
    gothicos_value_t values[2];
    gothicos_arguments_t arguments;
    gothicos_result_t result;
    int status;
    unsigned int available;

    if (name == 0 || buffer == 0 || length == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    values[0] = make_string(name);
    values[1] = make_int(task_id);
    arguments.items = values;
    arguments.count = 2;
    status = gothicos_native_call(GOTHICOS_OP_PORT_RECEIVE, &arguments, &result);
    if (status != GOTHICOS_STATUS_OK) {
        return status;
    }
    if (result.value.kind != GOTHICOS_VALUE_STRING || result.value.string == 0) {
        return GOTHICOS_STATUS_INTERNAL_ERROR;
    }

    available = (unsigned int)strlen(result.value.string);
    if (available >= length) {
        available = length - 1;
    }
    memcpy(buffer, result.value.string, available);
    buffer[available] = '\0';
    return GOTHICOS_STATUS_OK;
}

int gothicos_native_component_start(const gothicos_native_component_t *component)
{
    if (component == 0 || component->name == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (component->init != 0 && component->init(component->context) != 0) {
        return GOTHICOS_STATUS_INTERNAL_ERROR;
    }
    return GOTHICOS_STATUS_OK;
}

int gothicos_native_component_step(const gothicos_native_component_t *component)
{
    if (component == 0) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (component->step == 0) {
        return GOTHICOS_STATUS_OK;
    }
    return component->step(component->context);
}

void gothicos_native_component_stop(const gothicos_native_component_t *component)
{
    if (component != 0 && component->shutdown != 0) {
        component->shutdown(component->context);
    }
}

int gothicos_native_component_run(const gothicos_native_component_t *component, unsigned int steps)
{
    unsigned int index;
    int status = gothicos_native_component_start(component);

    if (status != GOTHICOS_STATUS_OK) {
        return status;
    }
    for (index = 0; index < steps; ++index) {
        status = gothicos_native_component_step(component);
        if (status != GOTHICOS_STATUS_OK) {
            gothicos_native_component_stop(component);
            return status;
        }
    }
    gothicos_native_component_stop(component);
    return GOTHICOS_STATUS_OK;
}
