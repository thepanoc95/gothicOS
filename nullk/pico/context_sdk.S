/* Reuse the RP2040 context switch with the Pico SDK's vector-table names.
 * The SDK declares isr_* weakly in crt0.S; these strong definitions override
 * them so SVC, PendSV, SysTick and faults reach the kernel handlers. */
#define SVC_Handler       isr_svcall
#define PendSV_Handler    isr_pendsv
#define SysTick_Handler   isr_systick
#define HardFault_Handler isr_hardfault

#include "../arch/arm/dev/rp2040/context.s"
