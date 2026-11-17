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

enum b43_rx_iq_comp {
	B43_RX_IQ_COMP_OK,
	B43_RX_IQ_COMP_LOW_PWR,		/* I and Q powers under 2 */
	B43_RX_IQ_COMP_NO_A,		/* no divisor for a */
	B43_RX_IQ_COMP_NO_B,		/* no divisor for b */
};

enum b43_rx_iq_comp b43_rx_iq_comp(s32 iq, u32 ii, u32 qq, s32 *a_out,
				   s32 *b_out);

#endif /* B43_CALIBRATE_H_ */
