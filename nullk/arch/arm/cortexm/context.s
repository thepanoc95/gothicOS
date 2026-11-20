/* Exception entry/exit and preemptive context switch for Cortex-M. */
.syntax unified
.cpu cortex-m3
.thumb

.extern nkk_svc_handler_c
.extern nkk_sched_on_tick
.extern nkk_sched_pick

.global SVC_Handler
.global PendSV_Handler
.global SysTick_Handler
.global HardFault_Handler
.type SVC_Handler, %function
.type PendSV_Handler, %function
.type SysTick_Handler, %function
.type HardFault_Handler, %function

.section .text.SVC_Handler, "ax", %progbits
.align 2
SVC_Handler:
    /* r0 = stacked SP (PSP for Thread mode, MSP if called from kernel). */
    tst lr, #4
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    b nkk_svc_handler_c
.size SVC_Handler, . - SVC_Handler

.section .text.PendSV_Handler, "ax", %progbits
.align 2
PendSV_Handler:
    /* Save r4-r11 of the outgoing task; hardware already stacked r0-r3. */
    ldr r1, =nkk_current
    ldr r2, [r1]
    cbz r2, .Lskip_save
    mrs r0, psp
    stmdb r0!, {r4-r11}
    str r0, [r2]                /* TCB.sp is the first field */
.Lskip_save:
    push {lr}
    bl nkk_sched_pick           /* returns next saved SP (r4 slot) in r0 */
    pop {lr}
    ldmia r0!, {r4-r11}         /* restore incoming task callee-saved regs */
    msr psp, r0                 /* now points at the hardware exception frame */
    mvn lr, #2                  /* EXC_RETURN: Thread mode, PSP (0xFFFFFFFD) */
    bx lr
.size PendSV_Handler, . - PendSV_Handler

.section .text.SysTick_Handler, "ax", %progbits
.align 2
SysTick_Handler:
    push {lr}
    bl nkk_sched_on_tick
    pop {lr}
    bx lr
.size SysTick_Handler, . - SysTick_Handler

/* Faults halt here instead of aliasing to reset, so a failed boot is visible. */
.section .text.HardFault_Handler, "ax", %progbits
.align 2
HardFault_Handler:
    b .
.size HardFault_Handler, . - HardFault_Handler
