/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Display hand-over between Linux (rz-du / vsp1) and the CM33 on the running VSPD.
 */
#ifndef VSPD_H
#define VSPD_H

#include <stdint.h>
#include "lpdemo_shared.h"

/*
 * Switches the running VSPD from the Linux display list to the LP display list in SRAM
 * (virtual RPF background + RPF0 static layer + RPF1 power read-out, placed as given in the
 * control block). Returns LPDEMO_DISP_*.
 */
int vspd_enter(volatile struct lpdemo_ctrl *ctrl);

/*
 * Restores the registers changed by the LP display list, switches back to the Linux
 * display list saved at vspd_enter() and re-enables the WPF0 interrupts.
 * Returns LPDEMO_DISP_*.
 */
int vspd_leave(volatile struct lpdemo_ctrl *ctrl);

/* Called every tick while the CM33 owns the display: counts and clears WPF0 underruns. */
void vspd_poll(volatile struct lpdemo_ctrl *ctrl);

/*
 * Suspend progress (debug, LPDEMO_LP_F_PROGRESS) in the bottom rows of RPF1:
 * VSPD_BAR_SEGMENTS segments, green = reached (bit set in done), red = failed (bit set in
 * failed), grey = not reached. Only redrawn when the masks change.
 */
#define VSPD_BAR_SEGMENTS	(9u)
void vspd_bar_progress(uint32_t done, uint32_t failed);

#endif /* VSPD_H */
