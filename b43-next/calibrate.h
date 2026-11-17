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

/* A tone's power per sample, from the I and Q powers summed over @samps */
u32 b43_tone_pwr(u32 ii, u32 qq, u32 samps);

enum b43_rx_iq_comp {
	B43_RX_IQ_COMP_OK,
	B43_RX_IQ_COMP_LOW_PWR,		/* I and Q powers under 2 */
	B43_RX_IQ_COMP_NO_A,		/* no divisor for a */
	B43_RX_IQ_COMP_NO_B,		/* no divisor for b */
};

enum b43_rx_iq_comp b43_rx_iq_comp(s32 iq, u32 ii, u32 qq, s32 *a_out,
				   s32 *b_out);

/*
 * brcmsmac's RX calibration gain control: from gain @idx of a table of
 * @max + 1, the highest gain whose tone stays at or under a power of 10000.
 */
struct b43_rxcal_gain {
	int idx;		/* the gain to measure next */
	int prev;
	int optim;		/* the gain found, once a step returns true */
	int max;
	int dirn;		/* 0 until measured, then -1 or 1 */
	u32 prev_pwr;
	u32 optim_pwr;
};

void b43_rxcal_gain_init(struct b43_rxcal_gain *g, int idx, int max);
bool b43_rxcal_gain_step(struct b43_rxcal_gain *g, u32 pwr);
int b43_rxcal_gain_delta(const struct b43_rxcal_gain *g);
u16 b43_rxcal_gain_biq1(u16 biq0, u16 biq1, int delta);

/*
 * The gain ladders the TX I/Q and LO calibrations of N, HT and AC load before
 * measuring a chain: step @i of the LO ladder and of the I/Q one at @bbmult,
 * each a gain in the high byte and an index in the low one.
 */
#define B43_TXCAL_LADDER_STEPS	18

void b43_txcal_ladder(unsigned int i, u16 bbmult, u16 *lo, u16 *iq);

void b43_iq_mismatch(s32 iq, u32 ii, u32 qq, s32 *angle_out, u32 *mag_out);
void b43_rx_iq_comp_mismatch(s32 angle, u32 mag, s16 *a_out, s16 *b_out);

#endif /* B43_CALIBRATE_H_ */
