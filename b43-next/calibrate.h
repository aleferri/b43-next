/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_CALIBRATE_H_
#define B43_CALIBRATE_H_

#include <linux/types.h>
#include <linux/cordic.h>

/*
 * The tones the PHYs play from their sample buffers: a tone of @freq kHz at
 * @rate MHz turns by b43_tone_rot() degrees Q16 per sample, and its sample
 * at an angle is b43_tone_sample() of it, whole degrees, @amp the amplitude.
 */
static inline s32 b43_tone_rot(u32 freq, u32 rate)
{
	return (((freq * 36) / rate) << 16) / 100;
}

struct cordic_iq b43_tone_sample(s32 angle, u16 amp);

/* A sample as the buffers of N, HT and AC hold it: I and Q, 10 bits each */
static inline u32 b43_tone_pack10(struct cordic_iq s)
{
	return ((s.i & 0x3ff) << 10) | (s.q & 0x3ff);
}

#endif /* B43_CALIBRATE_H_ */
