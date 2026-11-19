/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * DISP_RET wake-up on the CM33: DDR retention exit and wake-up of the CA55.
 */
#ifndef WAKE_H
#define WAKE_H

#include <stdint.h>

/* Enables the MHU (clock, module stop, reset). Called once at boot. */
void mhu_init(void);

/* Raises MHU software interrupt ch4: CM33 -> CA55 core 0 (GIC SPI 74). */
void mhu_wake_ca55(void);

/* MHU_SW_INT_STS4_NS (bit 0: interrupt asserted) */
uint32_t mhu_wake_status(void);

/*
 * Checks the DDR retention CSR snapshot left by BL2 in SRAM and loads it for
 * ddr_retention_exit(). Returns LPDEMO_SUSP_OK or LPDEMO_SUSP_ERR_RETCSR.
 */
int retcsr_load(void);

/* PLL4 (DDR) out of standby, waits for the lock. Returns LPDEMO_SUSP_OK / _ERR_PLL4. */
int pll4_on(void);

#endif /* WAKE_H */
