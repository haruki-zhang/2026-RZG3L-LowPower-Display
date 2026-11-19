/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * CM33 replacement for TF-A <lib/mmio.h>.
 *
 * The DDR driver uses CA55 physical addresses. On the CM33, the peripherals at CA55 address
 * 0x1xxx_xxxx are seen at 0x4xxx_xxxx (secure alias, HWM Table 1.8-2): translate here.
 * On-chip SRAM (< 0x1000_0000) has the same address on both CPUs.
 */
#ifndef LPDEMO_SHIM_MMIO_H
#define LPDEMO_SHIM_MMIO_H

#include <stdint.h>

#define CM33_PERI_CA55_START	(0x10000000u)
#define CM33_PERI_CA55_END	(0x20000000u)
#define CM33_SECURE_ALIAS_OFS	(0x30000000u)

static inline uintptr_t cm33_addr(uintptr_t addr)
{
	if (addr >= CM33_PERI_CA55_START && addr < CM33_PERI_CA55_END)
		return addr + CM33_SECURE_ALIAS_OFS;
	return addr;
}

static inline void mmio_write_8(uintptr_t addr, uint8_t value)
{
	*(volatile uint8_t *)cm33_addr(addr) = value;
}

static inline uint8_t mmio_read_8(uintptr_t addr)
{
	return *(volatile uint8_t *)cm33_addr(addr);
}

static inline void mmio_write_16(uintptr_t addr, uint16_t value)
{
	*(volatile uint16_t *)cm33_addr(addr) = value;
}

static inline uint16_t mmio_read_16(uintptr_t addr)
{
	return *(volatile uint16_t *)cm33_addr(addr);
}

static inline void mmio_write_32(uintptr_t addr, uint32_t value)
{
	*(volatile uint32_t *)cm33_addr(addr) = value;
}

static inline uint32_t mmio_read_32(uintptr_t addr)
{
	return *(volatile uint32_t *)cm33_addr(addr);
}

static inline void mmio_write_64(uintptr_t addr, uint64_t value)
{
	mmio_write_32(addr, (uint32_t)value);
	mmio_write_32(addr + 4u, (uint32_t)(value >> 32));
}

static inline void mmio_clrbits_32(uintptr_t addr, uint32_t clear)
{
	mmio_write_32(addr, mmio_read_32(addr) & ~clear);
}

static inline void mmio_setbits_32(uintptr_t addr, uint32_t set)
{
	mmio_write_32(addr, mmio_read_32(addr) | set);
}

static inline void mmio_clrsetbits_32(uintptr_t addr, uint32_t clear, uint32_t set)
{
	mmio_write_32(addr, (mmio_read_32(addr) & ~clear) | set);
}

#endif /* LPDEMO_SHIM_MMIO_H */
