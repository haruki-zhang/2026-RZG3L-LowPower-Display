// SPDX-License-Identifier: BSD-3-Clause
/*
 * DISP_RET wake-up on the CM33 (design document section 3.2).
 *
 * This file is compiled together with the TF-A DDR driver (ddr_retention_exit(), the same
 * sources and DDR parameters as BL2) and uses CA55 physical addresses like it: the
 * tfa-shim <lib/mmio.h> translates them to the CM33 secure alias.
 */
#include <stdint.h>
#include <cpg_regs.h>
#include <ddr.h>
#include <lib/mmio.h>
#include "lpdemo_shared.h"
#include "wake.h"

extern void delay_us(uint32_t us);

/* ddr_retention_exit() restores the PHY from this table (restore_retcsr()) */
uint32_t ddr_csr_table[RET_CSR_SIZE];

_Static_assert(LPDEMO_RETCSR_WORDS <= RET_CSR_SIZE, "retcsr snapshot larger than the table");

/* MHU: CPG_CLKON_MHU / CPG_RST_MHU / CPG_MHU_MSTOP bit 0 (cpg_regs.h, 0x1101_0520/0820/0B88) */
#define MHU_BASE		(0x10400000u)
#define MHU_SW_INT_STS(n)	(MHU_BASE + 0x800u + (n) * 0x10u)
#define MHU_SW_INT_SET(n)	(MHU_BASE + 0x804u + (n) * 0x10u)

#define PLL_MON_RESETB		(1u << 0)
#define PLL_MON_LOCK		(1u << 4)
#define PLL4_STBY_NORMAL	(0x00010001u)	/* RESETB=1, as cpg_pll_setup() */
#define PLL4_LOCK_TIMEOUT_US	(10000u)

void mhu_init(void)
{
	uint32_t t;

	mmio_write_32(CPG_CLKON_MHU, 0x00010001u);
	for (t = 0; !(mmio_read_32(CPG_CLKMON_MHU) & 1u) && t < 100u; t++)
		delay_us(1);
	mmio_write_32(CPG_MHU_MSTOP, 0x00010000u);
	mmio_write_32(CPG_RST_MHU, 0x00010001u);
	for (t = 0; (mmio_read_32(CPG_RSTMON_MHU) & 1u) && t < 100u; t++)
		delay_us(1);
}

void mhu_wake_ca55(void)
{
	/* Level interrupt, negated by the CA55 (BL31) with SW_INT_CLR4 */
	mmio_write_32(MHU_SW_INT_SET(LPDEMO_MHU_SWINT_CH), 1u);
	__asm__ volatile("dsb" ::: "memory");
}

uint32_t mhu_wake_status(void)
{
	return mmio_read_32(MHU_SW_INT_STS(LPDEMO_MHU_SWINT_CH));
}

/* CRC-32 (IEEE 802.3, reflected), as lpdemo_crc32() in BL2 */
static uint32_t crc32_words(const volatile uint32_t *p, uint32_t words)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (uint32_t i = 0; i < words; i++) {
		uint32_t w = p[i];

		for (uint32_t b = 0; b < 32u; b++) {
			uint32_t bit = (w ^ crc) & 1u;

			crc >>= 1;
			if (bit)
				crc ^= 0xEDB88320u;
			w >>= 1;
		}
	}
	return ~crc;
}

int retcsr_load(void)
{
	const volatile struct lpdemo_retcsr_hdr *h =
		(const volatile struct lpdemo_retcsr_hdr *)LPDEMO_SRAM_RETCSR_BASE;
	const volatile uint32_t *d = (const volatile uint32_t *)(LPDEMO_SRAM_RETCSR_BASE +
								  sizeof(*h));

	if (h->magic != LPDEMO_RETCSR_MAGIC || h->words != LPDEMO_RETCSR_WORDS ||
	    crc32_words(d, h->words) != h->crc32)
		return LPDEMO_SUSP_ERR_RETCSR;
	for (uint32_t i = 0; i < LPDEMO_RETCSR_WORDS; i++)
		ddr_csr_table[i] = d[i];
	return LPDEMO_SUSP_OK;
}

int pll4_on(void)
{
	/* HWM 4.4.6.5.1: normal mode, then wait for RESETB = 1 and LOCK = 1 */
	mmio_write_32(CPG_PLL4_STBY, PLL4_STBY_NORMAL);
	for (uint32_t t = 0; t < PLL4_LOCK_TIMEOUT_US; t++) {
		if ((mmio_read_32(CPG_PLL4_MON) & (PLL_MON_RESETB | PLL_MON_LOCK)) ==
		    (PLL_MON_RESETB | PLL_MON_LOCK))
			return LPDEMO_SUSP_OK;
		delay_us(1);
	}
	return LPDEMO_SUSP_ERR_PLL4;
}

/*
 * TF-A cpg_active_ddr1/2() (soc/g3l/drivers/cpg.c, which cannot be built for the CM33):
 * same register sequence, the waits rounded up to 1 us.
 */
void cpg_active_ddr1(void)
{
	/* 2 */
	mmio_write_32(CPG_RST_DDR, 0x01FE0000u);
	mmio_write_32(CPG_OTHERFUNC2_REG, 0x00010000u);
	/* 3 */
	mmio_write_32(CPG_CLKON_DDR, 0x003F003Fu);
	while ((mmio_read_32(CPG_CLKMON_DDR) & 0x0000003Fu) != 0x0000003Fu)
		;
	/* 4: wait_regaclk(5) */
	delay_us(1);
	/* 5 */
	mmio_write_32(CPG_RST_DDR, 0x01000100u);
	mmio_write_32(CPG_OTHERFUNC2_REG, 0x00010001u);
	/* 6: wait_regaclk(1) */
	delay_us(1);
	/* 7 */
	mmio_write_32(CPG_RST_DDR, 0x007C007Cu);
	/* 8: wait_regaclk(2) */
	delay_us(1);
}

void cpg_active_ddr2(void)
{
	/* 13.11 */
	mmio_write_32(CPG_RST_DDR, 0x00800080u);
	/* 13.12: wait_pclk(2) */
	delay_us(1);
	/* 13.13 */
	mmio_write_32(CPG_RST_DDR, 0x00020002u);
	/* 13.14: wait_pclk(5) */
	delay_us(1);
}

/* <common/debug.h> shim: the DDR driver never panics on the exit path */
void panic(void)
{
	for (;;)
		;
}
