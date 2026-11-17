/* Umbrella header for the gothicOS SDK. */
#ifndef GOTHICOS_SDK_H
#define GOTHICOS_SDK_H

#include "kernel_abi.h"

#define GOTHICOS_SDK_MAJOR 1
#define GOTHICOS_SDK_MINOR 1
#define GOTHICOS_SDK_PATCH 0

/* Fixed sizes of the nullkernRT v1 runtime. */
#define GOTHICOS_SDK_MAX_TASKS 8
#define GOTHICOS_SDK_MAILBOX_DEPTH 4
#define GOTHICOS_SDK_MAX_CORES 2

#ifdef __cplusplus
extern "C" {
#endif

int gothicos_sdk_abi_supported(int major, int minor);
int gothicos_sdk_init(gothicos_invoke_fn transport);

#ifdef __cplusplus
}
#endif

#endif /* GOTHICOS_SDK_H */
