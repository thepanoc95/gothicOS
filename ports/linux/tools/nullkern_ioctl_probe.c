/* Minimal userspace probe for the nullkern LKM char device.
 *
 * Build with `make -C ports/linux probe`, then run inside the UML guest as:
 *   ./nullkern_ioctl_probe [/dev/nullkern]
 *
 * It issues GET_INFO, RAISE_IRQ, PORT_SEND, SCHED_HINT, then GET_INFO again so
 * the counter changes prove the ioctls reached the kernel module.
 */
#include "gothicos_linux_port.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void print_info(const gothicos_lkm_info_t *info, const char *label)
{
    printf("%s: abi=%u.%u.%u caps=0x%02x irq=%u msg=%u hints=%u nodes=%u\n",
           label,
           info->abi_major, info->abi_minor, info->abi_patch,
           info->capabilities,
           info->irq_count, info->message_count,
           info->schedule_hints, info->active_nodes);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/nullkern";
    gothicos_lkm_request_t request;
    gothicos_lkm_info_t info;
    int descriptor;
    int result = 1;

    descriptor = open(path, O_RDWR);
    if (descriptor < 0) {
        perror(path);
        return 1;
    }

    memset(&info, 0, sizeof(info));
    if (ioctl(descriptor, GOTHICOS_LKM_IOC_GET_INFO, &info) < 0) {
        perror("GET_INFO");
        goto done;
    }
    print_info(&info, "before");

    memset(&request, 0, sizeof(request));
    request.opcode = GOTHICOS_LKM_OP_RAISE_IRQ;
    request.arg0 = 1; /* GOTHICOS_IRQ_TIMER */
    if (ioctl(descriptor, GOTHICOS_LKM_IOC_RAISE_IRQ, &request) < 0) {
        perror("RAISE_IRQ");
        goto done;
    }

    memset(&request, 0, sizeof(request));
    request.opcode = GOTHICOS_LKM_OP_PORT_SEND;
    request.arg0 = 1; /* example port id */
    if (ioctl(descriptor, GOTHICOS_LKM_IOC_PORT_SEND, &request) < 0) {
        perror("PORT_SEND");
        goto done;
    }

    memset(&request, 0, sizeof(request));
    request.opcode = GOTHICOS_LKM_OP_SCHED_HINT;
    if (ioctl(descriptor, GOTHICOS_LKM_IOC_SCHED_HINT, &request) < 0) {
        perror("SCHED_HINT");
        goto done;
    }

    memset(&info, 0, sizeof(info));
    if (ioctl(descriptor, GOTHICOS_LKM_IOC_GET_INFO, &info) < 0) {
        perror("GET_INFO (after)");
        goto done;
    }
    print_info(&info, "after");

    if (info.irq_count != 1 || info.message_count != 1 || info.schedule_hints != 1) {
        fprintf(stderr, "probe failed: ioctl counters did not advance as expected\n");
        goto done;
    }
    puts("nullkern LKM probe OK");
    result = 0;

done:
    close(descriptor);
    return result;
}
