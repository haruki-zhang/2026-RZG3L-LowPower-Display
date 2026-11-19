/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Extension hooks: code another layer links into this firmware (Makefile EXTRA_SRCS,
 * recipe LPDEMO_CM33_EXTRA_SRCS), e.g. the boot logo animation of
 * meta-rzg3l-seamless-logo-cm33-anim. BL2 loads a single CM33 firmware, so a second CM33
 * feature has to live in this one.
 *
 * main.c has weak, empty defaults: without an extension the firmware behaves exactly as
 * before. An extension must not access the DDR while the CA55 is in DISP_RET
 * (susp_state SLEEP / RETENTION): the DDR may be in self-refresh.
 */
#ifndef EXT_H
#define EXT_H

#include <stdint.h>

/* Once at start-up, after the control block is initialised, before the SysTick runs */
void lpdemo_ext_init(void);

/* Every 10 ms tick, after the display hand-over work of the tick. now_ms: ms since start */
void lpdemo_ext_tick(uint32_t now_ms);

/*
 * Nonzero while the extension drives the display: a hand-over (ENTER) is then refused
 * with LPDEMO_DISP_ERR_BUSY, which lpdemo-ctl and the rz-du already treat as "retry".
 */
int lpdemo_ext_display_busy(void);

#endif /* EXT_H */
