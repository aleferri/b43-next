/* SPDX-License-Identifier: ISC */
/*
 * lib/math/cordic.c and its header, for the userspace build.
 *
 * Copyright (c) 2011 Broadcom Corporation
 */
#ifndef _STUB_LINUX_CORDIC_H
#define _STUB_LINUX_CORDIC_H

#include <linux/types.h>

#define CORDIC_ANGLE_GEN	39797
#define CORDIC_PRECISION_SHIFT	16
#define CORDIC_NUM_ITER	(CORDIC_PRECISION_SHIFT + 2)

#define CORDIC_FIXED(X)	((s32)((X) << CORDIC_PRECISION_SHIFT))
#define CORDIC_FLOAT(X)	(((X) >= 0) \
		? ((((X) >> (CORDIC_PRECISION_SHIFT - 1)) + 1) >> 1) \
		: -((((-(X)) >> (CORDIC_PRECISION_SHIFT - 1)) + 1) >> 1))

struct cordic_iq {
	s32 i;
	s32 q;
};

static inline struct cordic_iq cordic_calc_iq(s32 theta)
{
	static const s32 arctan_table[] = {
		2949120, 1740967, 919879, 466945, 234379, 117304, 58666,
		29335, 14668, 7334, 3667, 1833, 917, 458, 229, 115, 57, 29
	};
	struct cordic_iq coord;
	s32 angle, valtmp;
	unsigned int iter;
	int signx = 1;
	int signtheta;

	coord.i = CORDIC_ANGLE_GEN;
	coord.q = 0;
	angle = 0;

	theta = CORDIC_FIXED(theta);
	signtheta = (theta < 0) ? -1 : 1;
	theta = ((theta + CORDIC_FIXED(180) * signtheta) % CORDIC_FIXED(360)) -
		CORDIC_FIXED(180) * signtheta;

	if (CORDIC_FLOAT(theta) > 90) {
		theta -= CORDIC_FIXED(180);
		signx = -1;
	} else if (CORDIC_FLOAT(theta) < -90) {
		theta += CORDIC_FIXED(180);
		signx = -1;
	}

	for (iter = 0; iter < CORDIC_NUM_ITER; iter++) {
		if (theta > angle) {
			valtmp = coord.i - (coord.q >> iter);
			coord.q += (coord.i >> iter);
			angle += arctan_table[iter];
		} else {
			valtmp = coord.i + (coord.q >> iter);
			coord.q -= (coord.i >> iter);
			angle -= arctan_table[iter];
		}
		coord.i = valtmp;
	}

	coord.i *= signx;
	coord.q *= signx;
	return coord;
}

#endif
