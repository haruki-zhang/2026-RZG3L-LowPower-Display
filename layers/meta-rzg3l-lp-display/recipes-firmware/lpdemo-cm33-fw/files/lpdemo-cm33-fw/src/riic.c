// SPDX-License-Identifier: BSD-3-Clause
/*
 * RIIC0 (I2C_PM) master, polling mode.
 *
 * Register layout: RZ/G3L RIIC with byte registers (FSP R9A08G046L riic_iodefine.h,
 * Linux i2c-riic.c riic_rz_v2h_regs). The bit rate calculation and the transfer sequence
 * follow Linux i2c-riic.c (riic_init_hw(), riic_xfer_atomic()), with NACK handling added.
 */
#include <stdint.h>
#include "cm33.h"
#include "riic.h"
#include "lpdemo_shared.h"

#define RIIC0_BASE		CM33_PERI(0x100AE000u)
#define ICCR1			(RIIC0_BASE + 0x00u)
#define ICCR2			(RIIC0_BASE + 0x01u)
#define ICMR1			(RIIC0_BASE + 0x02u)
#define ICMR2			(RIIC0_BASE + 0x03u)
#define ICMR3			(RIIC0_BASE + 0x04u)
#define ICFER			(RIIC0_BASE + 0x05u)
#define ICSER			(RIIC0_BASE + 0x06u)
#define ICIER			(RIIC0_BASE + 0x07u)
#define ICSR1			(RIIC0_BASE + 0x08u)
#define ICSR2			(RIIC0_BASE + 0x09u)
#define ICBRL			(RIIC0_BASE + 0x10u)
#define ICBRH			(RIIC0_BASE + 0x11u)
#define ICDRT			(RIIC0_BASE + 0x12u)
#define ICDRR			(RIIC0_BASE + 0x13u)

#define ICCR1_ICE		(1u << 7)
#define ICCR1_IICRST		(1u << 6)
#define ICCR1_SOWP		(1u << 4)
#define ICCR1_SDAO		(1u << 2)
#define ICCR1_SCLI		(1u << 1)
#define ICCR1_SDAI		(1u << 0)

#define ICCR2_BBSY		(1u << 7)
#define ICCR2_SP		(1u << 3)
#define ICCR2_RS		(1u << 2)
#define ICCR2_ST		(1u << 1)

#define ICMR1_BCWP		(1u << 3)
#define ICMR1_CKS(x)		((((x) & 7u) << 4) | ICMR1_BCWP)

#define ICMR3_RDRFS		(1u << 5)
#define ICMR3_ACKWP		(1u << 4)
#define ICMR3_ACKBT		(1u << 3)

#define ICFER_SCLE		(1u << 6)
#define ICFER_NFE		(1u << 5)

#define ICSR2_TDRE		(1u << 7)
#define ICSR2_TEND		(1u << 6)
#define ICSR2_RDRF		(1u << 5)
#define ICSR2_NACKF		(1u << 4)
#define ICSR2_STOP		(1u << 3)
#define ICSR2_START		(1u << 2)

#define ICBR_RESERVED		(0xE0u)		/* write as 1 */

/* RIIC0 module: CPG_CLKON_I2C / CPG_RST_I2C bit 0, CPG_BUS_MCPU2_MSTOP bit 10 */
#define CPG_I2C_OFF_CLK		(0x580u)
#define CPG_I2C_OFF_RST		(0x880u)
#define CPG_I2C0_BIT		(1u << 0)
#define MSTOP_I2C0_BIT		(1u << 10)

/* RIIC0 PCLK = P0 = PLL2 1600 MHz / 2 / 8 (BSP clock setting) */
#define RIIC_PCLK_HZ		(100000000u)
#define RIIC_BUS_HZ		(100000u)
#define RIIC_SCL_RISE_NS	(1000u)		/* I2C standard mode maxima */
#define RIIC_SCL_FALL_NS	(300u)

#define RIIC_FLAG_TIMEOUT_US	(2000u)
#define RIIC_BUS_FREE_US	(10000u)

static inline void wr(uintptr_t reg, uint32_t v) { mmio_write_8(reg, (uint8_t)v); }
static inline uint32_t rd(uintptr_t reg) { return mmio_read_8(reg); }

static int module_enable(void)
{
	uint32_t t;

	mmio_write_32(CPG_CLKON(CPG_I2C_OFF_CLK), (CPG_I2C0_BIT << 16) | CPG_I2C0_BIT);
	for (t = 0; !(mmio_read_32(CPG_CLKMON(CPG_I2C_OFF_CLK)) & CPG_I2C0_BIT); t++) {
		if (t > 100u)
			return LPDEMO_PM_CLK_FAIL;
		delay_us(1);
	}
	mmio_write_32(CPG_BUS_MCPU2_MSTOP, MSTOP_I2C0_BIT << 16);	/* clear module stop */
	mmio_write_32(CPG_RST(CPG_I2C_OFF_RST), (CPG_I2C0_BIT << 16) | CPG_I2C0_BIT);
	for (t = 0; mmio_read_32(CPG_RSTMON(CPG_I2C_OFF_RST)) & CPG_I2C0_BIT; t++) {
		if (t > 200u)
			return LPDEMO_PM_CLK_FAIL;
		delay_us(1);
	}
	return LPDEMO_PM_OK;
}

int riic_init(void)
{
	uint32_t rate = RIIC_PCLK_HZ;
	uint32_t total, cks, ns_per_tick;
	int32_t brl, brh;
	int ret;

	ret = module_enable();
	if (ret != LPDEMO_PM_OK)
		return ret;

	/* Linux riic_init_hw(): 60 % low / 40 % high, at most 31 + 3 ticks low */
	total = (rate + RIIC_BUS_HZ - 1u) / RIIC_BUS_HZ;
	brl = 0;
	for (cks = 0; cks < 8u; cks++) {
		brl = (int32_t)((total * 6u) / 10u);
		if (brl <= 0x1F + 3)
			break;
		total = (total + 1u) / 2u;
		rate /= 2u;
	}
	brh = (int32_t)total - brl;
	if (cks == 0u) {
		brl -= 4;
		brh -= 4;
	} else {
		brl -= 3;
		brh -= 3;
	}
	ns_per_tick = 1000000000u / rate;
	brl -= (int32_t)(RIIC_SCL_FALL_NS / ns_per_tick);
	brh -= (int32_t)(RIIC_SCL_RISE_NS / ns_per_tick);
	if (brl < 1)
		brl = 1;
	if (brh < 1)
		brh = 1;

	/* Changing the order of accessing IICRST and ICE may break things (Linux) */
	wr(ICCR1, ICCR1_IICRST | ICCR1_SOWP);
	wr(ICCR1, rd(ICCR1) | ICCR1_ICE);
	wr(ICFER, rd(ICFER) | ICFER_SCLE | ICFER_NFE);
	wr(ICMR1, ICMR1_CKS(cks));
	wr(ICBRH, (uint32_t)brh | ICBR_RESERVED);
	wr(ICBRL, (uint32_t)brl | ICBR_RESERVED);
	wr(ICSER, 0);
	wr(ICIER, 0);
	wr(ICMR3, ICMR3_ACKWP | ICMR3_RDRFS);
	wr(ICCR1, rd(ICCR1) & ~ICCR1_IICRST);
	return LPDEMO_PM_OK;
}

/* Waits for any bit of mask in ICSR2. A NACK aborts the wait. */
static int wait_sr2(uint32_t mask)
{
	uint32_t t0 = syc_lo();
	uint32_t limit = RIIC_FLAG_TIMEOUT_US * (SYC_HZ / 1000000u);

	for (;;) {
		uint32_t sr2 = rd(ICSR2);

		if (sr2 & ICSR2_NACKF)
			return LPDEMO_PM_NACK;
		if (sr2 & mask)
			return LPDEMO_PM_OK;
		if ((syc_lo() - t0) > limit)
			return LPDEMO_PM_TIMEOUT;
	}
}

static int wait_cond(uintptr_t reg, uint32_t mask, uint32_t want, uint32_t timeout_us)
{
	uint32_t t0 = syc_lo();
	uint32_t limit = timeout_us * (SYC_HZ / 1000000u);

	while ((rd(reg) & mask) != want) {
		if ((syc_lo() - t0) > limit)
			return LPDEMO_PM_TIMEOUT;
	}
	return LPDEMO_PM_OK;
}

/* Issues STOP after an error/NACK and waits for the bus to be released. */
static void stop_and_release(int reading)
{
	wr(ICCR2, ICCR2_SP);
	if (reading)
		(void)rd(ICDRR);	/* release SCL */
	(void)wait_cond(ICSR2, ICSR2_STOP, ICSR2_STOP, RIIC_FLAG_TIMEOUT_US);
	wr(ICSR2, 0);
	(void)rd(ICSR2);
}

/* START/repeated START + address byte. */
static int send_address(uint32_t start_bit, uint8_t addr_rw)
{
	int ret;

	wr(ICCR2, start_bit);
	/* Before writing the address: START and TDRE set, SDAO and SDAI low (Linux) */
	ret = wait_cond(ICSR2, ICSR2_START | ICSR2_TDRE, ICSR2_START | ICSR2_TDRE,
			RIIC_FLAG_TIMEOUT_US);
	if (ret == LPDEMO_PM_OK)
		ret = wait_cond(ICCR1, ICCR1_SDAO | ICCR1_SDAI, 0, RIIC_FLAG_TIMEOUT_US);
	if (ret != LPDEMO_PM_OK)
		return ret;
	wr(ICDRT, addr_rw);
	return LPDEMO_PM_OK;
}

int riic_write_read(uint8_t addr, const uint8_t *wbuf, uint32_t wlen,
		    uint8_t *rbuf, uint32_t rlen)
{
	uint32_t i;
	int ret;

	/* Bus barrier: BBSY clear and both lines high */
	if (wait_cond(ICCR2, ICCR2_BBSY, 0, RIIC_BUS_FREE_US) != LPDEMO_PM_OK ||
	    (rd(ICCR1) & (ICCR1_SDAI | ICCR1_SCLI)) != (ICCR1_SDAI | ICCR1_SCLI))
		return LPDEMO_PM_BUS_BUSY;

	wr(ICSR2, 0);

	/* Write phase */
	ret = send_address(ICCR2_ST, (uint8_t)(addr << 1));
	if (ret != LPDEMO_PM_OK)
		goto err_write;
	for (i = 0; i < wlen; i++) {
		ret = wait_sr2(ICSR2_TDRE);
		if (ret != LPDEMO_PM_OK)
			goto err_write;
		wr(ICDRT, wbuf[i]);
	}
	ret = wait_sr2(ICSR2_TEND);
	if (ret != LPDEMO_PM_OK)
		goto err_write;

	if (rlen == 0u) {
		wr(ICCR2, ICCR2_SP);
		goto done;
	}

	/* Read phase */
	wr(ICSR2, 0);
	(void)rd(ICSR2);
	ret = send_address(ICCR2_RS, (uint8_t)((addr << 1) | 1u));
	if (ret != LPDEMO_PM_OK)
		goto err_write;
	ret = wait_sr2(ICSR2_RDRF);
	if (ret != LPDEMO_PM_OK)
		goto err_read;
	(void)rd(ICDRR);	/* dummy read: starts the reception of the first byte */

	for (i = 0; i < rlen; i++) {
		ret = wait_cond(ICSR2, ICSR2_RDRF, ICSR2_RDRF, RIIC_FLAG_TIMEOUT_US);
		if (ret != LPDEMO_PM_OK)
			goto err_read;
		if (i == rlen - 1u) {
			/* STOP must come before ACKBT (Linux) */
			wr(ICCR2, ICCR2_SP);
			wr(ICMR3, rd(ICMR3) | ICMR3_ACKBT);
		} else {
			wr(ICMR3, rd(ICMR3) & ~ICMR3_ACKBT);
		}
		rbuf[i] = (uint8_t)rd(ICDRR);
	}

done:
	ret = wait_cond(ICSR2, ICSR2_STOP, ICSR2_STOP, RIIC_FLAG_TIMEOUT_US);
	wr(ICSR2, 0);
	(void)rd(ICSR2);
	if (ret == LPDEMO_PM_OK)
		ret = wait_cond(ICCR2, ICCR2_BBSY, 0, RIIC_FLAG_TIMEOUT_US);
	return ret;

err_read:
	stop_and_release(1);
	return ret;
err_write:
	stop_and_release(0);
	return ret;
}
