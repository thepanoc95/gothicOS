/*
 * IDT, GDT/TSS, 8259 PIC and PIT for the AMD64 port.
 *
 * Assembly stubs exported by context.S (names are part of the ABI between the
 * two files):
 *   nk64_exc_0 .. nk64_exc_31  - CPU exception trampolines. Vectors without an
 *                                error code push a zero first; all of them end
 *                                up in nk64_exception_dispatch -> handler.
 *   nk64_irq_0                 - PIT tick, calls nk64_timer_handler().
 *   nk64_irq_8                 - spurious/unused slave IRQ, EOI only.
 *
 * The GDT uses selectors 0x08 (kernel code), 0x10 (kernel data), 0x23 (user
 * code), 0x2b (user data) and 0x30 (64-bit TSS). nk64_tss_set_rsp0() lets the
 * task layer point ring-3 interrupts at the current kernel stack.
 */

#include <stdint.h>

/* kernel64.h contains a nested-comment typo in a libc-free header we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop

#define NK64_IDT_ENTRIES 256u

#define NK64_GDT_KERNEL_CODE 1u
#define NK64_GDT_KERNEL_DATA 2u
#define NK64_GDT_USER_CODE 4u
#define NK64_GDT_USER_DATA 5u
#define NK64_GDT_TSS 6u

#define NK64_PIT_BASE 0x40u
#define NK64_PIT_CTRL 0x43u
#define NK64_PIT_HZ 1193182u

#define NK64_PIC1_CMD 0x20u
#define NK64_PIC1_DATA 0x21u
#define NK64_PIC2_CMD 0xa0u
#define NK64_PIC2_DATA 0xa1u

#define NK64_PIC_EOI 0x20u

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} nk64_idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} nk64_idtr_t;

typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit;
    uint8_t base_high;
} nk64_gdt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} nk64_gdtr_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t ist[7];
    uint64_t reserved1;
    uint16_t reserved2;
    uint16_t iomap_base;
} nk64_tss_t;

extern nk64_bootinfo_t nk64_boot;
extern volatile uint32_t nk64_ticks;
extern void nk64_exception_handler(uint64_t vector, uint64_t error);
extern void nk64_spurious_handler(void);

#define NK64_EXCEPTION_LIST(X) \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

#define NK64_DECLARE_EXC(n) extern void nk64_exc_##n(void);
NK64_EXCEPTION_LIST(NK64_DECLARE_EXC)
#undef NK64_DECLARE_EXC

extern void nk64_irq_0(void);
extern void nk64_irq_1(void);
extern void nk64_irq_8(void);
extern void nk64_irq_12(void);

static nk64_idt_entry_t nk64_idt[NK64_IDT_ENTRIES] __attribute__((aligned(16)));
static nk64_gdt_entry_t nk64_gdt[8] __attribute__((aligned(16)));
static nk64_tss_t nk64_tss __attribute__((aligned(16)));
static uint8_t nk64_exception_stack[16384] __attribute__((aligned(16)));
static uint8_t nk64_pic1_mask = 0xffu;
static uint8_t nk64_pic2_mask = 0xffu;

static const char *const nk64_exception_names[NK64_IDT_ENTRIES / 8u] = {
    "divide by zero",
    "debug exception",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack-segment fault",
    "general protection fault",
    "page fault",
    "reserved exception",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualisation exception",
    "control protection exception",
    "reserved exception 22",
    "reserved exception 23",
    "reserved exception 24",
    "reserved exception 25",
    "reserved exception 26",
    "reserved exception 27",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved exception 31"
};

static void nk64_idt_set(uint32_t vector, uint64_t handler)
{
    nk64_idt_entry_t *e = &nk64_idt[vector];

    e->offset_low = (uint16_t)handler;
    e->selector = 0x08u;
    e->ist = 0u; /* ring 3 uses TSS.RSP0, ring 0 keeps its current stack */
    e->type_attr = 0x8eu; /* present, DPL 0, 64-bit interrupt gate */
    e->offset_mid = (uint16_t)(handler >> 16);
    e->offset_high = (uint32_t)(handler >> 32);
    e->reserved = 0u;
}

static void nk64_gdt_set(uint32_t index, uint64_t base, uint32_t limit,
                         uint8_t access, uint8_t flags)
{
    nk64_gdt_entry_t *e = &nk64_gdt[index];

    e->limit_low = (uint16_t)limit;
    e->base_low = (uint16_t)base;
    e->base_mid = (uint8_t)(base >> 16);
    e->access = access;
    e->flags_limit = (uint8_t)(flags | (uint8_t)((limit >> 16) & 0x0fu));
    e->base_high = (uint8_t)(base >> 24);
}

void nk64_tss_set_rsp0(uint64_t rsp0)
{
    nk64_tss.rsp0 = rsp0;
}

void nk64_spurious_handler(void)
{
    /* Nothing to do: the stub sends the EOI. */
}

void nk64_exception_handler(uint64_t vector, uint64_t error)
{
    (void)error;
    if (vector < (NK64_IDT_ENTRIES / 8u)) {
        nk64_panic(nk64_exception_names[vector]);
    }
    nk64_panic("unknown CPU exception");
}

static void nk64_gdt_init(void)
{
    uint64_t tss_base = (uint64_t)(uintptr_t)&nk64_tss;
    nk64_gdtr_t gdtr;

    nk64_gdt_set(NK64_GDT_KERNEL_CODE, 0u, 0xfffffu, 0x9au, 0xa0u);
    nk64_gdt_set(NK64_GDT_KERNEL_DATA, 0u, 0xfffffu, 0x92u, 0xa0u);
    /* SYSRET computes SS = STAR[63:48] + 8 = 0x1b while IRETQ uses 0x2b.
     * Keep a duplicate user-data descriptor at index 3 so both paths are
     * valid; user code stays at index 4 (0x23). */
    nk64_gdt_set(3u, 0u, 0xfffffu, 0xf2u, 0xa0u);
    nk64_gdt_set(NK64_GDT_USER_CODE, 0u, 0xfffffu, 0xfau, 0xa0u);
    nk64_gdt_set(NK64_GDT_USER_DATA, 0u, 0xfffffu, 0xf2u, 0xa0u);

    /* Long-mode TSS descriptor spans two GDT slots. */
    nk64_gdt_set(NK64_GDT_TSS, tss_base, (uint32_t)sizeof(nk64_tss_t) - 1u,
                 0x89u, 0x00u);
    nk64_gdt[7].limit_low = (uint16_t)(tss_base >> 32);
    nk64_gdt[7].base_low = (uint16_t)(tss_base >> 48);
    nk64_gdt[7].base_mid = 0u;
    nk64_gdt[7].access = 0u;
    nk64_gdt[7].flags_limit = 0u;
    nk64_gdt[7].base_high = 0u;

    nk64_tss.reserved0 = 0u;
    nk64_tss.rsp1 = 0u;
    nk64_tss.rsp2 = 0u;
    nk64_tss.rsp0 = (uint64_t)(uintptr_t)
        (nk64_exception_stack + sizeof(nk64_exception_stack));
    for (uint32_t i = 0; i < 7u; i++) {
        nk64_tss.ist[i] = 0u;
    }
    nk64_tss.reserved1 = 0u;
    nk64_tss.reserved2 = 0u;
    nk64_tss.iomap_base = (uint16_t)sizeof(nk64_tss_t);

    gdtr.limit = (uint16_t)(sizeof(nk64_gdt) - 1u);
    gdtr.base = (uint64_t)(uintptr_t)nk64_gdt;
    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");

    /* A direct far jmp is not encodable in long mode: use a far return. */
    __asm__ volatile(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        :
        :
        : "eax", "memory");

    __asm__ volatile("movw $0x30, %%ax\n\tltr %%ax" : : : "eax");
}

void nk64_idt_init(void)
{
    nk64_idtr_t idtr;

    nk64_gdt_init();

#define NK64_SET_EXC(n) \
    nk64_idt_set(n, (uint64_t)(uintptr_t)nk64_exc_##n);
    NK64_EXCEPTION_LIST(NK64_SET_EXC)
#undef NK64_SET_EXC

    nk64_idt_set(0x20u, (uint64_t)(uintptr_t)nk64_irq_0);
    nk64_idt_set(0x21u, (uint64_t)(uintptr_t)nk64_irq_1);
    nk64_idt_set(0x28u, (uint64_t)(uintptr_t)nk64_irq_8);
    nk64_idt_set(0x2cu, (uint64_t)(uintptr_t)nk64_irq_12);

    idtr.limit = (uint16_t)(sizeof(nk64_idt) - 1u);
    idtr.base = (uint64_t)(uintptr_t)nk64_idt;
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

void nk64_pic_init(void)
{
    /* Master PIC: 0x20-0x27, slave PIC: 0x28-0x2f, 8086 mode. */
    nk64_outb(NK64_PIC1_CMD, 0x11u);
    nk64_outb(NK64_PIC2_CMD, 0x11u);
    nk64_outb(NK64_PIC1_DATA, 0x20u);
    nk64_outb(NK64_PIC2_DATA, 0x28u);
    nk64_outb(NK64_PIC1_DATA, 0x04u); /* slave on IR2 */
    nk64_outb(NK64_PIC2_DATA, 0x02u); /* cascade identity */
    nk64_outb(NK64_PIC1_DATA, 0x01u);
    nk64_outb(NK64_PIC2_DATA, 0x01u);

    nk64_pic1_mask = 0xffu;
    nk64_pic2_mask = 0xffu;
    nk64_outb(NK64_PIC1_DATA, nk64_pic1_mask);
    nk64_outb(NK64_PIC2_DATA, nk64_pic2_mask);
}

void nk64_pic_unmask(uint8_t irq)
{
    if (irq >= 16u) {
        return;
    }
    if (irq < 8u) {
        nk64_pic1_mask &= (uint8_t)~(1u << irq);
        nk64_outb(NK64_PIC1_DATA, nk64_pic1_mask);
    } else {
        nk64_pic2_mask &= (uint8_t)~(1u << (irq - 8u));
        nk64_outb(NK64_PIC2_DATA, nk64_pic2_mask);
        /* PIC2 interrupts also need the cascade line (master IRQ2) open. */
        nk64_pic1_mask &= (uint8_t)~(1u << 2u);
        nk64_outb(NK64_PIC1_DATA, nk64_pic1_mask);
    }
}

void nk64_timer_init(uint32_t hz)
{
    uint32_t divisor;

    if (hz == 0u) {
        hz = 100u;
    }
    divisor = NK64_PIT_HZ / hz;
    if (divisor > 65535u) {
        divisor = 65535u;
    }
    if (divisor == 0u) {
        divisor = 1u;
    }

    nk64_outb(NK64_PIT_CTRL, 0x36u); /* channel 0, lobyte/hibyte, mode 3 */
    nk64_outb(NK64_PIT_BASE, (uint8_t)(divisor & 0xffu));
    nk64_outb(NK64_PIT_BASE, (uint8_t)(divisor >> 8));

    nk64_pic1_mask &= (uint8_t)~(1u << 0); /* unmask PIT (IRQ 0) */
    nk64_outb(NK64_PIC1_DATA, nk64_pic1_mask);
}

void nk64_irq_enable(void)
{
    __asm__ volatile("sti");
}

void nk64_irq_disable(void)
{
    __asm__ volatile("cli");
}

void nk64_timer_handler(void)
{
    nk64_ticks++;
    nk64_sched_tick();
}
