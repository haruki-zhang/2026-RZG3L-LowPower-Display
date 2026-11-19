/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Debug output of the CM33 on the console SCIF0 (LPDEMO_SUSP_MODE_DEBUG_UART only: Linux
 * keeps the SCIF clock running through the suspend only with console_suspend=N).
 */
#ifndef DBG_H
#define DBG_H

#include <stdint.h>

void dbg_enable(int on);
void dbg_putc(char c);
/* "\r\n[CM33 <s> <hex>]" */
void dbg_msg(const char *s, uint32_t hex);

#endif /* DBG_H */
