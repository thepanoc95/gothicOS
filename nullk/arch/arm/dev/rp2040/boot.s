/* null_kernel baremetal boot - Raspberry Pi Pico W (RP2040, Cortex-M0+) */
.syntax unified
.cpu cortex-m0plus
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
.extern USB_IRQHandler
.extern __sidata
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss
.extern __stack_top__

.section .isr_vector, "a", %progbits
.align 8
vect_table:
    .word __stack_top__          /* 0: initial MSP */
    .word Reset_Handler          /* 1: reset */
    .word HardFault_Handler      /* 2: NMI */
    .word HardFault_Handler      /* 3: HardFault */
    .word HardFault_Handler      /* 4: MemManage (reserved on M0+) */
    .word HardFault_Handler      /* 5: BusFault (reserved on M0+) */
    .word HardFault_Handler      /* 6: UsageFault (reserved on M0+) */
    .word 0                      /* 7: reserved */
    .word 0                      /* 8: reserved */
    .word 0                      /* 9: reserved */
    .word 0                      /* 10: reserved */
    .word SVC_Handler            /* 11: SVCall */
    .word 0                      /* 12: DebugMonitor */
    .word 0                      /* 13: reserved */
    .word PendSV_Handler         /* 14: PendSV */
    .word SysTick_Handler        /* 15: SysTick */

    /* External IRQs: 5 is USBCTRL, used by the CDC console. */
    .rept 5
    .word Default_Handler
    .endr
    .word USB_IRQHandler
    .rept 26
    .word Default_Handler
    .endr

.section .text.Reset_Handler, "ax", %progbits
.align 2
Reset_Handler:
    /* Relocate vector table in case we were loaded by a debugger. */
    ldr r0, =vect_table
    ldr r1, =0xe000ed08          /* VTOR */
    str r0, [r1]

    /* Copy .data initializers from flash to RAM. */
    ldr r0, =__sidata
    ldr r1, =_sdata
    ldr r2, =_edata
    b 2f
1:
    ldr r3, [r0]
    adds r0, r0, #4
    str r3, [r1]
    adds r1, r1, #4
2:
    cmp r1, r2
    blo 1b

    /* Zero .bss. */
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
    b 4f
3:
    str r3, [r1]
    adds r1, r1, #4
4:
    cmp r1, r2
    blo 3b

    ldr r0, =__stack_top__
    msr msp, r0
    bl nkk_boot
    b .
.size Reset_Handler, . - Reset_Handler

/* Spurious external IRQ: halt instead of aliasing to reset. */
.section .text.Default_Handler, "ax", %progbits
.align 2
Default_Handler:
    b .
.size Default_Handler, . - Default_Handler
