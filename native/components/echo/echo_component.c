/* Example native component: echoes messages between two ABI ports.
 *
 * It is deliberately MicroPython-free. The host only supplies a transport and
 * pumps step(); all state lives in this translation unit and the kernel's
 * bounded port queues.
 */
#include "gothicos/native_component.h"

#include <stdio.h>
#include <string.h>

#define ECHO_INPUT "native.echo.in"
#define ECHO_OUTPUT "native.echo.out"
#define ECHO_CAPACITY 8

typedef struct {
    int started;
} echo_state_t;

static echo_state_t echo_state;

static int echo_init(void *context)
{
    echo_state_t *state = (echo_state_t *)context;
    int status;

    status = gothicos_native_port_open(ECHO_INPUT, ECHO_CAPACITY);
    if (status != GOTHICOS_STATUS_OK && status != GOTHICOS_STATUS_ALREADY_EXISTS) {
        return status;
    }
    status = gothicos_native_port_open(ECHO_OUTPUT, ECHO_CAPACITY);
    if (status != GOTHICOS_STATUS_OK && status != GOTHICOS_STATUS_ALREADY_EXISTS) {
        return status;
    }
    state->started = 1;
    return GOTHICOS_STATUS_OK;
}

static int echo_step(void *context)
{
    echo_state_t *state = (echo_state_t *)context;
    char message[128];
    char reply[160];
    int status;

    if (!state->started) {
        return GOTHICOS_STATUS_INTERNAL_ERROR;
    }
    /* -1 is the kernel holder id: safe for a non-task native driver, and it
     * keeps an empty port non-blocking instead of parking an imaginary task. */
    status = gothicos_native_port_receive(ECHO_INPUT, -1, message, sizeof(message));
    if (status == GOTHICOS_STATUS_WOULD_BLOCK) {
        return GOTHICOS_STATUS_OK;
    }
    if (status != GOTHICOS_STATUS_OK) {
        return status;
    }

    (void)snprintf(reply, sizeof(reply), "echo:%s", message);
    return gothicos_native_port_send(ECHO_OUTPUT, reply);
}

static void echo_shutdown(void *context)
{
    echo_state_t *state = (echo_state_t *)context;

    if (!state->started) {
        return;
    }
    (void)gothicos_native_port_close(ECHO_INPUT);
    (void)gothicos_native_port_close(ECHO_OUTPUT);
    state->started = 0;
}

static const gothicos_native_component_t echo_component = {
    "native.echo",
    echo_init,
    echo_step,
    echo_shutdown,
    &echo_state,
};

const gothicos_native_component_t *gothicos_echo_component(void)
{
    return &echo_component;
}
