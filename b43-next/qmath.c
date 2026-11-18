// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom B43 wireless driver
 *
 * Fixed point arithmetic, as the stock driver does it.
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include "qmath.h"

/*
 * CORDIC in the stock driver's fixed point: the angle in degrees Q16, the
 * vector of 2^16 after 18 rotations (the start value 39797 is 2^16 over the
 * CORDIC gain), truncating shifts. It is lib/math/cordic.c, which takes
 * whole degrees only; the angles here are a few degrees with a fraction, so
 * the rotation is done here on the Q16 angle. The inverse is the same
 * rotation in vectoring mode on the same table: the vector, scaled by 16
 * first, is turned onto the axis and the angle it took is the result.
 */
#define B43_CORDIC_GAIN	39797
#define B43_CORDIC_ITER	18

static const s32 b43_cordic_atan[B43_CORDIC_ITER] = {
	2949120, 1740967, 919879, 466945, 234379, 117304, 58666, 29335,
	14668, 7334, 3667, 1833, 917, 458, 229, 115, 57, 29,
};

/* Angle in degrees Q16 -> (cos, sin) in Q16, |theta| up to 90. */
void b43_cordic_rotate(s32 theta, s32 *cos_out, s32 *sin_out)
{
	s32 i = B43_CORDIC_GAIN, q = 0, angle = 0, t;
	unsigned int k;

	for (k = 0; k < B43_CORDIC_ITER; k++) {
		if (theta > angle) {
			t = i - (q >> k);
			q += i >> k;
			angle += b43_cordic_atan[k];
		} else {
			t = i + (q >> k);
			q -= i >> k;
			angle -= b43_cordic_atan[k];
		}
		i = t;
	}
	*cos_out = i;
	*sin_out = q;
}

/* Vector (x, y), x > 0, both under 2^27 -> its angle in degrees Q16. */
s32 b43_cordic_vector(s32 y, s32 x)
{
	s32 angle = 0, ty, tx;
	unsigned int k;

	y <<= 4;
	x <<= 4;
	for (k = 0; k < B43_CORDIC_ITER; k++) {
		ty = y >> k;
		tx = x >> k;
		if (y >= 0) {
			angle += b43_cordic_atan[k];
			y -= tx;
			x += ty;
		} else {
			angle -= b43_cordic_atan[k];
			y += tx;
			x -= ty;
		}
	}
	return angle;
}

/* Square root rounded to nearest. */
u32 b43_sqrt_near(u64 v)
{
	u32 r = int_sqrt64(v);

	return v - (u64)r * r > r ? r + 1 : r;
}

/* Division rounded half away from zero, as a numerator biased by half the
 * denominator and then truncated. */
s32 b43_div_near(s64 num, s64 den)
{
	return (s32)div64_s64(num + (num > 0 ? den >> 1 : -(den >> 1)), den);
}
