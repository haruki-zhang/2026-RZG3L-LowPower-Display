/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * CM33 core and SoC access helpers (bare metal, no FSP).
 */
#ifndef CM33_H
#define CM33_H

#include <stdint.h>

/*
 * Peripherals are given with their CA55 physical address (as in the HWM and in the
 * TF-A/Linux sources). The CM33 sees them at +0x3000_0000 (secure alias, HWM Table 1.8-2).
 * On-chip SRAM has the same address on both CPUs.
 */
#define CM33_PERI(ca55_addr)	((uintptr_t)(ca55_addr) + 0x30000000u)

static inline void mmio_write_8(uintptr_t a, uint8_t v)   { *(volatile uint8_t *)a = v; }
static inline uint8_t mmio_read_8(uintptr_t a)            { return *(volatile uint8_t *)a; }
static inline void mmio_write_32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline uint32_t mmio_read_32(uintptr_t a)          { return *(volatile uint32_t *)a; }

static inline void dmb(void) { __asm__ volatile("dmb" ::: "memory"); }
static inline void dsb(void) { __asm__ volatile("dsb" ::: "memory"); }
static inline void wfi(void) { __asm__ volatile("wfi" ::: "memory"); }
static inline void irq_enable(void)  { __asm__ volatile("cpsie i" ::: "memory"); }
static inline void irq_disable(void) { __asm__ volatile("cpsid i" ::: "memory"); }

/* ---- System counter (SYC, 24 MHz, time base of the CA55 generic timer; started by BL2) ---- */
#define SYC_CNTCR		CM33_PERI(0x11000000u)	/* control frame, bit0 EN */
#define SYC_CNTCV_LO		CM33_PERI(0x11001000u)	/* read frame */
#define SYC_CNTCV_HI		CM33_PERI(0x11001004u)
#define SYC_HZ			(24000000u)

static inline uint32_t syc_lo(void)
{
	return mmio_read_32(SYC_CNTCV_LO);
}

static inline uint64_t syc_now(void)
{
	uint32_t hi, lo;

	do {
		hi = mmio_read_32(SYC_CNTCV_HI);
		lo = mmio_read_32(SYC_CNTCV_LO);
	} while (hi != mmio_read_32(SYC_CNTCV_HI));
	return ((uint64_t)hi << 32) | lo;
}

static inline int syc_running(void)
{
	return (mmio_read_32(SYC_CNTCR) & 1u) != 0u;
}

/* Busy wait based on SYC. Must only be used for short waits (< 1 s). */
void delay_us(uint32_t us);

/* ---- CPG (module clock / reset / module stop), CA55 base 0x1101_0000 ---- */
#define CPG_BASE		CM33_PERI(0x11010000u)
/* Module registers: upper 16 bits = write enable of the lower 16 bits */
#define CPG_CLKON(off)		(CPG_BASE + (off))
#define CPG_CLKMON(off)		(CPG_BASE + 0x180u + (off))
#define CPG_RST(off)		(CPG_BASE + (off))
#define CPG_RSTMON(off)		(CPG_BASE + 0x180u + (off))
#define CPG_BUS_MCPU2_MSTOP	(CPG_BASE + 0xB68u)

/* ---- Cortex-M33 system registers ---- */
#define SCB_ICSR		(0xE000ED04u)
#define SCB_VTOR		(0xE000ED08u)
#define SCB_SCR			(0xE000ED10u)
#define SCB_SHCSR		(0xE000ED24u)
#define SCB_CFSR		(0xE000ED28u)
#define SYST_CSR		(0xE000E010u)
#define SYST_RVR		(0xE000E014u)
#define SYST_CVR		(0xE000E018u)
#define DEMCR			(0xE000EDFCu)
#define DWT_CTRL		(0xE0001000u)
#define DWT_CYCCNT		(0xE0001004u)

#endif /* CM33_H */
