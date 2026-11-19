/* NullKernRT native core.
 *
 * Freestanding C11 implementation of the deterministic microkernel model. Every object
 * lives in the kernel structure supplied by the host; there is no allocator, no libc and
 * no platform call. The Linux module, native SDK and MicroPython facade all drive this
 * same code through nk_invoke().
 */
#include "gothicos/nk_core.h"

/* --- tiny local string helpers (no libc) -------------------------------- */

static void nk_strcpy(char *destination, const char *source, size_t capacity)
{
    size_t index = 0;
    if (destination == NULL || capacity == 0) {
        return;
    }
    if (source != NULL) {
        while (index + 1 < capacity && source[index] != '\0') {
            destination[index] = source[index];
            index++;
        }
    }
    destination[index] = '\0';
}

static int nk_strcmp(const char *left, const char *right)
{
    size_t index = 0;
    if (left == NULL || right == NULL) {
        return left == right ? 0 : (left == NULL ? -1 : 1);
    }
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return (unsigned char)left[index] - (unsigned char)right[index];
}

/* --- internal object types ---------------------------------------------- */

typedef struct {
    int sender;
    int kind;
    int integer;
    char text[NK_STRING_MAX];
} nk_message_t;

typedef struct {
    char name[NK_NAME_MAX];
    nk_step_fn step;
    void *context;
    int state;
    nk_message_t mailbox[NK_MAILBOX_DEPTH];
    int head;
    int tail;
    int count;
} nk_task_t;

typedef struct {
    char name[NK_NAME_MAX];
    int owner;
    int capacity;
    nk_message_t messages[NK_PORT_DEPTH];
    int head;
    int tail;
    int count;
} nk_port_t;

typedef struct {
    char name[NK_NAME_MAX];
    int block_size;
    int block_count;
    unsigned char used[NK_POOL_BLOCKS_MAX];
    unsigned char blocks[NK_POOL_BLOCK_MAX * NK_POOL_BLOCKS_MAX];
} nk_pool_t;

typedef struct {
    int source;
    nk_irq_fn handler;
    void *context;
    int enabled;
} nk_irq_t;

typedef struct {
    int source;
    int payload;
} nk_pending_irq_t;

typedef struct {
    char name[NK_NAME_MAX];
    nk_driver_io_fn io;
    void *context;
} nk_driver_t;

typedef struct {
    char name[NK_NAME_MAX];
    char subsystem[NK_NAME_MAX];
    int state;
    nk_ability_fn invoke;
    void *context;
} nk_ability_t;

typedef struct {
    char id[NK_NAME_MAX];
    char name[NK_NAME_MAX];
    int capacity;
    int load;
    char features[8][NK_NAME_MAX];
    int feature_count;
} nk_node_t;

typedef struct {
    char ability[NK_NAME_MAX];
    char node[NK_NAME_MAX];
} nk_route_t;

typedef struct {
    int id;
    char source[NK_NAME_MAX];
    char target[NK_NAME_MAX];
    int kind;
    int integer;
    char text[NK_STRING_MAX];
    int correlation_id;
} nk_bus_message_t;

struct nk_kernel {
    uint32_t magic;
    unsigned core_count;
    unsigned first_managed_cpu;
    int cursor;
    int current_task;
    int boot_status;
    int boot_stage_count;
    char boot_stages[NK_MAX_BOOT_STAGES][NK_NAME_MAX];
    char string_buffer[NK_STRING_MAX];
    nk_step_resolver_fn step_resolver;
    nk_task_t tasks[NK_MAX_TASKS];
    nk_port_t ports[NK_MAX_PORTS];
    nk_pool_t pools[NK_MAX_POOLS];
    nk_irq_t irqs[NK_MAX_IRQS];
    nk_pending_irq_t pending[NK_MAX_IRQS];
    int pending_head;
    int pending_tail;
    int pending_count;
    nk_driver_t drivers[NK_MAX_DRIVERS];
    nk_ability_t abilities[NK_MAX_ABILITIES];
    nk_node_t nodes[NK_MAX_NODES];
    nk_route_t routes[NK_MAX_ROUTES];
    nk_bus_message_t bus_messages[NK_MAX_SOFTBUS_MESSAGES];
    int bus_message_count;
    int bus_subscriptions[NK_MAX_SUBSCRIPTIONS];
    uint16_t capabilities[NK_MAX_TASKS + 1];
    int next_message_id;
};

#define NK_MAGIC 0x4e4b5231U /* NK1 */
static nk_kernel_t nk_shared_kernel;

size_t nk_kernel_storage_size(void)
{
    return sizeof(nk_kernel_t);
}

void nk_kernel_set_step_resolver(nk_kernel_t *kernel, nk_step_resolver_fn resolver)
{
    if (kernel != NULL && kernel->magic == NK_MAGIC) {
        kernel->step_resolver = resolver;
    }
}

nk_kernel_t *nk_kernel_init(void *storage, size_t size, unsigned core_count,
                            unsigned first_managed_cpu)
{
    nk_kernel_t *kernel;
    int index;

    if (storage == NULL || size < sizeof(nk_kernel_t) || core_count == 0) {
        return NULL;
    }
    kernel = (nk_kernel_t *)storage;
    for (index = 0; index < (int)sizeof(nk_kernel_t); index++) {
        ((unsigned char *)storage)[index] = 0;
    }
    kernel->magic = NK_MAGIC;
    kernel->core_count = core_count;
    kernel->first_managed_cpu = first_managed_cpu;
    kernel->current_task = -1;
    kernel->boot_status = NK_BOOT_PENDING;
    kernel->next_message_id = 1;
    /* Holder -1 is the kernel and is indexed at zero. */
    kernel->capabilities[0] = GOTHICOS_FULL_RIGHTS;
    return kernel;
}

nk_kernel_t *nk_kernel_create(void)
{
    return nk_kernel_init(&nk_shared_kernel, sizeof(nk_shared_kernel), 1, 0);
}

nk_kernel_t *nk_kernel_shared(void)
{
    return nk_shared_kernel.magic == NK_MAGIC ? &nk_shared_kernel : NULL;
}

void nk_kernel_shutdown(nk_kernel_t *kernel)
{
    if (kernel != NULL && kernel->magic == NK_MAGIC) {
        kernel->magic = 0;
    }
}

/* --- argument and result helpers ---------------------------------------- */

static int arg_count(const gothicos_arguments_t *arguments)
{
    return arguments == NULL ? 0 : (int)arguments->count;
}

static int arg_int(const gothicos_arguments_t *arguments, int index)
{
    if (arguments == NULL || index < 0 || (unsigned)index >= arguments->count ||
        arguments->items == NULL) {
        return 0;
    }
    return arguments->items[index].kind == GOTHICOS_VALUE_INT
               ? arguments->items[index].integer
               : 0;
}

static const char *arg_string(const gothicos_arguments_t *arguments, int index)
{
    if (arguments == NULL || index < 0 || (unsigned)index >= arguments->count ||
        arguments->items == NULL) {
        return NULL;
    }
    return arguments->items[index].kind == GOTHICOS_VALUE_STRING
               ? arguments->items[index].string
               : NULL;
}

static void result_int(gothicos_result_t *result, int status, int value)
{
    if (result == NULL) {
        return;
    }
    result->status = status;
    result->value.kind = GOTHICOS_VALUE_INT;
    result->value.integer = value;
    result->value.string = NULL;
}

static void result_none(gothicos_result_t *result, int status)
{
    if (result == NULL) {
        return;
    }
    result->status = status;
    result->value.kind = GOTHICOS_VALUE_NONE;
    result->value.integer = 0;
    result->value.string = NULL;
}

static const char *result_string(nk_kernel_t *kernel, gothicos_result_t *result,
                                 int status, const char *value)
{
    if (result == NULL) {
        return NULL;
    }
    nk_strcpy(kernel->string_buffer, value, sizeof(kernel->string_buffer));
    result->status = status;
    result->value.kind = GOTHICOS_VALUE_STRING;
    result->value.integer = 0;
    result->value.string = kernel->string_buffer;
    return kernel->string_buffer;
}

static void message_from_value(nk_message_t *message, int sender,
                               const gothicos_value_t *value)
{
    message->sender = sender;
    message->kind = value == NULL ? GOTHICOS_VALUE_NONE : (int)value->kind;
    message->integer = value == NULL ? 0 : value->integer;
    if (value != NULL && value->kind == GOTHICOS_VALUE_STRING && value->string != NULL) {
        nk_strcpy(message->text, value->string, sizeof(message->text));
    } else {
        message->text[0] = '\0';
    }
}

static gothicos_value_t message_value(nk_message_t *message)
{
    gothicos_value_t value;
    value.kind = (gothicos_value_kind_t)message->kind;
    value.integer = message->integer;
    value.string = message->kind == GOTHICOS_VALUE_STRING ? message->text : NULL;
    return value;
}

/* --- lookup helpers ----------------------------------------------------- */

static nk_task_t *find_task(nk_kernel_t *kernel, int task_id)
{
    if (kernel == NULL || task_id < 0 || task_id >= NK_MAX_TASKS) {
        return NULL;
    }
    return kernel->tasks[task_id].step != NULL || kernel->tasks[task_id].state != 0
               ? &kernel->tasks[task_id]
               : NULL;
}

static nk_port_t *find_port(nk_kernel_t *kernel, const char *name)
{
    int index;
    if (kernel == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < NK_MAX_PORTS; index++) {
        if (kernel->ports[index].name[0] != '\0' &&
            nk_strcmp(kernel->ports[index].name, name) == 0) {
            return &kernel->ports[index];
        }
    }
    return NULL;
}

static nk_pool_t *find_pool(nk_kernel_t *kernel, const char *name)
{
    int index;
    if (kernel == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < NK_MAX_POOLS; index++) {
        if (kernel->pools[index].name[0] != '\0' &&
            nk_strcmp(kernel->pools[index].name, name) == 0) {
            return &kernel->pools[index];
        }
    }
    return NULL;
}

static nk_driver_t *find_driver(nk_kernel_t *kernel, const char *name)
{
    int index;
    if (kernel == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < NK_MAX_DRIVERS; index++) {
        if (kernel->drivers[index].name[0] != '\0' &&
            nk_strcmp(kernel->drivers[index].name, name) == 0) {
            return &kernel->drivers[index];
        }
    }
    return NULL;
}

static nk_ability_t *find_ability(nk_kernel_t *kernel, const char *name)
{
    int index;
    if (kernel == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0; index < NK_MAX_ABILITIES; index++) {
        if (kernel->abilities[index].name[0] != '\0' &&
            nk_strcmp(kernel->abilities[index].name, name) == 0) {
            return &kernel->abilities[index];
        }
    }
    return NULL;
}

static nk_node_t *find_node(nk_kernel_t *kernel, const char *id)
{
    int index;
    if (kernel == NULL || id == NULL) {
        return NULL;
    }
    for (index = 0; index < NK_MAX_NODES; index++) {
        if (kernel->nodes[index].id[0] != '\0' &&
            nk_strcmp(kernel->nodes[index].id, id) == 0) {
            return &kernel->nodes[index];
        }
    }
    return NULL;
}

static int holder_index(int holder)
{
    return holder < 0 ? 0 : holder + 1;
}

static int require_capability(nk_kernel_t *kernel, int right)
{
    int holder = kernel == NULL ? -1 : kernel->current_task;
    if (holder < 0) {
        return GOTHICOS_STATUS_OK;
    }
    if (holder >= NK_MAX_TASKS) {
        return GOTHICOS_STATUS_PERMISSION_DENIED;
    }
    return (kernel->capabilities[holder_index(holder)] & right) != 0
               ? GOTHICOS_STATUS_OK
               : GOTHICOS_STATUS_PERMISSION_DENIED;
}

/* --- scheduler ---------------------------------------------------------- */

int nk_task_spawn(nk_kernel_t *kernel, const char *name, nk_step_fn step, void *context)
{
    int index;
    if (kernel == NULL || kernel->magic != NK_MAGIC || name == NULL || step == NULL) {
        return -1;
    }
    for (index = 0; index < NK_MAX_TASKS; index++) {
        if (kernel->tasks[index].step == NULL && kernel->tasks[index].state == 0) {
            nk_task_t *task = &kernel->tasks[index];
            nk_strcpy(task->name, name, sizeof(task->name));
            task->step = step;
            task->context = context;
            task->state = GOTHICOS_TASK_READY;
            task->head = 0;
            task->tail = 0;
            task->count = 0;
            kernel->capabilities[holder_index(index)] = GOTHICOS_FULL_RIGHTS;
            return index;
        }
    }
    return -1;
}

static int nk_task_stop(nk_kernel_t *kernel, int task_id)
{
    nk_task_t *task = find_task(kernel, task_id);
    if (task == NULL) {
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    task->state = GOTHICOS_TASK_STOPPED;
    kernel->capabilities[holder_index(task_id)] = 0;
    return GOTHICOS_STATUS_OK;
}

static int nk_task_resume(nk_kernel_t *kernel, int task_id)
{
    nk_task_t *task = find_task(kernel, task_id);
    if (task == NULL || task->state != GOTHICOS_TASK_BLOCKED) {
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    task->state = GOTHICOS_TASK_READY;
    return GOTHICOS_STATUS_OK;
}

int nk_dispatch_one(nk_kernel_t *kernel)
{
    int offset;
    int task_id = -1;
    nk_task_t *task;
    int keep_running;

    if (kernel == NULL || kernel->magic != NK_MAGIC) {
        return -1;
    }
    for (offset = 0; offset < NK_MAX_TASKS; offset++) {
        int slot = (kernel->cursor + offset) % NK_MAX_TASKS;
        if (kernel->tasks[slot].state == GOTHICOS_TASK_READY &&
            kernel->tasks[slot].step != NULL) {
            task_id = slot;
            break;
        }
    }
    if (task_id < 0) {
        return -1;
    }
    task = &kernel->tasks[task_id];
    kernel->cursor = (task_id + 1) % NK_MAX_TASKS;
    task->state = GOTHICOS_TASK_RUNNING;
    kernel->current_task = task_id;
    keep_running = task->step(kernel, task_id, task->context);
    kernel->current_task = -1;
    if (task->state == GOTHICOS_TASK_RUNNING) {
        if (keep_running == 0) {
            task->state = GOTHICOS_TASK_STOPPED;
            kernel->capabilities[holder_index(task_id)] = 0;
        } else {
            task->state = GOTHICOS_TASK_READY;
        }
    }
    return task_id;
}

int nk_yield(nk_kernel_t *kernel)
{
    return nk_dispatch_one(kernel) < 0 ? GOTHICOS_STATUS_OK : GOTHICOS_STATUS_OK;
}

int nk_run(nk_kernel_t *kernel, int max_steps)
{
    int steps = 0;
    int limit = max_steps < 0 ? 1000000 : max_steps;
    if (kernel == NULL || kernel->magic != NK_MAGIC) {
        return 0;
    }
    while (steps < limit) {
        int active = 0;
        int index;
        if (nk_dispatch_one(kernel) >= 0) {
            steps++;
            continue;
        }
        for (index = 0; index < NK_MAX_TASKS; index++) {
            if (kernel->tasks[index].step != NULL &&
                kernel->tasks[index].state != GOTHICOS_TASK_STOPPED) {
                active = 1;
                break;
            }
        }
        if (!active) {
            break;
        }
        /* A blocked task can only be woken by an IRQ service or an explicit send. */
        nk_service_irqs(kernel, 4);
        if (nk_dispatch_one(kernel) >= 0) {
            steps++;
        } else {
            break;
        }
    }
    return steps;
}

/* --- ports, memory, interrupts, boot ------------------------------------ */

int nk_port_open(nk_kernel_t *kernel, const char *name, int owner, int capacity)
{
    int index;
    int depth;
    if (kernel == NULL || name == NULL || name[0] == '\0') {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (find_port(kernel, name) != NULL) {
        return GOTHICOS_STATUS_ALREADY_EXISTS;
    }
    depth = capacity <= 0 ? NK_PORT_DEPTH : capacity;
    if (depth > NK_PORT_DEPTH) {
        depth = NK_PORT_DEPTH;
    }
    for (index = 0; index < NK_MAX_PORTS; index++) {
        nk_port_t *port = &kernel->ports[index];
        if (port->name[0] == '\0') {
            nk_strcpy(port->name, name, sizeof(port->name));
            port->owner = owner;
            port->capacity = depth;
            return GOTHICOS_STATUS_OK;
        }
    }
    return GOTHICOS_STATUS_OUT_OF_MEMORY;
}

int nk_port_send(nk_kernel_t *kernel, const char *name, int sender,
                 const gothicos_value_t *message)
{
    nk_port_t *port = find_port(kernel, name);
    int status;
    if (port == NULL) {
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    status = require_capability(kernel, GOTHICOS_RIGHT_WRITE);
    if (status != GOTHICOS_STATUS_OK) {
        return status;
    }
    if (port->count >= port->capacity) {
        return GOTHICOS_STATUS_QUEUE_FULL;
    }
    message_from_value(&port->messages[port->tail], sender, message);
    port->tail = (port->tail + 1) % port->capacity;
    port->count++;
    return GOTHICOS_STATUS_OK;
}

int nk_port_receive(nk_kernel_t *kernel, const char *name, int task_id,
                    gothicos_value_t *message, int *sender)
{
    nk_port_t *port = find_port(kernel, name);
    nk_message_t stored;
    if (port == NULL || message == NULL) {
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    if (port->count == 0) {
        nk_task_t *task = find_task(kernel, task_id);
        if (task != NULL && task_id == kernel->current_task) {
            task->state = GOTHICOS_TASK_BLOCKED;
        }
        return GOTHICOS_STATUS_WOULD_BLOCK;
    }
    stored = port->messages[port->head];
    port->messages[port->head].text[0] = '\0';
    port->head = (port->head + 1) % port->capacity;
    port->count--;
    *message = message_value(&stored);
    if (sender != NULL) {
        *sender = stored.sender;
    }
    return GOTHICOS_STATUS_OK;
}

int nk_driver_register(nk_kernel_t *kernel, const char *name, nk_driver_io_fn io,
                       void *context)
{
    int index;
    if (kernel == NULL || name == NULL || io == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (find_driver(kernel, name) != NULL) {
        return GOTHICOS_STATUS_ALREADY_EXISTS;
    }
    for (index = 0; index < NK_MAX_DRIVERS; index++) {
        nk_driver_t *driver = &kernel->drivers[index];
        if (driver->name[0] == '\0') {
            nk_strcpy(driver->name, name, sizeof(driver->name));
            driver->io = io;
            driver->context = context;
            return GOTHICOS_STATUS_OK;
        }
    }
    return GOTHICOS_STATUS_OUT_OF_MEMORY;
}

int nk_ability_register(nk_kernel_t *kernel, const char *name, const char *subsystem,
                        nk_ability_fn invoke, void *context)
{
    int index;
    if (kernel == NULL || name == NULL || name[0] == '\0') {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (find_ability(kernel, name) != NULL) {
        return GOTHICOS_STATUS_ALREADY_EXISTS;
    }
    for (index = 0; index < NK_MAX_ABILITIES; index++) {
        nk_ability_t *ability = &kernel->abilities[index];
        if (ability->name[0] == '\0') {
            nk_strcpy(ability->name, name, sizeof(ability->name));
            nk_strcpy(ability->subsystem, subsystem == NULL ? "" : subsystem,
                      sizeof(ability->subsystem));
            ability->state = GOTHICOS_ABILITY_STOPPED;
            ability->invoke = invoke;
            ability->context = context;
            return GOTHICOS_STATUS_OK;
        }
    }
    return GOTHICOS_STATUS_OUT_OF_MEMORY;
}

int nk_service_irqs(nk_kernel_t *kernel, int limit)
{
    int serviced = 0;
    if (kernel == NULL || kernel->magic != NK_MAGIC) {
        return 0;
    }
    while (kernel->pending_count > 0 && serviced < limit) {
        nk_pending_irq_t pending = kernel->pending[kernel->pending_head];
        int index;
        kernel->pending_head = (kernel->pending_head + 1) % NK_MAX_IRQS;
        kernel->pending_count--;
        for (index = 0; index < NK_MAX_IRQS; index++) {
            nk_irq_t *irq = &kernel->irqs[index];
            if (irq->source == pending.source && irq->enabled && irq->handler != NULL) {
                irq->handler(kernel, index, pending.source, irq->context);
            }
        }
        serviced++;
    }
    return serviced;
}

int nk_boot(nk_kernel_t *kernel)
{
    static const char *const stages[] = {
        "platform", "topology", "memory", "ports", "irq",
        "drivers", "subsystems", "distributed", "abi"
    };
    int index;
    if (kernel == NULL || kernel->magic != NK_MAGIC) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (kernel->boot_status == NK_BOOT_COMPLETE) {
        return GOTHICOS_STATUS_OK;
    }
    kernel->boot_stage_count = 0;
    for (index = 0; index < (int)(sizeof(stages) / sizeof(stages[0])); index++) {
        nk_strcpy(kernel->boot_stages[kernel->boot_stage_count], stages[index],
                  NK_NAME_MAX);
        kernel->boot_stage_count++;
        if (nk_strcmp(stages[index], "ports") == 0) {
            nk_port_open(kernel, "kernel", -1, NK_PORT_DEPTH);
        }
    }
    kernel->boot_status = NK_BOOT_COMPLETE;
    return GOTHICOS_STATUS_OK;
}

int nk_current_task(nk_kernel_t *kernel)
{
    return kernel == NULL ? -1 : kernel->current_task;
}

int nk_info(nk_kernel_t *kernel, nk_info_t *info)
{
    int index;
    int count = 0;
    if (kernel == NULL || info == NULL) {
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < NK_MAX_TASKS; index++) {
        if (kernel->tasks[index].step != NULL) {
            count++;
        }
    }
    info->abi_version = nk_abi_version();
    info->capabilities = GOTHICOS_CAP_TASKS | GOTHICOS_CAP_IPC | GOTHICOS_CAP_MULTICORE |
                         GOTHICOS_CAP_SUBSYSTEMS | GOTHICOS_CAP_DISTRIBUTED;
    info->max_tasks = NK_MAX_TASKS;
    info->mailbox_depth = NK_MAILBOX_DEPTH;
    info->core_count = kernel->core_count;
    info->first_managed_cpu = kernel->first_managed_cpu;
    info->task_count = (uint32_t)count;
    info->boot_status = (uint32_t)kernel->boot_status;
    return GOTHICOS_STATUS_OK;
}

/* --- ABI dispatch ------------------------------------------------------- */

static int abi_ipc_send(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                        gothicos_result_t *result)
{
    nk_task_t *receiver = find_task(kernel, arg_int(args, 0));
    gothicos_value_t value;
    int sender = arg_count(args) > 2 ? arg_int(args, 2) : nk_current_task(kernel);
    if (receiver == NULL) {
        result_none(result, GOTHICOS_STATUS_NOT_FOUND);
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    if (receiver->count >= NK_MAILBOX_DEPTH) {
        result_none(result, GOTHICOS_STATUS_QUEUE_FULL);
        return GOTHICOS_STATUS_QUEUE_FULL;
    }
    if (arg_count(args) > 1 && args->items != NULL &&
        args->items[1].kind != GOTHICOS_VALUE_NONE) {
        value = args->items[1];
    } else {
        value.kind = GOTHICOS_VALUE_NONE;
        value.integer = 0;
        value.string = NULL;
    }
    message_from_value(&receiver->mailbox[receiver->tail], sender, &value);
    receiver->tail = (receiver->tail + 1) % NK_MAILBOX_DEPTH;
    receiver->count++;
    if (receiver->state == GOTHICOS_TASK_BLOCKED) {
        receiver->state = GOTHICOS_TASK_READY;
    }
    result_none(result, GOTHICOS_STATUS_OK);
    return GOTHICOS_STATUS_OK;
}

static int abi_ipc_receive(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                           gothicos_result_t *result)
{
    int task_id = arg_count(args) > 0 ? arg_int(args, 0) : nk_current_task(kernel);
    nk_task_t *task = find_task(kernel, task_id);
    nk_message_t message;
    if (task == NULL || task->count == 0) {
        if (task != NULL && task_id == nk_current_task(kernel)) {
            task->state = GOTHICOS_TASK_BLOCKED;
        }
        result_none(result, GOTHICOS_STATUS_WOULD_BLOCK);
        return GOTHICOS_STATUS_WOULD_BLOCK;
    }
    message = task->mailbox[task->head];
    task->mailbox[task->head].text[0] = '\0';
    task->head = (task->head + 1) % NK_MAILBOX_DEPTH;
    task->count--;
    if (result != NULL) {
        result->status = GOTHICOS_STATUS_OK;
        result->value = message_value(&message);
        if (result->value.string != NULL) {
            result_string(kernel, result, GOTHICOS_STATUS_OK, message.text);
        }
    }
    return GOTHICOS_STATUS_OK;
}

static int abi_pool_create(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                           gothicos_result_t *result)
{
    const char *name = arg_string(args, 0);
    int block_size = arg_int(args, 1);
    int block_count = arg_int(args, 2);
    int index;
    if (name == NULL || block_size < 1 || block_size > NK_POOL_BLOCK_MAX ||
        block_count < 1 || block_count > NK_POOL_BLOCKS_MAX) {
        result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }
    if (find_pool(kernel, name) != NULL) {
        result_none(result, GOTHICOS_STATUS_ALREADY_EXISTS);
        return GOTHICOS_STATUS_ALREADY_EXISTS;
    }
    for (index = 0; index < NK_MAX_POOLS; index++) {
        nk_pool_t *pool = &kernel->pools[index];
        if (pool->name[0] == '\0') {
            nk_strcpy(pool->name, name, sizeof(pool->name));
            pool->block_size = block_size;
            pool->block_count = block_count;
            result_int(result, GOTHICOS_STATUS_OK, block_count);
            return GOTHICOS_STATUS_OK;
        }
    }
    result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
    return GOTHICOS_STATUS_OUT_OF_MEMORY;
}

static int abi_driver_io(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                         gothicos_result_t *result)
{
    const char *name = arg_string(args, 0);
    const char *command = arg_string(args, 1);
    nk_driver_t *driver = find_driver(kernel, name);
    gothicos_value_t argument;
    gothicos_value_t output;
    int status;
    if (driver == NULL || command == NULL) {
        result_none(result, GOTHICOS_STATUS_NOT_FOUND);
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    argument.kind = GOTHICOS_VALUE_NONE;
    argument.integer = 0;
    argument.string = NULL;
    if (arg_count(args) > 2 && args->items != NULL) {
        argument = args->items[2];
    }
    output.kind = GOTHICOS_VALUE_NONE;
    output.integer = 0;
    output.string = NULL;
    status = driver->io(kernel, driver->name, command, &argument, &output, driver->context);
    if (status != GOTHICOS_STATUS_OK) {
        result_none(result, status);
        return status;
    }
    if (output.kind == GOTHICOS_VALUE_STRING && output.string != NULL) {
        result_string(kernel, result, status, output.string);
    } else {
        result_int(result, status, output.integer);
    }
    return status;
}

static int abi_softbus_send(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                            gothicos_result_t *result)
{
    const char *target = arg_string(args, 0);
    nk_bus_message_t *message;
    int slot;
    if (target == NULL || find_node(kernel, target) == NULL) {
        result_none(result, GOTHICOS_STATUS_NOT_FOUND);
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    if (kernel->bus_message_count >= NK_MAX_SOFTBUS_MESSAGES) {
        result_none(result, GOTHICOS_STATUS_QUEUE_FULL);
        return GOTHICOS_STATUS_QUEUE_FULL;
    }
    slot = kernel->bus_message_count++;
    message = &kernel->bus_messages[slot];
    message->id = kernel->next_message_id++;
    nk_strcpy(message->source, kernel->nodes[0].id, sizeof(message->source));
    nk_strcpy(message->target, target, sizeof(message->target));
    message->kind = arg_int(args, 1);
    message->correlation_id = arg_count(args) > 3 ? arg_int(args, 3) : 0;
    message->integer = 0;
    message->text[0] = '\0';
    if (arg_count(args) > 2 && args->items != NULL &&
        args->items[2].kind == GOTHICOS_VALUE_STRING && args->items[2].string != NULL) {
        nk_strcpy(message->text, args->items[2].string, sizeof(message->text));
    } else {
        message->integer = arg_int(args, 2);
    }
    result_int(result, GOTHICOS_STATUS_OK, message->id);
    return GOTHICOS_STATUS_OK;
}

static int abi_schedule(nk_kernel_t *kernel, const gothicos_arguments_t *args,
                        gothicos_result_t *result)
{
    int index;
    int best = -1;
    int best_score = -1;
    for (index = 0; index < NK_MAX_NODES; index++) {
        nk_node_t *node = &kernel->nodes[index];
        int score;
        if (node->id[0] == '\0') {
            continue;
        }
        score = node->capacity - node->load;
        if (arg_string(args, 0) != NULL) {
            int feature;
            for (feature = 0; feature < node->feature_count; feature++) {
                if (nk_strcmp(node->features[feature], arg_string(args, 0)) == 0) {
                    score += 20;
                }
            }
        }
        if (score > best_score) {
            best_score = score;
            best = index;
        }
    }
    if (best < 0) {
        result_none(result, GOTHICOS_STATUS_NOT_FOUND);
        return GOTHICOS_STATUS_NOT_FOUND;
    }
    result_string(kernel, result, GOTHICOS_STATUS_OK, kernel->nodes[best].id);
    return GOTHICOS_STATUS_OK;
}

int nk_invoke(nk_kernel_t *kernel, int opcode, const gothicos_arguments_t *arguments,
              gothicos_result_t *result)
{
    int status = GOTHICOS_STATUS_UNSUPPORTED;
    int index;

    if (result != NULL) {
        result->status = GOTHICOS_STATUS_UNSUPPORTED;
        result->value.kind = GOTHICOS_VALUE_NONE;
        result->value.integer = 0;
        result->value.string = NULL;
    }
    if (kernel == NULL || kernel->magic != NK_MAGIC) {
        result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
        return GOTHICOS_STATUS_INVALID_ARGUMENT;
    }

    switch (opcode) {
    case GOTHICOS_OP_GET_INFO: {
        nk_info_t info;
        nk_info(kernel, &info);
        result_int(result, GOTHICOS_STATUS_OK, (int)info.abi_version);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_TASK_CREATE: {
        const char *name = arg_string(arguments, 0);
        nk_step_fn step = NULL;
        void *context = NULL;
        if (name == NULL || name[0] == '\0') {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        if (kernel->step_resolver != NULL) {
            kernel->step_resolver(name, &step, &context);
        }
        if (step == NULL) {
            result_none(result, GOTHICOS_STATUS_UNSUPPORTED);
            return GOTHICOS_STATUS_UNSUPPORTED;
        }
        index = nk_task_spawn(kernel, name, step, context);
        if (index < 0) {
            result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
            return GOTHICOS_STATUS_OUT_OF_MEMORY;
        }
        result_int(result, GOTHICOS_STATUS_OK, index);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_TASK_STOP:
        status = nk_task_stop(kernel, arg_int(arguments, 0));
        result_none(result, status);
        return status;
    case GOTHICOS_OP_TASK_RESUME:
        status = nk_task_resume(kernel, arg_int(arguments, 0));
        result_none(result, status);
        return status;
    case GOTHICOS_OP_TASK_STATE: {
        nk_task_t *task = find_task(kernel, arg_int(arguments, 0));
        if (task == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        result_int(result, GOTHICOS_STATUS_OK, task->state);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_TASK_INFO: {
        nk_task_t *task = find_task(kernel, arg_int(arguments, 0));
        if (task == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        result_string(kernel, result, GOTHICOS_STATUS_OK, task->name);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_IPC_SEND:
        return abi_ipc_send(kernel, arguments, result);
    case GOTHICOS_OP_IPC_RECEIVE:
        return abi_ipc_receive(kernel, arguments, result);
    case GOTHICOS_OP_YIELD:
        nk_yield(kernel);
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_RUN:
        result_int(result, GOTHICOS_STATUS_OK,
                   nk_run(kernel, arg_count(arguments) > 0 ? arg_int(arguments, 0) : 8));
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_PORT_OPEN: {
        const char *name = arg_string(arguments, 0);
        int capacity = arg_count(arguments) > 1 ? arg_int(arguments, 1) : NK_PORT_DEPTH;
        if (name == NULL) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        index = nk_port_open(kernel, name, nk_current_task(kernel), capacity);
        result_none(result, index);
        return index;
    }
    case GOTHICOS_OP_PORT_CLOSE: {
        nk_port_t *port = find_port(kernel, arg_string(arguments, 0));
        if (port == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        port->name[0] = '\0';
        port->count = 0;
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_PORT_SEND: {
        gothicos_value_t value;
        const char *name = arg_string(arguments, 0);
        int sender = arg_count(arguments) > 2 ? arg_int(arguments, 2) : nk_current_task(kernel);
        if (name == NULL) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        value.kind = GOTHICOS_VALUE_NONE;
        value.integer = 0;
        value.string = NULL;
        if (arg_count(arguments) > 1 && arguments->items != NULL) {
            value = arguments->items[1];
        }
        status = nk_port_send(kernel, name, sender, &value);
        result_none(result, status);
        return status;
    }
    case GOTHICOS_OP_PORT_RECEIVE: {
        gothicos_value_t value;
        int sender = -1;
        const char *name = arg_string(arguments, 0);
        int task_id = arg_count(arguments) > 1 ? arg_int(arguments, 1) : nk_current_task(kernel);
        if (name == NULL) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        status = nk_port_receive(kernel, name, task_id, &value, &sender);
        if (status != GOTHICOS_STATUS_OK) {
            result_none(result, status);
            return status;
        }
        if (result != NULL) {
            result->status = status;
            result->value = value;
            if (value.string != NULL) {
                result_string(kernel, result, status, value.string);
            }
        }
        return status;
    }
    case GOTHICOS_OP_PORT_INFO: {
        nk_port_t *port = find_port(kernel, arg_string(arguments, 0));
        if (port == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        result_int(result, GOTHICOS_STATUS_OK, port->count);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_MEM_POOL_CREATE:
        return abi_pool_create(kernel, arguments, result);
    case GOTHICOS_OP_MEM_ALLOC: {
        nk_pool_t *pool = find_pool(kernel, arg_string(arguments, 0));
        int block = -1;
        int slot;
        if (pool == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        for (slot = 0; slot < pool->block_count; slot++) {
            if (!pool->used[slot]) {
                block = slot;
                break;
            }
        }
        if (block < 0) {
            result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
            return GOTHICOS_STATUS_OUT_OF_MEMORY;
        }
        pool->used[block] = 1;
        result_int(result, GOTHICOS_STATUS_OK, block);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_MEM_FREE: {
        nk_pool_t *pool = find_pool(kernel, arg_string(arguments, 0));
        int block = arg_int(arguments, 1);
        if (pool == NULL || block < 0 || block >= pool->block_count || !pool->used[block]) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        pool->used[block] = 0;
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_MEM_WRITE: {
        nk_pool_t *pool = find_pool(kernel, arg_string(arguments, 0));
        int block = arg_int(arguments, 1);
        const char *data = arg_string(arguments, 2);
        if (pool == NULL || block < 0 || block >= pool->block_count || !pool->used[block] ||
            data == NULL) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        nk_strcpy((char *)&pool->blocks[block * pool->block_size], data,
                  (size_t)pool->block_size);
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_MEM_READ: {
        nk_pool_t *pool = find_pool(kernel, arg_string(arguments, 0));
        int block = arg_int(arguments, 1);
        if (pool == NULL || block < 0 || block >= pool->block_count || !pool->used[block]) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        result_string(kernel, result, GOTHICOS_STATUS_OK,
                      (const char *)&pool->blocks[block * pool->block_size]);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_IRQ_REGISTER: {
        int source = arg_int(arguments, 0);
        for (index = 0; index < NK_MAX_IRQS; index++) {
            if (kernel->irqs[index].source == 0 && kernel->irqs[index].handler == NULL) {
                kernel->irqs[index].source = source == 0 ? NK_MAX_IRQS : source;
                kernel->irqs[index].enabled = 1;
                result_int(result, GOTHICOS_STATUS_OK, index);
                return GOTHICOS_STATUS_OK;
            }
        }
        result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
        return GOTHICOS_STATUS_OUT_OF_MEMORY;
    }
    case GOTHICOS_OP_IRQ_UNREGISTER:
        index = arg_int(arguments, 0);
        if (index < 0 || index >= NK_MAX_IRQS || kernel->irqs[index].source == 0) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        kernel->irqs[index].source = 0;
        kernel->irqs[index].enabled = 0;
        kernel->irqs[index].handler = NULL;
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_IRQ_RAISE: {
        int source = arg_int(arguments, 0);
        if (kernel->pending_count >= NK_MAX_IRQS) {
            result_none(result, GOTHICOS_STATUS_QUEUE_FULL);
            return GOTHICOS_STATUS_QUEUE_FULL;
        }
        kernel->pending[kernel->pending_tail].source = source;
        kernel->pending[kernel->pending_tail].payload = arg_int(arguments, 1);
        kernel->pending_tail = (kernel->pending_tail + 1) % NK_MAX_IRQS;
        kernel->pending_count++;
        nk_service_irqs(kernel, 1);
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_IRQ_SERVICE:
        result_int(result, GOTHICOS_STATUS_OK,
                   nk_service_irqs(kernel, arg_count(arguments) > 0 ? arg_int(arguments, 0) : 4));
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_DRIVER_INFO: {
        nk_driver_t *driver = find_driver(kernel, arg_string(arguments, 0));
        result_none(result, driver == NULL ? GOTHICOS_STATUS_NOT_FOUND : GOTHICOS_STATUS_OK);
        return result->status;
    }
    case GOTHICOS_OP_DRIVER_IO:
        return abi_driver_io(kernel, arguments, result);
    case GOTHICOS_OP_CAP_GRANT: {
        int holder = arg_int(arguments, 0);
        int rights = arg_int(arguments, 2);
        if (holder < -1 || holder >= NK_MAX_TASKS) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        kernel->capabilities[holder_index(holder)] = (uint16_t)rights;
        result_int(result, GOTHICOS_STATUS_OK, rights);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_CAP_REVOKE: {
        int holder = arg_int(arguments, 0);
        int rights = arg_count(arguments) > 1 ? arg_int(arguments, 1) : GOTHICOS_FULL_RIGHTS;
        if (holder < -1 || holder >= NK_MAX_TASKS) {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        kernel->capabilities[holder_index(holder)] &= (uint16_t)~rights;
        result_none(result, GOTHICOS_STATUS_OK);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_CAP_INFO: {
        int granted = 0;
        for (index = 0; index <= NK_MAX_TASKS; index++) {
            if (kernel->capabilities[index] != 0) {
                granted++;
            }
        }
        result_int(result, GOTHICOS_STATUS_OK, granted);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_BOOT_INFO:
        result_int(result, GOTHICOS_STATUS_OK, (int)kernel->boot_status);
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_ABILITY_REGISTER:
        status = nk_ability_register(kernel, arg_string(arguments, 0),
                                     arg_string(arguments, 1), NULL, NULL);
        result_none(result, status);
        return status;
    case GOTHICOS_OP_ABILITY_START:
    case GOTHICOS_OP_ABILITY_STOP: {
        nk_ability_t *ability = find_ability(kernel, arg_string(arguments, 0));
        if (ability == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        ability->state = opcode == GOTHICOS_OP_ABILITY_START ? GOTHICOS_ABILITY_RUNNING
                                                            : GOTHICOS_ABILITY_STOPPED;
        result_int(result, GOTHICOS_STATUS_OK, ability->state);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_ABILITY_STATE:
    case GOTHICOS_OP_ABILITY_INFO: {
        nk_ability_t *ability = find_ability(kernel, arg_string(arguments, 0));
        if (ability == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        if (opcode == GOTHICOS_OP_ABILITY_STATE) {
            result_int(result, GOTHICOS_STATUS_OK, ability->state);
        } else {
            result_string(kernel, result, GOTHICOS_STATUS_OK, ability->subsystem);
        }
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_SOFTBUS_SEND:
        return abi_softbus_send(kernel, arguments, result);
    case GOTHICOS_OP_SOFTBUS_SUBSCRIBE:
        for (index = 0; index < NK_MAX_SUBSCRIPTIONS; index++) {
            if (kernel->bus_subscriptions[index] == 0) {
                kernel->bus_subscriptions[index] = arg_int(arguments, 0);
                result_none(result, GOTHICOS_STATUS_OK);
                return GOTHICOS_STATUS_OK;
            }
        }
        result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
        return GOTHICOS_STATUS_OUT_OF_MEMORY;
    case GOTHICOS_OP_SOFTBUS_INFO:
        result_int(result, GOTHICOS_STATUS_OK, kernel->bus_message_count);
        return GOTHICOS_STATUS_OK;
    case GOTHICOS_OP_REMOTE_CALL: {
        nk_ability_t *ability = find_ability(kernel, arg_string(arguments, 0));
        if (ability == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        if (ability->invoke == NULL) {
            result_none(result, GOTHICOS_STATUS_UNSUPPORTED);
            return GOTHICOS_STATUS_UNSUPPORTED;
        }
        return ability->invoke(kernel, ability->name, arguments, result, ability->context);
    }
    case GOTHICOS_OP_REMOTE_ROUTE: {
        const char *ability = arg_string(arguments, 0);
        const char *node = arg_string(arguments, 1);
        if (ability == NULL || node == NULL || find_node(kernel, node) == NULL) {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        for (index = 0; index < NK_MAX_ROUTES; index++) {
            if (kernel->routes[index].ability[0] == '\0') {
                nk_strcpy(kernel->routes[index].ability, ability, NK_NAME_MAX);
                nk_strcpy(kernel->routes[index].node, node, NK_NAME_MAX);
                result_none(result, GOTHICOS_STATUS_OK);
                return GOTHICOS_STATUS_OK;
            }
        }
        result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
        return GOTHICOS_STATUS_OUT_OF_MEMORY;
    }
    case GOTHICOS_OP_NODE_REGISTER: {
        const char *id = arg_string(arguments, 0);
        if (id == NULL || id[0] == '\0') {
            result_none(result, GOTHICOS_STATUS_INVALID_ARGUMENT);
            return GOTHICOS_STATUS_INVALID_ARGUMENT;
        }
        if (find_node(kernel, id) != NULL) {
            result_none(result, GOTHICOS_STATUS_ALREADY_EXISTS);
            return GOTHICOS_STATUS_ALREADY_EXISTS;
        }
        for (index = 0; index < NK_MAX_NODES; index++) {
            nk_node_t *node = &kernel->nodes[index];
            if (node->id[0] == '\0') {
                nk_strcpy(node->id, id, sizeof(node->id));
                nk_strcpy(node->name, arg_string(arguments, 1) == NULL
                                          ? id : arg_string(arguments, 1),
                          sizeof(node->name));
                node->capacity = arg_count(arguments) > 2 ? arg_int(arguments, 2) : 100;
                node->load = arg_count(arguments) > 3 ? arg_int(arguments, 3) : 0;
                result_int(result, GOTHICOS_STATUS_OK, index);
                return GOTHICOS_STATUS_OK;
            }
        }
        result_none(result, GOTHICOS_STATUS_OUT_OF_MEMORY);
        return GOTHICOS_STATUS_OUT_OF_MEMORY;
    }
    case GOTHICOS_OP_NODE_INFO: {
        nk_node_t *node = arg_string(arguments, 0) == NULL
                              ? &kernel->nodes[0]
                              : find_node(kernel, arg_string(arguments, 0));
        if (node == NULL || node->id[0] == '\0') {
            result_none(result, GOTHICOS_STATUS_NOT_FOUND);
            return GOTHICOS_STATUS_NOT_FOUND;
        }
        result_string(kernel, result, GOTHICOS_STATUS_OK, node->id);
        return GOTHICOS_STATUS_OK;
    }
    case GOTHICOS_OP_SCHEDULE:
        return abi_schedule(kernel, arguments, result);
    case GOTHICOS_OP_DISTRIBUTED_INFO: {
        int nodes = 0;
        for (index = 0; index < NK_MAX_NODES; index++) {
            if (kernel->nodes[index].id[0] != '\0') {
                nodes++;
            }
        }
        result_int(result, GOTHICOS_STATUS_OK, nodes);
        return GOTHICOS_STATUS_OK;
    }
    default:
        result_none(result, GOTHICOS_STATUS_UNSUPPORTED);
        return GOTHICOS_STATUS_UNSUPPORTED;
    }
}

/* --- introspection ------------------------------------------------------ */

const char *nk_status_name(int status)
{
    switch (status) {
    case GOTHICOS_STATUS_OK: return "OK";
    case GOTHICOS_STATUS_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case GOTHICOS_STATUS_NOT_FOUND: return "NOT_FOUND";
    case GOTHICOS_STATUS_WOULD_BLOCK: return "WOULD_BLOCK";
    case GOTHICOS_STATUS_QUEUE_FULL: return "QUEUE_FULL";
    case GOTHICOS_STATUS_UNSUPPORTED: return "UNSUPPORTED";
    case GOTHICOS_STATUS_INTERNAL_ERROR: return "INTERNAL_ERROR";
    case GOTHICOS_STATUS_PERMISSION_DENIED: return "PERMISSION_DENIED";
    case GOTHICOS_STATUS_ALREADY_EXISTS: return "ALREADY_EXISTS";
    case GOTHICOS_STATUS_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    default: return "UNKNOWN";
    }
}

const char *nk_opcode_name(int opcode)
{
    switch (opcode) {
    case GOTHICOS_OP_GET_INFO: return "GET_INFO";
    case GOTHICOS_OP_TASK_CREATE: return "TASK_CREATE";
    case GOTHICOS_OP_TASK_STOP: return "TASK_STOP";
    case GOTHICOS_OP_TASK_RESUME: return "TASK_RESUME";
    case GOTHICOS_OP_TASK_STATE: return "TASK_STATE";
    case GOTHICOS_OP_IPC_SEND: return "IPC_SEND";
    case GOTHICOS_OP_IPC_RECEIVE: return "IPC_RECEIVE";
    case GOTHICOS_OP_YIELD: return "YIELD";
    case GOTHICOS_OP_RUN: return "RUN";
    case GOTHICOS_OP_TASK_INFO: return "TASK_INFO";
    case GOTHICOS_OP_PORT_OPEN: return "PORT_OPEN";
    case GOTHICOS_OP_PORT_CLOSE: return "PORT_CLOSE";
    case GOTHICOS_OP_PORT_SEND: return "PORT_SEND";
    case GOTHICOS_OP_PORT_RECEIVE: return "PORT_RECEIVE";
    case GOTHICOS_OP_PORT_INFO: return "PORT_INFO";
    case GOTHICOS_OP_MEM_POOL_CREATE: return "MEM_POOL_CREATE";
    case GOTHICOS_OP_MEM_ALLOC: return "MEM_ALLOC";
    case GOTHICOS_OP_MEM_FREE: return "MEM_FREE";
    case GOTHICOS_OP_MEM_WRITE: return "MEM_WRITE";
    case GOTHICOS_OP_MEM_READ: return "MEM_READ";
    case GOTHICOS_OP_IRQ_REGISTER: return "IRQ_REGISTER";
    case GOTHICOS_OP_IRQ_UNREGISTER: return "IRQ_UNREGISTER";
    case GOTHICOS_OP_IRQ_RAISE: return "IRQ_RAISE";
    case GOTHICOS_OP_IRQ_SERVICE: return "IRQ_SERVICE";
    case GOTHICOS_OP_DRIVER_INFO: return "DRIVER_INFO";
    case GOTHICOS_OP_DRIVER_IO: return "DRIVER_IO";
    case GOTHICOS_OP_CAP_GRANT: return "CAP_GRANT";
    case GOTHICOS_OP_CAP_REVOKE: return "CAP_REVOKE";
    case GOTHICOS_OP_CAP_INFO: return "CAP_INFO";
    case GOTHICOS_OP_BOOT_INFO: return "BOOT_INFO";
    case GOTHICOS_OP_ABILITY_REGISTER: return "ABILITY_REGISTER";
    case GOTHICOS_OP_ABILITY_START: return "ABILITY_START";
    case GOTHICOS_OP_ABILITY_STOP: return "ABILITY_STOP";
    case GOTHICOS_OP_ABILITY_STATE: return "ABILITY_STATE";
    case GOTHICOS_OP_ABILITY_INFO: return "ABILITY_INFO";
    case GOTHICOS_OP_SOFTBUS_SEND: return "SOFTBUS_SEND";
    case GOTHICOS_OP_SOFTBUS_SUBSCRIBE: return "SOFTBUS_SUBSCRIBE";
    case GOTHICOS_OP_SOFTBUS_INFO: return "SOFTBUS_INFO";
    case GOTHICOS_OP_REMOTE_CALL: return "REMOTE_CALL";
    case GOTHICOS_OP_REMOTE_ROUTE: return "REMOTE_ROUTE";
    case GOTHICOS_OP_NODE_REGISTER: return "NODE_REGISTER";
    case GOTHICOS_OP_NODE_INFO: return "NODE_INFO";
    case GOTHICOS_OP_SCHEDULE: return "SCHEDULE";
    case GOTHICOS_OP_DISTRIBUTED_INFO: return "DISTRIBUTED_INFO";
    default: return "UNKNOWN";
    }
}

unsigned nk_abi_version(void)
{
    return (unsigned)GOTHICOS_ABI_VERSION;
}
