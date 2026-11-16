/* SPDX-License-Identifier: GPL-2.0
 * Stub for b43/main.h. Scratch code includes this for utility prototypes
 * we do not need in the test build; leave empty and let the linker sort
 * out concrete symbols via wrap.c.
 */
#ifndef _STUB_MAIN_H
#define _STUB_MAIN_H
#include "b43.h"

/* The calibration report, on with AC_CALREPORT in main.c */
extern int unit_calreport;
#define b43_calreport_on(dev) (unit_calreport)

/* The survey's noise, which the trace does not see */
static inline void b43_noise_pwr_report(struct b43_wldev *dev, const u32 *pwr,
					unsigned int cores)
{
}
#endif
