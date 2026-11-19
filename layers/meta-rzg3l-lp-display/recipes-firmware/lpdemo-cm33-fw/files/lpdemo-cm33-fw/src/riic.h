/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * RIIC0 (I2C_PM) master, polling mode.
 */
#ifndef RIIC_H
#define RIIC_H

#include <stdint.h>

/*
 * Enables the RIIC0 module (clock, module stop, reset) and sets it up as a 100 kHz
 * master. Pin function (PL2/PL3 = RIIC0) is set by U-Boot's s_init() and left alone here.
 * Returns LPDEMO_PM_OK or LPDEMO_PM_CLK_FAIL.
 */
int riic_init(void);

/*
 * Writes wlen bytes to the 7-bit address addr, then (rlen > 0) reads rlen bytes after a
 * repeated START. wlen must be > 0. Returns LPDEMO_PM_OK or LPDEMO_PM_BUS_BUSY /
 * LPDEMO_PM_NACK / LPDEMO_PM_TIMEOUT. After an error the caller should call riic_init().
 */
int riic_write_read(uint8_t addr, const uint8_t *wbuf, uint32_t wlen,
		    uint8_t *rbuf, uint32_t rlen);

#endif /* RIIC_H */
