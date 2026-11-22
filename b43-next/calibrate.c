// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom B43 wireless driver
 *
 * What the PHYs' calibrations share: the tones they play, and the
 * arithmetic of their results.
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include "calibrate.h"

struct cordic_iq b43_tone_sample(s32 angle, u16 amp)
{
	struct cordic_iq s = cordic_calc_iq(CORDIC_FLOAT(angle));

	s.i = CORDIC_FLOAT(s.i * amp);
	s.q = CORDIC_FLOAT(s.q * amp);
	return s;
}

u32 b43_tone_pwr(u32 ii, u32 qq, u32 samps)
{
	return DIV_ROUND_CLOSEST(ii, samps) + DIV_ROUND_CLOSEST(qq, samps);
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

/* brcmsmac's ladders: percent of the bbmult, index */
static const u8 b43_txcal_ladder_lo[B43_TXCAL_LADDER_STEPS][2] = {
	{ 3, 0 }, { 4, 0 }, { 6, 0 }, { 9, 0 }, { 13, 0 }, { 18, 0 },
	{ 25, 0 }, { 25, 1 }, { 25, 2 }, { 25, 3 }, { 25, 4 }, { 25, 5 },
	{ 25, 6 }, { 25, 7 }, { 35, 7 }, { 50, 7 }, { 71, 7 }, { 100, 7 },
};

static const u8 b43_txcal_ladder_iq[B43_TXCAL_LADDER_STEPS][2] = {
	{ 3, 0 }, { 4, 0 }, { 6, 0 }, { 9, 0 }, { 13, 0 }, { 18, 0 },
	{ 25, 0 }, { 35, 0 }, { 50, 0 }, { 71, 0 }, { 100, 0 }, { 100, 1 },
	{ 100, 2 }, { 100, 3 }, { 100, 4 }, { 100, 5 }, { 100, 6 }, { 100, 7 },
};

void b43_txcal_ladder(unsigned int i, u16 bbmult, u16 *lo, u16 *iq)
{
	u16 lo_gain = b43_txcal_ladder_lo[i][0] * bbmult / 100;
	u16 iq_gain = b43_txcal_ladder_iq[i][0] * bbmult / 100;

	*lo = (lo_gain & 0xff) << 8 | b43_txcal_ladder_lo[i][1];
	*iq = (iq_gain & 0xff) << 8 | b43_txcal_ladder_iq[i][1];
}

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

/* Angle in degrees Q16 -> (cos, sin) in Q16. |theta| stays well under 90. */
static void b43_cordic_rotate(s32 theta, s32 *cos_out, s32 *sin_out)
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
static s32 b43_cordic_vector(s32 y, s32 x)
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
static u32 b43_sqrt_near(u64 v)
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

/*
 * One tone's IQ mismatch in the stock driver's form: the angle in degrees
 * Q16 and the gain ratio 2^10 * sqrt(qq / ii), both from the three
 * estimator accumulators and in its fixed point.
 *
 * sin(angle) = -iq / sqrt(ii * qq) in Q16, the root as the product of the
 * two roots taken on the accumulators brought to 30 bits; cos from
 * 1 - sin^2 in Q30; the angle is the inverse CORDIC of (sin, cos). The
 * gain ratio is sqrt(2^20 + 2^20 * (qq - ii) / ii), the quotient taken on
 * qq - ii brought to 30 bits over ii cut by the same even shift: that cut
 * of the denominator is what the per-tone ratio carries into the rounding
 * of the magnitude, a few hundredths at most, which decided the last
 * handful of coefficients.
 */
void b43_iq_mismatch(s32 iq, u32 ii, u32 qq, s32 *angle_out, u32 *mag_out)
{
	unsigned int nb_iq = fls(abs(iq)), nb_max = fls(ii | qq);
	unsigned int nb;
	s64 num, den, prod;
	s32 sin_q16, half, cos_q16, d;

	if (nb_max < 31)
		prod = (s64)b43_sqrt_near((u64)qq << (30 - nb_max)) *
		       b43_sqrt_near((u64)ii << (30 - nb_max));
	else
		prod = (s64)b43_sqrt_near(qq >> (nb_max - 30)) *
		       b43_sqrt_near(ii >> (nb_max - 30));
	num = (s64)-iq << (30 - nb_iq);
	if (nb_iq + 16 < nb_max)
		den = prod << (nb_max - nb_iq - 16);
	else
		den = prod >> (nb_iq + 16 - nb_max);
	sin_q16 = den ? b43_div_near(num, den) : 0;
	half = sin_q16 >> 1;
	cos_q16 = b43_sqrt_near((1 << 30) - (s64)half * half) << 1;
	*angle_out = b43_cordic_vector(sin_q16, cos_q16);

	d = (s32)(qq - ii);
	nb = fls(abs(d));
	nb += nb & 1;
	num = (s64)d << (30 - nb);
	den = nb >= 11 ? ii >> (nb - 10) : (u64)ii << (10 - nb);
	*mag_out = den ? b43_sqrt_near(b43_div_near(num, den) +
					      (1 << 20))
		       : 1 << 10;
}

/*
 * The RX I/Q compensation of a mismatch in b43_iq_mismatch()'s form: a is
 * the magnitude times the sine of the angle, b + 2^10 the magnitude times
 * its cosine, each rounded to nearest.
 */
void b43_rx_iq_comp_mismatch(s32 angle, u32 mag, s16 *a_out, s16 *b_out)
{
	s32 cos_q16, sin_q16;

	b43_cordic_rotate(angle, &cos_q16, &sin_q16);
	*a_out = (s16)(((s64)mag * sin_q16 + (1 << 15)) >> 16);
	*b_out = (s16)((((s64)mag * cos_q16 + (1 << 15)) >> 16) - (1 << 10));
}
