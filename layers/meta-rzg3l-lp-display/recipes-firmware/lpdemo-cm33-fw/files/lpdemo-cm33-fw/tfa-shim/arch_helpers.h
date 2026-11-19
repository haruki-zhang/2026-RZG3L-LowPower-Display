/* SPDX-License-Identifier: BSD-3-Clause */
/* CM33 replacement for TF-A <arch_helpers.h>: only what the DDR driver uses. */
#ifndef LPDEMO_SHIM_ARCH_HELPERS_H
#define LPDEMO_SHIM_ARCH_HELPERS_H

static inline void dsb(void)
{
	__asm__ volatile("dsb" ::: "memory");
}

static inline void dsbsy(void)
{
	__asm__ volatile("dsb" ::: "memory");
}

#endif /* LPDEMO_SHIM_ARCH_HELPERS_H */
