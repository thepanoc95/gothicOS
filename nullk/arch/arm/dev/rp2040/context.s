/* Exception entry/exit and preemptive context switch for RP2040 (M0+).
 * ARMv6-M cannot encode cbz, high-register TST/MVN, or multi-register LDM/STM
 * with writeback. r4-r11 are therefore moved through explicit word accesses,
 * preserving the same saved-frame layout as the Cortex-M3 backend. */
.syntax unified
.cpu cortex-m0plus
.thumb

.extern nkk_svc_handler_c
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
    /* Use MSP if nested, PSP for Thread-mode syscalls. High registers cannot
     * be used with TST in Thumb-1, so copy LR into a low register first. */
    mov r1, lr
    movs r0, #4
    ands r0, r1
    cmp r0, #0
    beq .Lsvc_msp
    mrs r0, psp
    ldr r3, =nkk_svc_handler_c  /* Thumb-1 branches are only +/-2 KiB */
    bx r3
.Lsvc_msp:
    mrs r0, msp
    ldr r3, =nkk_svc_handler_c
    bx r3
.ltorg
.size SVC_Handler, . - SVC_Handler

.section .text.PendSV_Handler, "ax", %progbits
.align 2
PendSV_Handler:
    /* Save r4-r11 of the outgoing task; hardware already stacked r0-r3. */
    ldr r1, =nkk_current
    ldr r2, [r1]
    cmp r2, #0
    beq .Lskip_save
    mrs r0, psp

    /* Keep the original r4-r7 in a temporary MSP frame while r8-r11 are
     * copied through r4-r7 for the first block of stores. */
    push {r4-r7}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    subs r0, r0, #16
    str r4, [r0, #0]
    str r5, [r0, #4]
    str r6, [r0, #8]
    str r7, [r0, #12]
    pop {r4-r7}
    subs r0, r0, #16
    str r4, [r0, #0]
    str r5, [r0, #4]
    str r6, [r0, #8]
    str r7, [r0, #12]
    str r0, [r2]                /* TCB.sp is the first field */
.Lskip_save:
    /* LR is callee-saved by the C ABI; keep it in r8 across the call. */
    mov r8, lr
    bl nkk_sched_pick           /* returns next saved SP (r4 slot) in r0 */
    mov lr, r8
    mov r3, r0

    /* Restore in the reverse order, again using MSP as a scratch area. */
    ldr r4, [r3, #0]
    ldr r5, [r3, #4]
    ldr r6, [r3, #8]
    ldr r7, [r3, #12]
    adds r3, r3, #16
    push {r4-r7}
    ldr r4, [r3, #0]
    ldr r5, [r3, #4]
    ldr r6, [r3, #8]
    ldr r7, [r3, #12]
    adds r3, r3, #16
    mov r8, r4
    mov r9, r5
    mov r10, r6
    mov r11, r7
    pop {r4-r7}

    msr psp, r3                 /* now points at the hardware exception frame */
    movs r0, #2
    mvns r0, r0                 /* EXC_RETURN 0xFFFFFFFD: Thread mode, PSP */
    mov lr, r0
    bx lr
.ltorg
.size PendSV_Handler, . - PendSV_Handler

.section .text.SysTick_Handler, "ax", %progbits
.align 2
SysTick_Handler:
    mov r4, lr
    push {r4}
    bl nkk_sched_on_tick
    pop {r4}
    mov lr, r4
    bx lr
.size SysTick_Handler, . - SysTick_Handler

/* Faults halt here instead of aliasing to reset, so a failed boot is visible. */
.section .text.HardFault_Handler, "ax", %progbits
.align 2
HardFault_Handler:
    b .
.size HardFault_Handler, . - HardFault_Handler
