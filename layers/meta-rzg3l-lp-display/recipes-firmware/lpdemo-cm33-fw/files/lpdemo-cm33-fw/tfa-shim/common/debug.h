/* SPDX-License-Identifier: BSD-3-Clause */
/* CM33 replacement for TF-A <common/debug.h>: no console on the CM33. */
#ifndef LPDEMO_SHIM_DEBUG_H
#define LPDEMO_SHIM_DEBUG_H

/* as the TF-A header: ARRAY_SIZE() and friends */
#include <lib/utils_def.h>

#define ERROR(...)	do { } while (0)
#define NOTICE(...)	do { } while (0)
#define WARN(...)	do { } while (0)
#define INFO(...)	do { } while (0)
#define VERBOSE(...)	do { } while (0)

void __attribute__((noreturn)) panic(void);

#endif /* LPDEMO_SHIM_DEBUG_H */
