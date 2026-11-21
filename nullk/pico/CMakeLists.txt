cmake_minimum_required(VERSION 3.12)

# Pico SDK port of null_kernel. The Pico SDK is a build/runtime dependency for
# this board only; the default lm3s6965evb QEMU build stays freestanding.
include(pico_sdk_import.cmake)

project(nullk_pico C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 0 CACHE STRING
    "Do not block waiting for a terminal to attach")

pico_sdk_init()

set(NULLK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(null_kernel_pico
    sdk_main.c
    arch.c

    ${NULLK_ROOT}/nk/_oskernel.c
    ${NULLK_ROOT}/nk/_os_runmode.c
    ${NULLK_ROOT}/nk/_os_task.c
    ${NULLK_ROOT}/nk/_os_sched.c
    ${NULLK_ROOT}/nk/_os_capabilities.c
    ${NULLK_ROOT}/nk/_os_memory.c
    ${NULLK_ROOT}/nk/_os_ipc.c
    ${NULLK_ROOT}/nk/_os_irq.c
    ${NULLK_ROOT}/nk/_os_syscalls.c
    ${NULLK_ROOT}/nk/hdf/HardwareDriverEntry.c
    ${NULLK_ROOT}/lib/libnk/libnk.c
    ${NULLK_ROOT}/apps/init/app.c
    ${NULLK_ROOT}/shared/interfacekit_gfx.c
    ${NULLK_ROOT}/shared/interfacekit_widgets.c

    context_sdk.S
)

target_include_directories(null_kernel_pico PRIVATE
    ${NULLK_ROOT}/nk
    ${NULLK_ROOT}/include
)

# Pico W uses the reduced InterfaceKit widget set: common glass controls only.
target_compile_definitions(null_kernel_pico PRIVATE INTERFACEKIT_EMBEDDED=1)

# _os_compiler_rt.c is intentionally excluded: the SDK links newlib, which
# already provides memcpy/memmove/memset/memcmp.
target_link_libraries(null_kernel_pico pico_stdlib)

# Console is CDC ACM over the micro-USB port; no GPIO UART.
pico_enable_stdio_usb(null_kernel_pico 1)
pico_enable_stdio_uart(null_kernel_pico 0)

pico_add_extra_outputs(null_kernel_pico)
