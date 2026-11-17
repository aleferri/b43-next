/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_QMATH_H_
#define B43_QMATH_H_

#include <linux/types.h>

void b43_cordic_rotate(s32 theta, s32 *cos_out, s32 *sin_out);
s32 b43_cordic_vector(s32 y, s32 x);
u32 b43_sqrt_near(u64 v);
s32 b43_div_near(s64 num, s64 den);

#endif /* B43_QMATH_H_ */
