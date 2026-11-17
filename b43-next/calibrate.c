// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom B43 wireless driver
 *
 * What the PHYs' calibrations share: the tones they play, and the
 * arithmetic of their results.
 */

#include <linux/kernel.h>
#include "calibrate.h"

struct cordic_iq b43_tone_sample(s32 angle, u16 amp)
{
	struct cordic_iq s = cordic_calc_iq(CORDIC_FLOAT(angle));

	s.i = CORDIC_FLOAT(s.i * amp);
	s.q = CORDIC_FLOAT(s.q * amp);
	return s;
}
