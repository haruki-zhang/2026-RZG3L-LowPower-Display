// SPDX-License-Identifier: BSD-3-Clause
/*
 * Debug output of the CM33 on the console SCIF0, same FIFO handling as the TF-A console
 * driver (scifa.S). The CA55 console (Linux / BL31) shares the line: messages interleave.
 */
#include <stdint.h>
#include "cm33.h"
#include "dbg.h"

#define SCIF0_BASE		CM33_PERI(0x100AC000u)
#define SCIF_FTDR		(SCIF0_BASE + 0x06u)
#define SCIF_FDR		(SCIF0_BASE + 0x0Eu)
#define FIFO_SIZE		(16u)
#define PUTC_TIMEOUT		(200000u)	/* polls, the line may be stuck */

static int dbg_on;

void dbg_enable(int on)
{
	dbg_on = on;
}

void dbg_putc(char c)
{
	uint32_t t;

	if (!dbg_on)
		return;
	for (t = 0; t < PUTC_TIMEOUT; t++) {
		if (((*(volatile uint16_t *)SCIF_FDR >> 8) & 0x1Fu) < FIFO_SIZE)
			break;
	}
	*(volatile uint8_t *)SCIF_FTDR = (uint8_t)c;
}

void dbg_msg(const char *s, uint32_t hex)
{
	const char *p;
	int i;

	if (!dbg_on)
		return;
	dbg_putc('\r');
	dbg_putc('\n');
	for (p = "[CM33 "; *p; p++)
		dbg_putc(*p);
	for (; *s; s++)
		dbg_putc(*s);
	dbg_putc(' ');
	for (i = 28; i >= 0; i -= 4)
		dbg_putc("0123456789abcdef"[(hex >> i) & 0xFu]);
	dbg_putc(']');
}
