// SPDX-License-Identifier: BSD-3-Clause
/*
 * Cortex-M33 startup (no FSP, no C library).
 *
 * BL2 (TF-A, PLAT_M33_BOOT_SUPPORT) loads the raw image to BL22_BASE = 0x20000 and starts
 * the CM33 in secure state with SYS_CM33_CFG2 (INITSVTOR) = 0x20000, so the vector table
 * must be the first thing in the image.
 */
#include <stdint.h>
#include "cm33.h"
#include "lpdemo_shared.h"

extern uint32_t __bss_start, __bss_end, __stack_top;
extern int main(void);
extern void SysTick_Handler(void);

void Reset_Handler(void);
void Fault_Handler(void);
void Default_Handler(void);

__attribute__((section(".vectors"), used, aligned(128)))
const void *const g_vectors[16] = {
	&__stack_top,		/* initial MSP */
	Reset_Handler,
	Default_Handler,	/* NMI */
	Fault_Handler,		/* HardFault */
	Fault_Handler,		/* MemManage */
	Fault_Handler,		/* BusFault */
	Fault_Handler,		/* UsageFault */
	Fault_Handler,		/* SecureFault */
	0, 0, 0,
	Default_Handler,	/* SVCall */
	Default_Handler,	/* DebugMonitor */
	0,
	Default_Handler,	/* PendSV */
	SysTick_Handler,
};

void Reset_Handler(void)
{
	uint32_t *p;

	/* .data is linked in place (the image is loaded to RAM): only .bss needs clearing */
	for (p = &__bss_start; p < &__bss_end; p++)
		*p = 0;
	mmio_write_32(SCB_VTOR, (uint32_t)(uintptr_t)g_vectors);
	/* Report BusFault/MemManage/UsageFault separately instead of escalating */
	mmio_write_32(SCB_SHCSR, mmio_read_32(SCB_SHCSR) | (7u << 16));
	dsb();
	(void)main();
	for (;;)
		wfi();
}

/* Reports the fault through the control block so that the CA55 (lpdemo-ctl) can show it. */
void __attribute__((used)) fault_report(const uint32_t *frame)
{
	volatile struct lpdemo_ctrl *c = (volatile struct lpdemo_ctrl *)LPDEMO_SRAM_CTRL_BASE;

	c->fault_pc = frame[6];		/* stacked PC */
	c->fault_lr = frame[5];		/* stacked LR */
	c->fault_cfsr = mmio_read_32(SCB_CFSR);
	c->cm33_state = LPDEMO_CM33_FAULT;
	dsb();
	for (;;)
		;
}

__attribute__((naked)) void Fault_Handler(void)
{
	__asm__ volatile(
		"tst lr, #4\n"
		"ite eq\n"
		"mrseq r0, msp\n"
		"mrsne r0, psp\n"
		"b fault_report\n");
}

void Default_Handler(void)
{
	for (;;)
		;
}
