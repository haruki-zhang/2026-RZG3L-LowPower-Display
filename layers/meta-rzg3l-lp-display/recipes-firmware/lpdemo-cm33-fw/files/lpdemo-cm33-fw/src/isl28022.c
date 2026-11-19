// SPDX-License-Identifier: BSD-3-Clause
/*
 * ISL28022 digital power monitor (datasheet FN8386 Rev 8.02).
 *
 * Registers are 16 bit, MSB first. The calibration/current/power registers are not used:
 * current and power are computed from the shunt and bus voltage registers, so that the
 * result does not depend on the calibration value.
 */
#include <stdint.h>
#include "isl28022.h"
#include "riic.h"
#include "lpdemo_shared.h"

#define REG_CONFIG		(0x00u)
#define REG_VSHUNT		(0x01u)
#define REG_VBUS		(0x02u)

/*
 * Configuration (Table 3):
 *   BRNG = 00   16 V bus range: 5V_MODULE only (bus register Table 14: bits 14:3)
 *   PG   = 01   /2, +-80 mV shunt range = +-10 A with 8 mOhm
 *   BADC = 1111 128 samples averaged, 64.01 ms
 *   SADC = 1111 128 samples averaged, 64.01 ms
 *   MODE = 111  shunt and bus, continuous
 */
#define CFG_BRNG_16V		(0u << 13)
#define CFG_PG_DIV2		(1u << 11)
#define CFG_BADC_128		(0xFu << 7)
#define CFG_SADC_128		(0xFu << 3)
#define CFG_MODE_CONT		(7u << 0)
#define ISL28022_CONFIG		(CFG_BRNG_16V | CFG_PG_DIV2 | CFG_BADC_128 | CFG_SADC_128 | \
				 CFG_MODE_CONT)

#define VSHUNT_LSB_UV		(10)		/* independent of PG */
#define VBUS_LSB_MV		(4)		/* all BRNG settings */
#define VBUS_SHIFT_16V		(3)		/* BRNG = 00: data in bits 14:3 */
#define VBUS_MASK_16V		(0x0FFFu)

static int reg_write(uint8_t reg, uint16_t val)
{
	uint8_t b[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };

	return riic_write_read(ISL28022_ADDR, b, 3, 0, 0);
}

static int reg_read(uint8_t reg, uint16_t *val)
{
	uint8_t b[2];
	int ret;

	ret = riic_write_read(ISL28022_ADDR, &reg, 1, b, 2);
	if (ret == LPDEMO_PM_OK)
		*val = (uint16_t)((b[0] << 8) | b[1]);
	return ret;
}

int isl28022_init(uint16_t *config_readback)
{
	uint16_t v = 0;
	int ret;

	ret = reg_write(REG_CONFIG, ISL28022_CONFIG);
	if (ret == LPDEMO_PM_OK)
		ret = reg_read(REG_CONFIG, &v);
	*config_readback = v;
	if (ret == LPDEMO_PM_OK && v != ISL28022_CONFIG)
		ret = LPDEMO_PM_BAD_CONFIG;
	return ret;
}

int isl28022_read(struct isl28022_sample *s)
{
	int ret;

	ret = reg_read(REG_VSHUNT, &s->raw_vshunt);
	if (ret == LPDEMO_PM_OK)
		ret = reg_read(REG_VBUS, &s->raw_vbus);
	if (ret != LPDEMO_PM_OK)
		return ret;

	/* Shunt: two's complement with sign extension (Table 10), 10 uV/LSB */
	s->vshunt_uv = (int32_t)(int16_t)s->raw_vshunt * VSHUNT_LSB_UV;
	s->vbus_mv = (int32_t)((s->raw_vbus >> VBUS_SHIFT_16V) & VBUS_MASK_16V) * VBUS_LSB_MV;
	/* I[mA] = V[uV] * 1000 / R[uOhm]; |vshunt| <= 81920 uV, so no overflow */
	s->current_ma = (s->vshunt_uv * 1000) / (int32_t)ISL28022_RSHUNT_UOHM;
	/*
	 * P[mW] = Vbus[mV] * Vshunt[uV] / R[uOhm], computed from the voltages directly to
	 * avoid the rounding of current_ma. Max |16380 * 81920| = 1.3e9 < 2^31.
	 */
	s->power_mw = (s->vbus_mv * s->vshunt_uv) / (int32_t)ISL28022_RSHUNT_UOHM;
	return LPDEMO_PM_OK;
}
