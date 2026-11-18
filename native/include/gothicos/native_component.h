/* Native component API for nullkernRT.
 *
 * A native component is a plain C ABI object. It never imports MicroPython:
 * all kernel access goes through a gothicos_invoke_fn transport supplied by
 * the host (CPython/Cython on Linux, firmware on RP2040).
 */
#ifndef GOTHICOS_NATIVE_COMPONENT_H
#define GOTHICOS_NATIVE_COMPONENT_H

#include <gothicos/kernel_abi.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define GOTHICOS_NATIVE_EXPORT __declspec(dllexport)
#else
#define GOTHICOS_NATIVE_EXPORT __attribute__((visibility("default")))
#endif

typedef struct {
    const char *name;
    int (*init)(void *context);
    int (*step)(void *context);
    void (*shutdown)(void *context);
    void *context;
} gothicos_native_component_t;

/* The component library owns a single transport pointer. Hosts install it
 * once with gothicos_sdk_init() or gothicos_native_set_transport(). */
void gothicos_native_set_transport(gothicos_invoke_fn transport);
gothicos_invoke_fn gothicos_native_transport(void);

/* Component lifecycle. A step is bounded and cooperative: it should handle at
 * most one message/event and return a status from kernel_abi.h (0 means OK). */
int gothicos_native_component_start(const gothicos_native_component_t *component);
int gothicos_native_component_step(const gothicos_native_component_t *component);
void gothicos_native_component_stop(const gothicos_native_component_t *component);
int gothicos_native_component_run(const gothicos_native_component_t *component, unsigned int steps);

/* Typed ABI helpers for components. They use the installed transport and copy
 * string results into caller-owned buffers. */
int gothicos_native_call(int opcode, const gothicos_arguments_t *arguments, gothicos_result_t *result);
int gothicos_native_port_open(const char *name, int capacity);
int gothicos_native_port_close(const char *name);
int gothicos_native_port_send(const char *name, const char *message);
int gothicos_native_port_receive(const char *name, int task_id, char *buffer, unsigned int length);

/* Example component supplied with the SDK; useful as a scaffold. */
GOTHICOS_NATIVE_EXPORT const gothicos_native_component_t *gothicos_echo_component(void);

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_NATIVE_COMPONENT_H */
