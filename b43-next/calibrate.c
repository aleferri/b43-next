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

/* Number of bits of the magnitude of a value, 0 for 0 */
static u8 b43_nbits(s32 value)
{
	s32 abs_val = abs(value);
	u8 nbits = 0;

	while ((abs_val >> nbits) > 0)
		nbits++;

	return nbits;
}

/*
 * brcmsmac's wlc_phy_calc_rx_iq_comp_nphy() for one estimate: @iq the I/Q
 * product, @ii and @qq the I and Q powers. wl rounds the root of b where
 * brcmsmac truncates it. a and b come out unmasked.
 */
enum b43_rx_iq_comp b43_rx_iq_comp(s32 iq, u32 ii, u32 qq, s32 *a_out,
				   s32 *b_out)
{
	int iq_nbits, qq_nbits, arsh, brsh;
	s32 a, b, tmp;

	if (ii + qq < 2)
		return B43_RX_IQ_COMP_LOW_PWR;

	iq_nbits = b43_nbits(iq);
	qq_nbits = b43_nbits(qq);

	arsh = 10 - (30 - iq_nbits);
	if (arsh >= 0) {
		a = (-(iq << (30 - iq_nbits)) + (ii >> (1 + arsh)));
		tmp = (s32)(ii >> arsh);
	} else {
		a = (-(iq << (30 - iq_nbits)) + (ii << (-1 - arsh)));
		tmp = (s32)(ii << -arsh);
	}
	if (tmp == 0)
		return B43_RX_IQ_COMP_NO_A;
	a /= tmp;

	brsh = qq_nbits - 31 + 20;
	b = (qq << (31 - qq_nbits));
	if (brsh >= 0)
		tmp = (s32)(ii >> brsh);
	else
		tmp = (s32)(ii << -brsh);
	if (tmp == 0)
		return B43_RX_IQ_COMP_NO_B;
	b /= tmp;
	b -= a * a;
	b = ((s32)int_sqrt(4 * (unsigned long)b) + 1) / 2;
	b -= (1 << 10);

	*a_out = a;
	*b_out = b;
	return B43_RX_IQ_COMP_OK;
}

#define B43_RXCAL_GAIN_THRESH_PWR	10000

void b43_rxcal_gain_init(struct b43_rxcal_gain *g, int idx, int max)
{
	*g = (struct b43_rxcal_gain){ .idx = idx, .max = max };
}

/* One measurement of gain g->idx: true when g->optim is found */
bool b43_rxcal_gain_step(struct b43_rxcal_gain *g, u32 pwr)
{
	bool above = pwr > B43_RXCAL_GAIN_THRESH_PWR;
	bool done = false;

	if (!g->dirn) {
		g->dirn = above ? -1 : 1;
		g->prev = g->idx;
		g->idx += g->dirn;
	} else if (g->dirn > 0 && above) {
		done = true;
		g->optim_pwr = g->prev_pwr;
		g->optim = g->prev;
	} else if (g->dirn < 0 && !above) {
		done = true;
		g->optim_pwr = pwr;
		g->optim = g->idx;
	} else {
		g->prev = g->idx;
		g->idx += g->dirn;
	}

	if (g->idx < 0 || g->idx > g->max) {
		done = true;
		g->optim_pwr = pwr;
		g->optim = g->prev;
	} else {
		g->prev_pwr = pwr;
	}
	return done;
}

/* How many doublings the power found misses 2^13 by */
int b43_rxcal_gain_delta(const struct b43_rxcal_gain *g)
{
	return 13 - b43_nbits(g->optim_pwr);
}

/* The LPF's biq1 moved by @delta, biq0 and biq1 together at most 10 */
u16 b43_rxcal_gain_biq1(u16 biq0, u16 biq1, int delta)
{
	int fine = biq1 + delta;

	if (fine + biq0 > 10)
		return 10 - biq0;
	return max(fine, 0);
}
