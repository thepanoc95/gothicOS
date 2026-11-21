/*
 * AMD64 low-level assembly: exception/IRQ trampolines, syscall fast path,
 * ring-3 entry and context switching.
 *
 * Exported symbols (consumed by idt.c and the task layer):
 *   nk64_exc_0 .. nk64_exc_31 : exception stubs (errors: 8,10-14,17,21,29,30)
 *   nk64_irq_0                 : PIT  -> nk64_timer_handler()
 *   nk64_irq_1                 : PS/2 keyboard -> nk64_keyboard_handler()
 *   nk64_irq_8                 : spurious -> nk64_spurious_handler()
 *   nk64_irq_12                : PS/2 mouse -> nk64_mouse_handler()
 *   nk64_syscall_entry         : SYSCALL entry, +1 argument in r10
 *   nk64_usermode_jump(rsp,rip,cr3)
 *   nk64_context_switch(uint64_t *old_rsp, uint64_t new_rsp)
 *
 * Assumptions: one CPU (single static syscall stack, no swapgs), Limine's GDT
 * is replaced by the one in idt.c, and FS/GS are not used by ring 3. The
 * syscall path clears IF via IA32_FMASK, so nested syscalls/IRQs are excluded.
 * Context switch expects the incoming stack to hold a return address followed
 * by r15,r14,r13,r12,rbp,rbx (the layout emitted below).
 */

    .code64
    .section .text

/* ---------------------------------------------------------------- exceptions */

.macro NK64_EXC_NOERR num
    .globl nk64_exc_\num
    .type  nk64_exc_\num, @function
nk64_exc_\num:
    pushq $0
    pushq $\num
    jmp nk64_exception_dispatch
    .size nk64_exc_\num, . - nk64_exc_\num
.endm

.macro NK64_EXC_ERR num
    .globl nk64_exc_\num
    .type  nk64_exc_\num, @function
nk64_exc_\num:
    /* The CPU has already pushed the error code. */
    pushq $\num
    jmp nk64_exception_dispatch
    .size nk64_exc_\num, . - nk64_exc_\num
.endm

NK64_EXC_NOERR 0
NK64_EXC_NOERR 1
NK64_EXC_NOERR 2
NK64_EXC_NOERR 3
NK64_EXC_NOERR 4
NK64_EXC_NOERR 5
NK64_EXC_NOERR 6
NK64_EXC_NOERR 7
NK64_EXC_ERR  8
NK64_EXC_NOERR 9
NK64_EXC_ERR  10
NK64_EXC_ERR  11
NK64_EXC_ERR  12
NK64_EXC_ERR  13
NK64_EXC_ERR  14
NK64_EXC_NOERR 15
NK64_EXC_NOERR 16
NK64_EXC_ERR  17
NK64_EXC_NOERR 18
NK64_EXC_NOERR 19
NK64_EXC_NOERR 20
NK64_EXC_ERR  21
NK64_EXC_NOERR 22
NK64_EXC_NOERR 23
NK64_EXC_NOERR 24
NK64_EXC_NOERR 25
NK64_EXC_NOERR 26
NK64_EXC_NOERR 27
NK64_EXC_NOERR 28
NK64_EXC_ERR  29
NK64_EXC_ERR  30
NK64_EXC_NOERR 31

/* Entered with: [vector][error] on the stack. */
    .type nk64_exception_dispatch, @function
nk64_exception_dispatch:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbp
    pushq %rbx
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rax
    /* rax+0, rcx+8, rdx+16, rsi+24, rdi+32, ..., vector+120, error+128 */
    movq 120(%rsp), %rdi
    movq 128(%rsp), %rsi
    movq %rsp, %r12
    andq $-16, %rsp
    cld
    call nk64_exception_handler
    movq %r12, %rsp
    popq %rax
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %rbx
    popq %rbp
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    addq $16, %rsp
    iretq
    .size nk64_exception_dispatch, . - nk64_exception_dispatch

/* ------------------------------------------------------------------ IRQs */

.macro NK64_IRQ_STUB num, handler, slave
    .globl nk64_irq_\num
    .type  nk64_irq_\num, @function
nk64_irq_\num:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    cld
    call \handler
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    movb $0x20, %al
    outb %al, $0x20
.if \slave
    outb %al, $0xa0
.endif
    popq %rax
    iretq
    .size nk64_irq_\num, . - nk64_irq_\num
.endm

NK64_IRQ_STUB 0, nk64_timer_handler, 0
NK64_IRQ_STUB 1, nk64_keyboard_handler, 0
NK64_IRQ_STUB 8, nk64_spurious_handler, 1
NK64_IRQ_STUB 12, nk64_mouse_handler, 1

/* ----------------------------------------------------------------- syscalls */

    .globl nk64_syscall_entry
    .type  nk64_syscall_entry, @function
nk64_syscall_entry:
    /* On entry: rax=nr, args=rdi,rsi,rdx,r10,r8,r9; rcx/r11 hold user RIP/FLAGS. */
    movq %rsp, nk64_syscall_user_rsp(%rip)
    /* Each task owns its privileged SYSCALL stack; the task layer updates
     * this global whenever it switches contexts. */
    movq nk64_current_kstack_top(%rip), %rsp

    pushq %r11                  /* user rflags */
    pushq %rcx                  /* user rip */
    pushq %rax                  /* syscall number */
    pushq %rdi                  /* a0 */
    pushq %rsi                  /* a1 */
    pushq %rdx                  /* a2 */
    pushq %r10                  /* a3 (SysV wants this in rcx) */
    pushq %r8                   /* a4 */
    pushq %r9                   /* a5 */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Seventh SysV argument goes on the stack. */
    movq 48(%rsp), %rax
    pushq %rax

    /* After that push: a5+0, r15+8, ..., number+104, rip+112, flags+120. */
    movq 104(%rsp), %rdi        /* n */
    movq 96(%rsp), %rsi         /* a0 */
    movq 88(%rsp), %rdx         /* a1 */
    movq 80(%rsp), %rcx         /* a2 */
    movq 72(%rsp), %r8          /* a3 */
    movq 64(%rsp), %r9          /* a4 */
    cld
    call nk64_dispatch_syscall

    /* Deferred preemption: PIT ticks only set a flag. A stack switch here is
     * safe because we are on the task's own SYSCALL stack. Preserve rax. */
    pushq %rax
    call nk64_yield_if_needed
    popq %rax

    addq $8, %rsp               /* drop the seventh argument */
    movq %rax, 96(%rsp)         /* return value -> saved rax */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    popq %rcx                   /* sysret RIP */
    popq %r11                   /* sysret RFLAGS */
    movq nk64_syscall_user_rsp(%rip), %rsp
    sysretq
    .size nk64_syscall_entry, . - nk64_syscall_entry

/* ------------------------------------------------------------ ring 3 entry */

    .globl nk64_usermode_jump
    .type  nk64_usermode_jump, @function
nk64_usermode_jump:
    movq %rdx, %cr3             /* address space prepared by the task layer */
    andq $-16, %rsp
    subq $48, %rsp
    movq %rsi, 0(%rsp)          /* RIP */
    movq $0x23, 8(%rsp)         /* user code */
    movq $0x202, 16(%rsp)       /* RFLAGS: IF set */
    movq %rdi, 24(%rsp)         /* user RSP */
    movq $0x2b, 32(%rsp)        /* user data */
    movw $0x2b, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    iretq
    .size nk64_usermode_jump, . - nk64_usermode_jump

/* -------------------------------------------------------- context switching */

    .globl nk64_context_switch
    .type  nk64_context_switch, @function
nk64_context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)           /* *old_rsp = saved frame */
    movq %rsi, %rsp             /* load the incoming stack */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
    .size nk64_context_switch, . - nk64_context_switch

/* ------------------------------------------------------------------- state */

    .section .data
    .align 16
    .globl nk64_syscall_user_rsp
nk64_syscall_user_rsp:
    .quad 0

    .globl nk64_current_kstack_top
nk64_current_kstack_top:
    .quad 0

    .section .bss
    .align 16
nk64_syscall_stack:
    .skip 16384
    .globl nk64_syscall_stack_top
nk64_syscall_stack_top:
