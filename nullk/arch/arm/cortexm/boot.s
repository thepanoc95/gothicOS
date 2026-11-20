/* null_kernel baremetal boot - ARM Cortex-M3 (QEMU lm3s6965evb) */
.syntax unified
.cpu cortex-m3
.thumb

.global vect_table
.global Reset_Handler
.type vect_table, %object
.type Reset_Handler, %function

.extern nkk_boot
.extern SVC_Handler
.extern PendSV_Handler
.extern SysTick_Handler
.extern HardFault_Handler
.extern __sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss
.extern __stack_top__

.section .isr_vector, "a", %progbits
.align 2
vect_table:
    .word __stack_top__          /* 0: initial MSP */
    .word Reset_Handler          /* 1: reset */
    .word HardFault_Handler      /* 2: NMI */
    .word HardFault_Handler      /* 3: HardFault */
    .word HardFault_Handler      /* 4: MemManage */
    .word HardFault_Handler      /* 5: BusFault */
    .word HardFault_Handler      /* 6: UsageFault */
    .word 0                      /* 7: reserved */
    .word 0                      /* 8: reserved */
    .word 0                      /* 9: reserved */
    .word 0                      /* 10: reserved */
    .word SVC_Handler            /* 11: SVCall */
    .word 0                      /* 12: DebugMonitor */
    .word 0                      /* 13: reserved */
    .word PendSV_Handler         /* 14: PendSV */
    .word SysTick_Handler        /* 15: SysTick */

.section .text.Reset_Handler, "ax", %progbits
.align 2
Reset_Handler:
    /* Copy .data initializers from flash to RAM. */
    ldr r0, =__sidata
    ldr r1, =_sdata
    ldr r2, =_edata
    b 2f
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
2:
    cmp r1, r2
    blo 1b

    /* Zero .bss. */
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
    b 4f
3:
    str r3, [r1], #4
4:
    cmp r1, r2
    blo 3b

    ldr r0, =__stack_top__
    msr msp, r0
    bl nkk_boot
    b .
.size Reset_Handler, . - Reset_Handler
