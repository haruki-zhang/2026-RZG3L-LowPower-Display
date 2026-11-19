/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ISL28022 digital power monitor on the carrier board (I2C_PM, 0x44).
 * Shunt R94 = 8 mOhm in 5V_MODULE: measures the whole SMARC module input (Path B).
 */
#ifndef ISL28022_H
#define ISL28022_H

#include <stdint.h>

#define ISL28022_ADDR		(0x44u)
#define ISL28022_RSHUNT_UOHM	(8000u)

struct isl28022_sample {
	uint16_t raw_vshunt;
	uint16_t raw_vbus;
	int32_t vshunt_uv;
	int32_t vbus_mv;
	int32_t current_ma;
	int32_t power_mw;
};

/* Writes the configuration and verifies it. Returns LPDEMO_PM_*. */
int isl28022_init(uint16_t *config_readback);

/* Reads shunt and bus voltage and computes current/power. Returns LPDEMO_PM_*. */
int isl28022_read(struct isl28022_sample *s);

#endif /* ISL28022_H */
