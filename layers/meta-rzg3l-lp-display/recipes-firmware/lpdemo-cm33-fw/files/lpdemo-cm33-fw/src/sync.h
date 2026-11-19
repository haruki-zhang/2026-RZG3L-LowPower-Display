/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Frame boundary for the single buffered RPF1 redraw (Step 4), found by probing status
 * registers of the VSPD, DU and DSI (lpdemo_shared.h, struct lpdemo_probe).
 */
#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include "lpdemo_shared.h"

/*
 * Samples the status registers for LPDEMO_PROBE_MS, writes the results to probe and picks
 * the frame boundary signal. alive() is called regularly (heartbeat: BL31 checks it).
 */
void sync_probe(volatile struct lpdemo_probe *probe, void (*alive)(void));

/* LPDEMO_SYNC_* chosen by the last probe */
uint32_t sync_method(void);

/*
 * Waits for the next frame boundary. Returns 0, or -1 without a usable signal or after
 * timeout_us. *us = time waited.
 */
int sync_wait(uint32_t timeout_us, uint32_t *us);

#endif /* SYNC_H */
