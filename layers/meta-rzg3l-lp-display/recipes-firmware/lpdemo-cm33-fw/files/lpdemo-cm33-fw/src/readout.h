/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Power read-out of the LP picture (Step 4): the number and the saving in RPF1, redrawn by
 * the CM33 over the template the GUI rendered (design_handoff_lpdemo_gui section 7.2).
 */
#ifndef READOUT_H
#define READOUT_H

#include <stdint.h>
#include "lpdemo_shared.h"

/*
 * Before the hand-over (RPF1 is not scanned out yet): checks that the LP picture asks for
 * the read-out and that RPF1 has the geometry of the glyph tables, then draws the current
 * value. Returns 1 when the read-out is active for this LP period.
 */
int readout_begin(volatile struct lpdemo_ctrl *ctrl);

/*
 * While the CM33 owns the display: redraws the value if it changed, right after a frame
 * end (RPF1 is single buffered).
 */
void readout_update(volatile struct lpdemo_ctrl *ctrl);

#endif /* READOUT_H */
