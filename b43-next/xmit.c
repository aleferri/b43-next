// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver

  Transmission (TX/RX) related functions.

  Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (C) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (C) 2005, 2006 Michael Buesch <m@bues.ch>
  Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (C) 2026 Alessio Ferri <alessio.ferri.3012@gmail.com>
  Copyright (C) 2026 Andreas Wendleder <andreas.wendleder@proton.me>
  Copyright (C) 2026 Ville Takio <ville+git@takio.fi>


*/

/*
 * HT packet-format and timing algorithms are adapted from brcmsmac's
 * main.c, rate.c and d11.h, distributed under the following ISC notice:
 *
 * Copyright (c) 2010 Broadcom Corporation
 * Copyright (c) 2013 Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "xmit.h"
#include "phy_common.h"
#include "phy_ac.h"
#include "dma.h"
#include "pio.h"
#include <linux/ktime.h>
#include <linux/unaligned.h>

/*
 * The OFDM rates: the legacy ones and those of the HT and VHT MCS, one for
 * each modulation and coding, in the order of the legacy rates. The coded
 * bits per subcarrier and the coding rate give the data bits per symbol;
 * an HT or VHT frame's response goes at the legacy rate of its row, its
 * non-HT reference rate; a legacy rate's L-SIG carries its RATE code; PHY
 * control word 1 of the pre-AC TX header takes the modulation and coding.
 */
struct b43_ofdm_rate {
	u8 bpscs;
	u8 num, den;
	u8 legacy;		/* B43_OFDM_RATE_* */
	u8 signal;		/* of the legacy rates */
	u16 phy_ctl1;
};

#define OFDM(_bpscs, _num, _den, _modul, _legacy, _signal) {		\
	.bpscs = _bpscs, .num = _num, .den = _den,			\
	.legacy = B43_OFDM_RATE_##_legacy, .signal = _signal,		\
	.phy_ctl1 = B43_TXH_PHY1_MODUL_##_modul |			\
		    B43_TXH_PHY1_CRATE_##_num##_##_den }
static const struct b43_ofdm_rate b43_ofdm_rates[] = {
	OFDM(1, 1, 2, BPSK, 6MB, 0xB),
	OFDM(1, 3, 4, BPSK, 9MB, 0xF),
	OFDM(2, 1, 2, QPSK, 12MB, 0xA),
	OFDM(2, 3, 4, QPSK, 18MB, 0xE),
	OFDM(4, 1, 2, QAM16, 24MB, 0x9),
	OFDM(4, 3, 4, QAM16, 36MB, 0xD),
	OFDM(6, 2, 3, QAM64, 48MB, 0x8),
	OFDM(6, 3, 4, QAM64, 54MB, 0xC),
	OFDM(6, 5, 6, QAM64, 54MB, 0),
	OFDM(8, 3, 4, QAM256, 54MB, 0),
	OFDM(8, 5, 6, QAM256, 54MB, 0),
};
#undef OFDM

#define B43_OFDM_LEGACY_RATES	8

/* The rows of HT MCS 0-7 and VHT MCS 0-9: all but 9 Mbit/s's. */
static const u8 b43_mcs_ofdm_rates[10] = { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

/* A legacy OFDM rate, B43_OFDM_RATE_*. */
static const struct b43_ofdm_rate *b43_ofdm_rate(u8 rate)
{
	unsigned int i;

	for (i = 0; i < B43_OFDM_LEGACY_RATES; i++)
		if (b43_ofdm_rates[i].legacy == rate)
			return &b43_ofdm_rates[i];
	B43_WARN_ON(1);
	return &b43_ofdm_rates[0];
}

/* The OFDM rate of an HT MCS within its stream, or of a VHT MCS. */
static const struct b43_ofdm_rate *b43_mcs_ofdm_rate(u8 mcs)
{
	return &b43_ofdm_rates[b43_mcs_ofdm_rates[mcs]];
}

/* Data bits per symbol on @nsd data subcarriers and @nss streams */
static unsigned int b43_ofdm_ndbps(const struct b43_ofdm_rate *r,
				   unsigned int nsd, unsigned int nss)
{
	return nsd * nss * r->bpscs * r->num / r->den;
}

/* The CCK rates, with the SIGNAL of their PLCP header. */
struct b43_cck_rate {
	u8 rate;		/* B43_CCK_RATE_* */
	u8 signal;
};

static const struct b43_cck_rate b43_cck_rates[] = {
	{ B43_CCK_RATE_1MB, 0x0A },
	{ B43_CCK_RATE_2MB, 0x14 },
	{ B43_CCK_RATE_5MB, 0x37 },
	{ B43_CCK_RATE_11MB, 0x6E },
};

static unsigned int b43_cck_rate_idx(u8 rate)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b43_cck_rates); i++)
		if (b43_cck_rates[i].rate == rate)
			return i;
	B43_WARN_ON(1);
	return 0;
}

/* Extract the bitrate index out of a CCK PLCP header. */
static int b43_plcp_get_bitrate_idx_cck(struct b43_plcp_hdr6 *plcp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b43_cck_rates); i++)
		if (b43_cck_rates[i].signal == plcp->raw[0])
			return i;
	return -1;
}

/* Extract the bitrate index out of an OFDM PLCP header. */
static int b43_plcp_get_bitrate_idx_ofdm(struct b43_plcp_hdr6 *plcp, bool ghz5)
{
	/* For 2 GHz band first OFDM rate is at index 4, see main.c */
	int base = ghz5 ? 0 : ARRAY_SIZE(b43_cck_rates);
	unsigned int i;

	for (i = 0; i < B43_OFDM_LEGACY_RATES; i++)
		if (b43_ofdm_rates[i].signal == (plcp->raw[0] & 0xF))
			return base + i;
	return -1;
}

u8 b43_plcp_get_ratecode_cck(const u8 bitrate)
{
	return b43_cck_rates[b43_cck_rate_idx(bitrate)].signal;
}

u8 b43_plcp_get_ratecode_ofdm(const u8 bitrate)
{
	return b43_ofdm_rate(bitrate)->signal;
}

void b43_generate_plcp_hdr(struct b43_plcp_hdr4 *plcp,
			   const u16 octets, const u8 bitrate)
{
	__u8 *raw = plcp->raw;

	if (b43_is_ofdm_rate(bitrate)) {
		u32 d;

		d = b43_plcp_get_ratecode_ofdm(bitrate);
		B43_WARN_ON(octets & 0xF000);
		d |= (octets << 5);
		plcp->data = cpu_to_le32(d);
	} else {
		u32 plen;

		plen = octets * 16 / bitrate;
		if ((octets * 16 % bitrate) > 0) {
			plen++;
			if ((bitrate == B43_CCK_RATE_11MB)
			    && ((octets * 8 % 11) < 4)) {
				raw[1] = 0x84;
			} else
				raw[1] = 0x04;
		} else
			raw[1] = 0x04;
		plcp->data |= cpu_to_le32(plen << 16);
		raw[0] = b43_plcp_get_ratecode_cck(bitrate);
	}
}

/*
 * One rate of a frame as the device sends it (b43_tx_rate_get()). @rate is
 * the B43_*_RATE_* of a legacy rate, the MCS of an HT one, the MCS of a
 * stream of a VHT one; @bw 0 for 20 MHz, 1 for 40, 2 for 80.
 */
enum b43_tx_enc {
	B43_TX_CCK,
	B43_TX_OFDM,
	B43_TX_HT,
	B43_TX_VHT,
};

struct b43_tx_rate {
	enum b43_tx_enc enc;
	u8 rate;
	u8 nss;
	u8 bw;
	bool sgi;
	bool stbc;
	bool ldpc;
	bool short_preamble;
	const struct b43_ofdm_rate *ofdm;	/* all but CCK */
};

/* A legacy rate, B43_*_RATE_*: the short preamble is CCK's, not 1 Mbit/s's. */
static void b43_tx_rate_legacy(struct b43_tx_rate *r, u8 rate,
			       bool short_preamble)
{
	memset(r, 0, sizeof(*r));
	r->rate = rate;
	r->nss = 1;
	if (b43_is_ofdm_rate(rate)) {
		r->enc = B43_TX_OFDM;
		r->ofdm = b43_ofdm_rate(rate);
	} else {
		r->enc = B43_TX_CCK;
		r->short_preamble = short_preamble && rate != B43_CCK_RATE_1MB;
	}
}

/* Whether @t is an HT MCS on the streams of the band's TX MCS set. */
static bool b43_tx_ht_ok(const struct ieee80211_supported_band *band,
			 const struct ieee80211_tx_rate *t)
{
	const struct ieee80211_mcs_info *mcs = &band->ht_cap.mcs;
	unsigned int nss = t->idx / 8 + 1;

	if (!(mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED) || t->idx > 31)
		return false;
	if ((mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF) &&
	    nss > ((mcs->tx_params & IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK) >>
		   IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT) + 1)
		return false;
	return mcs->rx_mask[t->idx / 8] & BIT(t->idx % 8);
}

/*
 * Whether @t is a VHT MCS 0-9 at 20, 40 or 80 MHz on a stream count the
 * band's TX map has.
 */
static bool b43_tx_vht_ok(const struct ieee80211_supported_band *band,
			  const struct ieee80211_tx_rate *t)
{
	u8 nss = ieee80211_rate_get_vht_nss(t);
	u16 map;

	if (!band->vht_cap.vht_supported)
		return false;
	map = le16_to_cpu(band->vht_cap.vht_mcs.tx_mcs_map);
	return ((map >> (2 * (nss - 1))) & 3) !=
		IEEE80211_VHT_MCS_NOT_SUPPORTED &&
	       ieee80211_rate_get_vht_mcs(t) <= 9 &&
	       !(t->flags & IEEE80211_TX_RC_160_MHZ_WIDTH);
}

/* The width mac80211 asks for, as b43_tx_rate's bw. */
static u8 b43_tx_rate_bw(const struct ieee80211_tx_rate *t)
{
	if (t->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		return 2;
	if (t->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		return 1;
	return 0;
}

/* The channel's width, as b43_tx_rate's bw. */
static u8 b43_tx_chan_bw(struct b43_wldev *dev)
{
	switch (dev->phy.chandef->width) {
	case NL80211_CHAN_WIDTH_40:
		return 1;
	case NL80211_CHAN_WIDTH_80:
		return 2;
	default:
		return 0;
	}
}

/*
 * @t of @info as the device sends it, from the microcode's TX header
 * (dev->fw.layout.hdr: STBC, LDPC), the core (its TX chains, its channel's
 * width) and the configuration (the band's rates and TX MCS sets); false
 * when the header cannot carry it.
 */
static bool b43_tx_rate_get(struct b43_wldev *dev,
			    const struct ieee80211_tx_info *info,
			    const struct ieee80211_tx_rate *t,
			    struct b43_tx_rate *r)
{
	const struct ieee80211_supported_band *band =
		dev->wl->hw->wiphy->bands[info->band];
	const struct b43_fw_hdr *fh = dev->fw.layout.hdr;

	if (t->idx < 0 || !band)
		return false;
	if (!(t->flags & (IEEE80211_TX_RC_MCS | IEEE80211_TX_RC_VHT_MCS))) {
		if (t->idx >= band->n_bitrates)
			return false;
		b43_tx_rate_legacy(r, band->bitrates[t->idx].hw_value,
				   t->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE);
		return true;
	}

	memset(r, 0, sizeof(*r));
	if (t->flags & IEEE80211_TX_RC_VHT_MCS) {
		if (!b43_tx_vht_ok(band, t))
			return false;
		r->enc = B43_TX_VHT;
		r->rate = ieee80211_rate_get_vht_mcs(t);
		r->nss = ieee80211_rate_get_vht_nss(t);
		r->ofdm = b43_mcs_ofdm_rate(r->rate);
	} else {
		if (!b43_tx_ht_ok(band, t))
			return false;
		r->enc = B43_TX_HT;
		r->rate = t->idx;
		r->nss = t->idx / 8 + 1;
		r->ofdm = b43_mcs_ofdm_rate(t->idx % 8);
	}
	r->bw = min(b43_tx_rate_bw(t), b43_tx_chan_bw(dev));
	r->sgi = t->flags & IEEE80211_TX_RC_SHORT_GI;
	/* One stream as two space-time streams, one per TX core */
	r->stbc = fh && fh->tx_stbc && r->enc == B43_TX_HT && r->nss == 1 &&
		  hweight8(dev->txchain) > 1 &&
		  (info->flags & IEEE80211_TX_CTL_STBC);
	r->ldpc = fh && fh->tx_ldpc && (info->flags & IEEE80211_TX_CTL_LDPC);
	return true;
}

/*
 * Whether the TX header can carry the frame's rates: the first, and on the
 * pre-AC header the fallback too; the AC descriptor ends its rate blocks at
 * one it cannot carry (b43_generate_txhdr_ac()). Rate control picks
 * nothing else, see b43_ac_set_ht_vht_cap() and b43_setup_bands(), but an
 * injected frame can ask for any rate.
 */
bool b43_tx_rates_ok(struct b43_wldev *dev,
		     const struct ieee80211_tx_info *info)
{
	const struct ieee80211_tx_rate *rates = info->control.rates;
	const struct b43_fw_hdr *fh;
	struct b43_tx_rate r;

	if (!dev || !b43_tx_rate_get(dev, info, &rates[0], &r))
		return false;
	fh = dev->fw.layout.hdr;
	return (fh && fh->tx_rates_cut) || rates[1].idx < 0 ||
	       b43_tx_rate_get(dev, info, &rates[1], &r);
}

/* Data bits per symbol */
static unsigned int b43_tx_ndbps(const struct b43_tx_rate *r)
{
	static const u8 nsd[] = { 52, 108, 234 };

	if (r->enc == B43_TX_OFDM)
		return b43_ofdm_ndbps(r->ofdm, 48, 1);
	return b43_ofdm_ndbps(r->ofdm, nsd[r->bw], r->nss);
}

/* The data of an HT rate in 4 us periods: 802.11n rounds short-GI (3.6 us)
 * symbols up to a 4 us boundary for TXTIME and the L-SIG length. STBC
 * sends an even number of symbols.
 */
static unsigned int b43_tx_ht_periods(const struct b43_tx_rate *r,
				      unsigned int octets)
{
	unsigned int m = r->stbc ? 2 : 1;
	unsigned int symbols = m * DIV_ROUND_UP(16 + 8 * octets + 6,
						m * b43_tx_ndbps(r));

	return r->sgi ? DIV_ROUND_UP(9 * symbols, 10) : symbols;
}

/* The HT-LTFs: one per space-time stream, four for three. */
static unsigned int b43_tx_ht_ltfs(const struct b43_tx_rate *r)
{
	unsigned int nsts = r->nss + r->stbc;

	return nsts == 3 ? 4 : nsts;
}

/* Mixed-mode L-SIG length: the octets 6 Mbit/s would carry after the
 * L-SIG, three per 4 us of HT-SIG, HT-STF, HT-LTFs and data, less three
 * for the service and tail bits.
 */
static unsigned int b43_tx_lsig_len(const struct b43_tx_rate *r,
				    unsigned int octets)
{
	return 3 * (b43_tx_ht_periods(r, octets) + b43_tx_ht_ltfs(r) + 3) - 3;
}

/* Airtime of a legacy or HT rate, with the FCS/trailer in octets but not
 * SIFS.
 */
static unsigned int b43_tx_airtime(const struct b43_tx_rate *r,
				   unsigned int octets, bool gmode)
{
	switch (r->enc) {
	case B43_TX_CCK:
		return (r->short_preamble ? 96 : 192) +
		       DIV_ROUND_UP(16 * octets, r->rate);
	case B43_TX_OFDM:
		return 20 + 4 * DIV_ROUND_UP(16 + 8 * octets + 6,
					     b43_tx_ndbps(r)) +
		       (gmode ? 6 : 0);
	default:
		return 36 + 4 * (b43_tx_ht_ltfs(r) - 1) +
		       4 * b43_tx_ht_periods(r, octets) + (gmode ? 6 : 0);
	}
}

/*
 * The SIG of an HT or VHT rate: the first four bytes of the HT-SIG (the
 * MCS, 40 MHz, the length, smoothing, not sounding and the reserved bit
 * set, aggregation, STBC, LDPC, the short guard interval), or VHT-SIG-A1
 * and A2 (group ID 63 and both reserved bits set, the SGI disambiguation
 * and LDPC extra-symbol bits left clear).
 */
static void b43_tx_sig(const struct b43_tx_rate *r, unsigned int len,
		       bool aggregation, u8 *sig)
{
	u32 a1, a2;

	if (r->enc == B43_TX_HT) {
		sig[0] = r->rate | (r->bw ? 0x80 : 0);
		put_unaligned_le16(len, sig + 1);
		sig[3] = 0x07 | (aggregation ? 0x08 : 0) |
			 (r->stbc ? 0x10 : 0) | (r->ldpc ? 0x40 : 0) |
			 (r->sgi ? 0x80 : 0);
		return;
	}
	a1 = r->bw | BIT(2) | (63 << 4) | (r->nss - 1) << 10 | BIT(23);
	a2 = (r->sgi ? BIT(0) : 0) | (r->ldpc ? BIT(2) : 0) |
	     (r->rate << 4) | BIT(9);
	put_unaligned_le24(a1, sig);
	put_unaligned_le24(a2, sig + 3);
}

/* The PLCP header of a rate of the pre-AC header */
static void b43_tx_plcp(const struct b43_tx_rate *r, unsigned int len,
			bool aggregation, struct b43_plcp_hdr6 *plcp)
{
	if (r->enc == B43_TX_HT)
		b43_tx_sig(r, len, aggregation, plcp->raw);
	else
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)plcp, len,
				      r->rate);
}

/* The legacy rate an MCS's response goes at, or a legacy rate's own. */
static u8 b43_tx_ref_rate(const struct b43_tx_rate *r)
{
	return r->enc == B43_TX_HT || r->enc == B43_TX_VHT ?
	       r->ofdm->legacy : r->rate;
}

static u8 b43_tx_response_rate(struct b43_wldev *dev,
			       struct ieee80211_tx_info *info,
			       const struct b43_tx_rate *r, bool protection)
{
	struct ieee80211_supported_band *sband =
		dev->wl->hw->wiphy->bands[info->band];
	struct ieee80211_vif *vif = info->control.vif;
	u8 response = 0;
	int i;
	u8 target = b43_tx_ref_rate(r);

	if (protection && vif && vif->bss_conf.use_cts_prot)
		target = B43_CCK_RATE_11MB;
	/* Keep the modulation class of the target, after ERP protection. */
	for (i = 0; vif && i < sband->n_bitrates; i++) {
		u8 candidate = sband->bitrates[i].hw_value;

		if ((vif->bss_conf.basic_rates & BIT(i)) &&
		    b43_is_ofdm_rate(candidate) == b43_is_ofdm_rate(target) &&
		    candidate <= target && candidate > response)
			response = candidate;
	}
	if (!response) {
		/* No basic rate fits: a mandatory rate of the same class. */
		if (b43_is_cck_rate(target))
			response = target;
		else if (target >= B43_OFDM_RATE_24MB)
			response = B43_OFDM_RATE_24MB;
		else if (target >= B43_OFDM_RATE_12MB)
			response = B43_OFDM_RATE_12MB;
		else
			response = B43_OFDM_RATE_6MB;
	}
	return response;
}

/* A response or protection frame, legacy, with the BSS's preamble. */
static void b43_tx_rate_bss(struct b43_tx_rate *r, u8 rate,
			    const struct ieee80211_vif *vif)
{
	b43_tx_rate_legacy(r, rate, vif && vif->bss_conf.use_short_preamble);
}

/* The response is a 14-byte ACK, or a 32-byte compressed Block Ack. */
static unsigned int b43_tx_nav(struct b43_wldev *dev,
			       struct ieee80211_tx_info *info,
			       const struct b43_tx_rate *r,
			       unsigned int next_len, bool block_ack)
{
	struct b43_tx_rate response;
	unsigned int sifs = dev->phy.gmode ? 10 : 16;
	unsigned int nav;

	b43_tx_rate_bss(&response, b43_tx_response_rate(dev, info, r, false),
			info->control.vif);
	nav = sifs + b43_tx_airtime(&response, block_ack ? 32 : 14,
				    dev->phy.gmode);
	if (next_len)
		nav = 2 * nav + sifs +
		      b43_tx_airtime(r, next_len, dev->phy.gmode);
	return nav;
}

/*
 * Protect HT frames with RTS/CTS when the BSS uses ERP protection or HT
 * protection mode 1 (non-member) or 3 (non-HT mixed). Mode 2, 20 MHz
 * protection, only covers 40 MHz PPDUs, which 20 MHz-only HT stations
 * cannot receive. Before b43_txhdr_ctl_get(): a protected frame also gets
 * the long retry limit.
 */
static void b43_tx_ht_protect(struct b43_wldev *dev,
			      struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *rates = info->control.rates;
	struct ieee80211_vif *vif = info->control.vif;
	bool bw40 = b43_is_40mhz(dev) &&
		    ((rates[0].flags & IEEE80211_TX_RC_40_MHZ_WIDTH) ||
		     (rates[1].idx >= 0 &&
		      (rates[1].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)));
	u16 ht_prot;
	int i;

	if (!(rates[0].flags & IEEE80211_TX_RC_MCS) &&
	    !(rates[1].idx >= 0 && (rates[1].flags & IEEE80211_TX_RC_MCS)))
		return;
	if (!vif || (info->flags & IEEE80211_TX_CTL_NO_ACK))
		return;
	ht_prot = vif->bss_conf.ht_operation_mode &
		  IEEE80211_HT_OP_MODE_PROTECTION;
	if (vif->bss_conf.use_cts_prot ||
	    ht_prot == IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER ||
	    ht_prot == IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED ||
	    (ht_prot == IEEE80211_HT_OP_MODE_PROTECTION_20MHZ && bw40))
		rates[0].flags |= IEEE80211_TX_RC_USE_RTS_CTS;
	/* So is an HT PPDU when any retry stage asks for RTS/CTS. minstrel_ht
	 * asks on its fallback stages, so every A-MPDU starts with an RTS;
	 * unprotected A-MPDUs raise the PSM watchdog more often. */
	for (i = 1; i < IEEE80211_TX_MAX_RATES && rates[i].idx >= 0; i++)
		if (rates[i].flags & IEEE80211_TX_RC_USE_RTS_CTS)
			rates[0].flags |= IEEE80211_TX_RC_USE_RTS_CTS;
}

/*
 * PHY control word 1 of the pre-AC header. A legacy OFDM rate at 40 MHz
 * (an RTS or CTS ahead of a 40 MHz frame) goes on both halves.
 */
static u16 b43_generate_tx_phy_ctl1(struct b43_wldev *dev,
				    const struct b43_tx_rate *r)
{
	const struct b43_phy *phy = &dev->phy;
	u16 control;

	/* On a 40 MHz channel, send 20 MHz frames on the control channel. */
	if (!b43_is_40mhz(dev))
		control = B43_TXH_PHY1_BW_20;
	else if (r->bw && r->enc == B43_TX_HT)
		control = B43_TXH_PHY1_BW_40;
	else if (r->bw && r->enc == B43_TX_OFDM)
		control = B43_TXH_PHY1_BW_40DUP;
	else if (phy->chandef->chan->center_freq > phy->chandef->center_freq1)
		control = B43_TXH_PHY1_BW_20U;
	else
		control = B43_TXH_PHY1_BW_20;

	switch (r->enc) {
	case B43_TX_CCK:
		/* The LP-PHY's CCK rates add their index, as upstream b43. */
		if (phy->type == B43_PHYTYPE_LP)
			control |= b43_cck_rate_idx(r->rate);
		return control;
	case B43_TX_OFDM:
		return control | r->ofdm->phy_ctl1 | B43_TXH_PHY1_MODE_SISO;
	default:
		control |= r->ofdm->phy_ctl1;
		if (r->nss > 1)
			return control | B43_TXH_PHY1_MODE_SDM |
			       (r->nss - 1) << B43_TXH_PHY1_NSS_SHIFT;
		if (r->stbc)
			return control | B43_TXH_PHY1_MODE_STBC;
		if (hweight8(dev->txchain) == 1)
			return control | B43_TXH_PHY1_MODE_SISO;
		return control | B43_TXH_PHY1_MODE_CDD;
	}
}

static u8 b43_calc_fallback_rate(u8 bitrate, int gmode)
{
	switch (bitrate) {
	case B43_CCK_RATE_1MB:
		return B43_CCK_RATE_1MB;
	case B43_CCK_RATE_2MB:
		return B43_CCK_RATE_1MB;
	case B43_CCK_RATE_5MB:
		return B43_CCK_RATE_2MB;
	case B43_CCK_RATE_11MB:
		return B43_CCK_RATE_5MB;
	/*
	 * Don't just fallback to CCK; it may be in 5GHz operation
	 * and falling back to CCK won't work out very well.
	 */
	case B43_OFDM_RATE_6MB:
		if (gmode)
			return B43_CCK_RATE_5MB;
		else
			return B43_OFDM_RATE_6MB;
	case B43_OFDM_RATE_9MB:
		return B43_OFDM_RATE_6MB;
	case B43_OFDM_RATE_12MB:
		return B43_OFDM_RATE_9MB;
	case B43_OFDM_RATE_18MB:
		return B43_OFDM_RATE_12MB;
	case B43_OFDM_RATE_24MB:
		return B43_OFDM_RATE_18MB;
	case B43_OFDM_RATE_36MB:
		return B43_OFDM_RATE_24MB;
	case B43_OFDM_RATE_48MB:
		return B43_OFDM_RATE_36MB;
	case B43_OFDM_RATE_54MB:
		return B43_OFDM_RATE_48MB;
	}
	B43_WARN_ON(1);
	return 0;
}

/* Generate a TX data header. */
/* Index of a legacy rate in PHY TX control word 2 of the AC microcode. */
static u16 b43_txhdr_ac_rate_idx(u8 rate)
{
	if (b43_is_cck_rate(rate))
		return b43_cck_rate_idx(rate);
	return b43_ofdm_rate(rate) - b43_ofdm_rates;
}

/*
 * What the TX header formats share, decided once from mac80211's TX info:
 * the rates, the MAC flags and the protection frame. b43_generate_txhdr_fw4()
 * writes it in the pre-AC header, b43_generate_txhdr_ac() in the AC one.
 */
struct b43_txhdr_ctl {
	u8 rts_rate;		/* hw_value of the RTS or CTS rate */
	bool ack;		/* expect an ACK */
	bool hwseq;		/* hardware sequence number */
	bool first_frag;	/* first fragment of an MSDU */
	bool long_frame;	/* long retry limit */
	bool rts;		/* RTS before the frame */
	bool cts;		/* CTS-to-self before the frame */
};

static void b43_txhdr_ctl_get(struct b43_wldev *dev,
			      struct ieee80211_tx_info *info,
			      struct b43_txhdr_ctl *c)
{
	struct ieee80211_tx_rate *rates = info->control.rates;
	struct ieee80211_rate *rts_cts_rate;

	c->ack = !(info->flags & IEEE80211_TX_CTL_NO_ACK);
	/* use hardware sequence counter as the non-TID counter */
	c->hwseq = info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ;
	c->first_frag = info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT;

	/* Overwrite rates[0].count to make the retry calculation
	 * in the tx status easier. need the actual retry limit to
	 * detect whether the fallback rate was used.
	 */
	c->long_frame = (rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
			(rates[0].count <=
			 dev->wl->hw->conf.long_frame_max_tx_count);
	if (c->long_frame)
		rates[0].count = B43_LONG_FALLBACK_LIMIT;
	else
		rates[0].count = B43_SHORT_FALLBACK_LIMIT;

	c->cts = rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT;
	c->rts = !c->cts && (rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS);
	c->rts_rate = B43_CCK_RATE_1MB;
	if (c->rts || c->cts) {
		rts_cts_rate = ieee80211_get_rts_cts_rate(dev->wl->hw, info);
		if (rts_cts_rate)
			c->rts_rate = rts_cts_rate->hw_value;
	}
}

/*
 * The cores of a rate class, word 0's mask: the cell of the microcode's TX
 * core table, as wlc_stf_txcore_get() picks it for the stock descriptors.
 */
static u16 b43_txhdr_ac_cores(const struct b43_phy_ac *ac,
			      enum b43_phy_ac_txcore class)
{
	return (ac->txcore[class] ? : ac->coremask)
		<< B43_TXH_AC_PHY0_CORES_SHIFT;
}

/*
 * Word 1's subband: the primary 20 MHz within a frame of width @bw, 0 20
 * MHz, 1 40, 2 80, as wlc_acphy_txctl1_calc() shifts the chanspec's sideband.
 */
static u16 b43_txhdr_ac_subband(const struct b43_phy_ac *ac, unsigned int bw)
{
	return ((ac->chanspec & B43_PHY_AC_CHANSPEC_SB_MASK) >>
		B43_PHY_AC_CHANSPEC_SB_SHIFT) >> bw;
}

/*
 * Under thermal throttling TX has one chain (b43_phy_ac_temp_throttle()),
 * which carries one stream: each MCS goes out on one at the same index,
 * and its status reports that rate.
 */
static void b43_txhdr_ac_one_stream(struct ieee80211_tx_rate *rates)
{
	unsigned int i;

	for (i = 0; i < IEEE80211_TX_MAX_RATES && rates[i].idx >= 0; i++) {
		struct ieee80211_tx_rate *r = &rates[i];
		u8 mcs = ieee80211_rate_get_vht_mcs(r);

		if (r->flags & IEEE80211_TX_RC_VHT_MCS)
			ieee80211_rate_set_vht(r, mcs, 1);
		else if (r->flags & IEEE80211_TX_RC_MCS)
			r->idx %= 8;
	}
}

/* One legacy rate, @rate a B43_*_RATE_*, on the primary 20 MHz. */
static void b43_txhdr_ac_legacy(const struct b43_phy_ac *ac,
				struct b43_txhdr_ac_rate *r, u8 rate,
				bool short_preamble, unsigned int len)
{
	bool ofdm = b43_is_ofdm_rate(rate);
	u16 phy0, po;

	phy0 = ofdm ? B43_TXH_PHY_ENC_OFDM : B43_TXH_PHY_ENC_CCK;
	phy0 |= B43_TXH_AC_PHY0_NON_SOUNDING;
	phy0 |= b43_txhdr_ac_cores(ac, ofdm ? B43_PHY_AC_TXCORE_OFDM :
					      B43_PHY_AC_TXCORE_CCK);
	if (short_preamble)
		phy0 |= B43_TXH_AC_PHY0_SHORT_PREAMBLE;
	r->phy_ctl[0] = cpu_to_le16(phy0);
	po = ofdm ? ac->rate_po_ofdm[b43_txhdr_ac_rate_idx(rate)] :
		    ac->rate_po_cck;
	r->phy_ctl[1] = cpu_to_le16((po & B43_TXH_AC_PHY1_TXPWR_OFFSET) |
				    b43_txhdr_ac_subband(ac, 0));
	r->phy_ctl[2] = cpu_to_le16(b43_txhdr_ac_rate_idx(rate));
	b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)&r->plcp, len, rate);
	r->tx_rate = cpu_to_le16(rate);
}

/*
 * One HT rate, as the DSL-3580L's 6.30 lays it out (wlc_acphy_txctl0/1/2_calc(),
 * wlc_compute_plcp()) and as gonsolo's port sends it under 832.127 on MCS
 * 0-15 at 20 MHz: frame type 2 and the cores of the stream count's class in
 * word 0; word 1 as on VHT, with the power offset of the VHT MCS of the same
 * modulation, the row 6.30's ppr_get_ht_mcs() reads; word 2 the MCS index;
 * the HT-SIG with the MCS, the length, and smoothing, not sounding and the
 * reserved bit set.
 *
 * At 40 MHz the width field and the HT-SIG's 40 MHz bit are 6.30's
 * (docs/retrace-todo.md, "HT40").
 */
static void b43_txhdr_ac_ht(const struct b43_phy_ac *ac,
			    struct b43_txhdr_ac_rate *r,
			    const struct b43_tx_rate *t, unsigned int len)
{
	unsigned int bw = t->bw;
	u8 mcs = t->rate;
	bool sgi = t->sgi;
	struct rate_info ri = {
		.flags = RATE_INFO_FLAGS_MCS |
			 (sgi ? RATE_INFO_FLAGS_SHORT_GI : 0),
		.mcs = mcs,
		.bw = bw ? RATE_INFO_BW_40 : RATE_INFO_BW_20,
	};
	u16 phy0;

	phy0 = B43_TXH_PHY_ENC_HT | B43_TXH_AC_PHY0_NON_SOUNDING |
	       b43_txhdr_ac_cores(ac, B43_PHY_AC_TXCORE_NSTS1 + mcs / 8) |
	       bw << B43_TXH_AC_PHY0_BW_SHIFT;
	r->phy_ctl[0] = cpu_to_le16(phy0);
	r->phy_ctl[1] = cpu_to_le16((ac->rate_po_vht[bw][mcs % 8] &
				     B43_TXH_AC_PHY1_TXPWR_OFFSET) |
				    b43_txhdr_ac_subband(ac, bw));
	r->phy_ctl[2] = cpu_to_le16(mcs);

	b43_tx_sig(t, len, false, r->plcp.raw);
	r->tx_rate = cpu_to_le16(cfg80211_calculate_bitrate(&ri) / 5);
}

/*
 * One VHT rate, as both stock drivers lay it out: the frames of the
 * agcombo's 7.14.43 to one and two-stream stations (router-data/vd625-agcombo)
 * and the code of the DSL-3580L's own 6.30 (wlc_acphy_txctl0/1/2_calc(),
 * wlc_compute_plcp()). Word 0 with the frame type, the cores of the stream
 * count's class and the width in 15:14; word 1 the power offset, the same
 * for one and two streams, and the primary's subband within the frame's
 * width; word 2 the MCS and NSS - 1; VHT-SIG-A1/A2 as 802.11 lays them out,
 * group ID 63 and both reserved bits set, the SGI disambiguation and LDPC
 * extra-symbol bits left clear, as the stock driver leaves them on frames of
 * every length; the PHY rate in 500 kb/s; 0x0300 in the fallback bandwidth
 * field, as on every block 7.14.43 sends.
 *
 * Word 0's bit 3 stays clear: 6.30 never sets it, and 7.14.43 sets it from
 * MCS 4 up together with a value in the beamforming field, which b43 has
 * nothing to put in.
 */
static void b43_txhdr_ac_vht(const struct b43_phy_ac *ac,
			     struct b43_txhdr_ac_rate *r,
			     const struct b43_tx_rate *t)
{
	static const u8 rate_bw[] = {
		RATE_INFO_BW_20, RATE_INFO_BW_40, RATE_INFO_BW_80,
	};
	unsigned int bw = t->bw;
	u8 mcs = t->rate, nss = t->nss;
	bool sgi = t->sgi;
	struct rate_info ri = {
		.flags = RATE_INFO_FLAGS_VHT_MCS |
			 (sgi ? RATE_INFO_FLAGS_SHORT_GI : 0),
		.mcs = mcs,
		.nss = nss,
		.bw = rate_bw[bw],
	};
	u16 phy0, phy1 = ac->rate_po_vht[bw][mcs] & B43_TXH_AC_PHY1_TXPWR_OFFSET;
	phy0 = B43_TXH_PHY_ENC_VHT | B43_TXH_AC_PHY0_NON_SOUNDING |
	       b43_txhdr_ac_cores(ac, B43_PHY_AC_TXCORE_NSTS1 + nss - 1) |
	       bw << B43_TXH_AC_PHY0_BW_SHIFT;
	r->phy_ctl[0] = cpu_to_le16(phy0);
	r->phy_ctl[1] = cpu_to_le16(phy1 | b43_txhdr_ac_subband(ac, bw));
	r->phy_ctl[2] = cpu_to_le16((mcs & B43_TXH_AC_PHY2_VHT_MCS) |
				    (nss - 1) << B43_TXH_AC_PHY2_VHT_NSS_SHIFT);

	b43_tx_sig(t, 0, false, r->plcp.raw);
	r->fbw_info = cpu_to_le16(0x0300);
	r->tx_rate = cpu_to_le16(cfg80211_calculate_bitrate(&ri) / 5);
}

/*
 * The Duration of a unicast frame sent at an MCS. mac80211 leaves it 0 and
 * to the hardware (ieee80211_duration()), and the AC microcode does not fill
 * it in: the stock driver writes it, SIFS and the ACK at the highest basic
 * rate not above the MCS's non-HT reference rate, for its last fallback rate
 * (router-data/d6220/rxtx-1s-ht20-40-vht20-40-80-txbf0.zip: 48 us after HT
 * MCS 2, 44 after VHT MCS 4). @ref is that rate's non-HT reference rate
 * (b43_tx_ref_rate()).
 */
static void b43_txhdr_ac_mcs_duration(struct b43_wldev *dev,
				      struct sk_buff *skb,
				      const struct ieee80211_tx_info *info,
				      u8 ref)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_hw *hw = dev->wl->hw;
	struct ieee80211_vif *vif = info->control.vif;
	struct ieee80211_supported_band *sband = hw->wiphy->bands[info->band];
	struct ieee80211_rate *ack = NULL;
	unsigned int i;
	u16 dur;

	if (!vif || !sband || !sband->n_bitrates ||
	    ieee80211_is_ctl(hdr->frame_control) ||
	    ieee80211_has_morefrags(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1) ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))
		return;

	for (i = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *rate = &sband->bitrates[i];

		if (!(vif->bss_conf.basic_rates & BIT(i)) ||
		    rate->bitrate > B43_RATE_TO_BASE100KBPS(ref))
			continue;
		if (!ack || rate->bitrate > ack->bitrate)
			ack = rate;
	}
	if (!ack)
		ack = &sband->bitrates[0];

	dur = info->band == NL80211_BAND_5GHZ ? 16 : 10;
	dur += le16_to_cpu(ieee80211_generic_frame_duration(hw, vif, info->band,
							      10 + FCS_LEN, ack));
	hdr->duration_id = cpu_to_le16(dur);
}

/* One rate block from one of the frame's rates. */
static void b43_txhdr_ac_block(const struct b43_phy_ac *ac,
			       struct b43_txhdr_ac_rate *r,
			       const struct b43_tx_rate *t, unsigned int len)
{
	if (t->enc == B43_TX_VHT)
		b43_txhdr_ac_vht(ac, r, t);
	else if (t->enc == B43_TX_HT)
		b43_txhdr_ac_ht(ac, r, t, len);
	else
		b43_txhdr_ac_legacy(ac, r, t->rate, t->short_preamble, len);
}

/*
 * The aggregation fields of an MPDU of a block ack session, as the agcombo's
 * 7.14.43 fills them for a one-stream VHT phone at every width
 * (router-data/vd625-agcombo/rxtx-1s-ht20-40-80.zip: 0x20 0x20 0x1526 0x3f
 * 0x14): 32 MPDUs at most at the primary rate and at the fallback; the
 * longest A-MPDU, 0x152 times 16 us, above the MPDU density in d11.h's
 * "minimum duration index", 6 (8 us); the session's window of 64 less one;
 * and the phone's longest A-MPDU, 2^(13 + 7) bytes, as the exponent. The
 * aggregation queue manager takes no minimum MPDU length
 * (b43_aqm_init_rev42()), so the density is the descriptor's. Whether 6 is
 * the phone's own or the stock driver's floor is not in the capture: the
 * station's, raised to 6, gives the spacing it asks for and the stock
 * one, at the cost of padding behind MPDUs shorter than 8 us. The window
 * is the session's (b43_ampdu_action()); without one the MPDU goes out
 * alone.
 */
static unsigned int b43_sta_ampdu_exp(const struct ieee80211_sta *sta)
{
	const struct ieee80211_sta_vht_cap *vht = &sta->deflink.vht_cap;

	if (!vht->vht_supported)
		return sta->deflink.ht_cap.ampdu_factor;
	return u32_get_bits(vht->cap,
			    IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
}

/*
 * A frame to a station with a beamforming slot (b43_txbf_link()): on each
 * VHT rate block every TX chain, the BFM bit of PHY control word 0 and the
 * rate block's last word 0x0009, as wl 7.14 sends its VHT frames to a
 * beamformee (router-data/rt-ac66u-b1/mmio-txbf-5g36-80.zip: 0x81cf and
 * 0x0009 with txbf 1, 0x81c7 and 0 without, the RTS/CTS word and VHT-SIG-A
 * the same in both), and the slot in bits 15:12 of the RTS/CTS word
 * (mmio-txbf2.zip: 0x0905 to the slot-0 station, 0x1905 to the slot-1 one).
 */
static void b43_txhdr_ac_txbf(struct b43_wldev *dev,
			      struct b43_txhdr_ac *txhdr, unsigned int n,
			      const struct ieee80211_tx_info *info,
			      const struct ieee80211_hdr *hdr)
{
	u8 txchain = dev->dev->bus_sprom->txchain & 0x07;
	struct ieee80211_sta *sta;
	unsigned int slot = 0, i;

	if (!dev->txbf_base || !info->control.vif ||
	    !ieee80211_is_data(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return;

	rcu_read_lock();
	sta = ieee80211_find_sta(info->control.vif, hdr->addr1);
	if (sta)
		slot = READ_ONCE(((struct b43_sta *)sta->drv_priv)->txbf_slot);
	rcu_read_unlock();
	if (!slot)
		return;
	if (!txchain)
		txchain = 0x03;

	for (i = 0; i < n; i++) {
		struct b43_txhdr_ac_rate *r = &txhdr->rate[i];
		u16 phy0 = le16_to_cpu(r->phy_ctl[0]);

		if ((phy0 & B43_TXH_PHY_ENC) != B43_TXH_PHY_ENC_VHT)
			continue;
		phy0 &= ~B43_TXH_AC_PHY0_CORES;
		phy0 |= txchain << B43_TXH_AC_PHY0_CORES_SHIFT |
			B43_TXH_AC_PHY0_BFM;
		r->phy_ctl[0] = cpu_to_le16(phy0);
		r->rts_cts_ctl |= cpu_to_le16((slot - 1) <<
					      B43_TXH_AC_RTS_TXBF_SLOT_SHIFT);
		r->bfm0 = cpu_to_le16(0x0009);
	}
}

static bool b43_txhdr_ac_ampdu(struct b43_txhdr_ac *txhdr,
			       const struct ieee80211_tx_info *info,
			       struct ieee80211_hdr *hdr)
{
	struct ieee80211_sta *sta;
	unsigned int win = 0, exp = 0, density = 0;

	if (!info->control.vif || !ieee80211_is_data_qos(hdr->frame_control))
		return false;

	rcu_read_lock();
	sta = ieee80211_find_sta(info->control.vif, hdr->addr1);
	if (sta) {
		const struct b43_sta *bsta = (const void *)sta->drv_priv;

		win = READ_ONCE(bsta->ampdu_win[ieee80211_get_tid(hdr)]);
		exp = b43_sta_ampdu_exp(sta);
		density = sta->deflink.ht_cap.ampdu_density;
	}
	rcu_read_unlock();
	if (!win)
		return false;

	txhdr->mpdu_max = min(win, 32U);
	txhdr->mpdu_max_fb = txhdr->mpdu_max;
	density = max_t(unsigned int, density, IEEE80211_HT_MPDU_DENSITY_8);
	txhdr->ampdu_dur = cpu_to_le16(0x152 << B43_TXH_AC_AMPDU_MAX_DUR_SHIFT |
				       (density & B43_TXH_AC_AMPDU_DENSITY));
	txhdr->ba_win = win - 1;
	txhdr->max_agg_len = IEEE80211_HT_MAX_AMPDU_FACTOR + exp;
	return true;
}

/*
 * The AC microcode's descriptor: a rate block for each of the frame's rates,
 * which the microcode tries in order, as the stock drivers fill up to four
 * on unicast data (router-data/d6220/rxtx-1s-ht20-40-vht20-40-80-txbf0.zip),
 * the last one marked in its RTS/CTS word; FIX_RATE when there is one, as on
 * their management and multicast frames. A fallback the header cannot carry
 * ends the list there, and is cut from it with the rest, so that the status
 * reports the rates tried (b43_fill_txstatus_report()).
 *
 * A frame with a hardware key carries its IV from mac80211, and the cache
 * info names the cipher and the key slot; the microcode encrypts it and
 * appends the MIC or ICV, which the frame length counts. The agcombo's
 * 7.14.43 sends CCMP so: 0x50 and the key slot in the cache info, the PN in
 * the frame's own CCMP header and none in the descriptor's, and a length 12
 * bytes over the frame (router-data/vd625-agcombo/rxtx-1s-ht20-40-80.zip).
 *
 * A VHT PPDU carries an A-MPDU. Outside an aggregate SVHT has the microcode
 * send the frame as an A-MPDU of one MPDU, as the agcombo's 7.14.43 does on
 * the unicast QoS frames it sends outside a block ack session (MAC TX
 * control 0x4080/0x0004, router-data/vd625-agcombo/rxtx-1s-ht20-40-80.zip).
 * The flag is the frame's, so a VHT frame keeps VHT fallbacks only, as the
 * stock ones are. An MPDU of a block ack session is AMPDU instead, and the
 * microcode puts it in an A-MPDU with the MPDUs posted after it.
 */
static int b43_generate_txhdr_ac(struct b43_wldev *dev, u8 *_txhdr,
				 struct sk_buff *skb,
				 struct ieee80211_tx_info *info, u16 cookie,
				 const struct b43_txhdr_ampdu *ampdu)
{
	struct b43_txhdr_ac *txhdr = (struct b43_txhdr_ac *)_txhdr;
	struct b43_txhdr_ctl ctl;
	const struct b43_txhdr_ctl *c = &ctl;
	const struct ieee80211_hdr *wlhdr =
	    (const struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_rate *rates = info->control.rates;
	struct b43_tx_rate t[B43_TXH_AC_RATES];
	const struct b43_phy_ac *ac = dev->phy.ac;
	const struct ieee80211_key_conf *key = info->control.hw_key;
	unsigned int len = skb->len + FCS_LEN;
	u16 mac_lo = 0, mac_hi = 0, rts = 0;
	unsigned int n, i;

	BUILD_BUG_ON(sizeof(struct b43_txhdr_ac) != 128);
	BUILD_BUG_ON(B43_TXH_AC_RATES > IEEE80211_TX_MAX_RATES);

	b43_txhdr_ctl_get(dev, info, &ctl);

	if (key)
		len += key->icv_len;

	if (READ_ONCE(ac->thermal_throttled))
		b43_txhdr_ac_one_stream(rates);
	if (!b43_tx_rate_get(dev, info, &rates[0], &t[0]))
		return -EINVAL;
	for (n = 1; n < B43_TXH_AC_RATES; n++) {
		if (!rates[n].count ||
		    !b43_tx_rate_get(dev, info, &rates[n], &t[n]) ||
		    (t[0].enc == B43_TX_VHT && t[n].enc != B43_TX_VHT))
			break;
	}
	for (i = n; i < IEEE80211_TX_MAX_RATES; i++)
		rates[i].idx = -1;

	memset(txhdr, 0, sizeof(*txhdr));
	txhdr->toe[0] = 0x02;
	txhdr->toe[2] = 0x02;

	if (c->ack)
		mac_lo |= B43_TXH_AC_MAC_IACK;
	if (c->hwseq)
		mac_lo |= B43_TXH_AC_MAC_ASEQ;
	if (c->first_frag)
		mac_lo |= B43_TXH_AC_MAC_STMSDU;
	if (c->long_frame)
		mac_lo |= B43_TXH_AC_MAC_LFRM;
	if (n == 1)
		mac_hi |= B43_TXH_AC_MAC_FIX_RATE;
	/* IEEE80211_TX_CTL_AMPDU stays only on an MPDU sent as one. */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    b43_txhdr_ac_ampdu(txhdr, info, (struct ieee80211_hdr *)skb->data))
		mac_lo |= B43_TXH_AC_MAC_AMPDU;
	else
		info->flags &= ~IEEE80211_TX_CTL_AMPDU;
	if (t[0].enc == B43_TX_VHT && !(mac_lo & B43_TXH_AC_MAC_AMPDU))
		mac_hi |= B43_TXH_AC_MAC_SVHT;

	txhdr->mac_ctl_lo = cpu_to_le16(mac_lo);
	txhdr->mac_ctl_hi = cpu_to_le16(mac_hi);
	txhdr->chanspec = cpu_to_le16(ac->chanspec);
	txhdr->iv_offset = ieee80211_hdrlen(wlhdr->frame_control);
	txhdr->frame_len = cpu_to_le16(len);
	if (key) {
		u8 kidx = key->hw_key_idx;
		u8 cipher = b43_sec_algo_fw(dev, dev->key[kidx].algorithm);

		txhdr->cipher_bss = cipher << B43_TXH_AC_CIPHER_SHIFT;
		txhdr->key_idx = b43_kidx_to_fw(dev, kidx);
	}
	txhdr->cookie = cpu_to_le16(cookie);
	txhdr->seq = wlhdr->seq_ctrl;

	for (i = 0; i < n; i++)
		b43_txhdr_ac_block(ac, &txhdr->rate[i], &t[i], len);
	if (t[0].enc == B43_TX_HT || t[0].enc == B43_TX_VHT)
		b43_txhdr_ac_mcs_duration(dev, skb, info,
					  b43_tx_ref_rate(&t[n - 1]));

	if (c->rts || c->cts) {
		u8 code;

		rts |= c->cts ? B43_TXH_AC_RTS_USE_CTS : B43_TXH_AC_RTS_USE_RTS;
		if (b43_is_ofdm_rate(c->rts_rate)) {
			rts |= B43_TXH_AC_RTS_FT_OFDM;
			code = b43_plcp_get_ratecode_ofdm(c->rts_rate);
		} else {
			code = b43_plcp_get_ratecode_cck(c->rts_rate);
		}
		rts |= (code & 0x0f) << B43_TXH_AC_RTS_RATE_SHIFT;
	}
	for (i = 0; i < n; i++)
		txhdr->rate[i].rts_cts_ctl = cpu_to_le16(rts);
	txhdr->rate[n - 1].rts_cts_ctl |= cpu_to_le16(B43_TXH_AC_RTS_LAST_RATE);
	b43_txhdr_ac_txbf(dev, txhdr, n, info, wlhdr);

	return 0;
}

static int b43_generate_txhdr_fw4(struct b43_wldev *dev,
				  u8 *_txhdr,
				  struct sk_buff *skb_frag,
				  struct ieee80211_tx_info *info,
				  u16 cookie,
				  const struct b43_txhdr_ampdu *ampdu)
{
	const unsigned char *fragment_data = skb_frag->data;
	unsigned int fragment_len = skb_frag->len;
	struct b43_txhdr *txhdr = (struct b43_txhdr *)_txhdr;
	const struct b43_fw_hdr *fh = dev->fw.layout.hdr;
	const struct b43_phy *phy = &dev->phy;
	struct ieee80211_hdr *wlhdr =
	    (struct ieee80211_hdr *)fragment_data;
	struct b43_plcp_hdr6 *plcp_main;
	int use_encryption = !!info->control.hw_key;
	__le16 fctl = wlhdr->frame_control;
	const struct ieee80211_tx_rate *rates = info->control.rates;
	/* Without a fallback stage, retry at the primary rate. */
	const struct ieee80211_tx_rate *t_fb = &rates[rates[1].idx < 0 ? 0 : 1];
	struct b43_tx_rate r, fb;
	bool ht;
	unsigned int fes = 0, fes_fb = 0, next_len = 0;
	struct ieee80211_vif *vif = info->control.vif;
	unsigned int plcp_fragment_len, ppdu_len;
	bool protect = !ampdu || ampdu->first;
	u32 mac_ctl = 0;
	u16 phy_ctl = 0;
	bool fill_phy_ctl1 = (phy->type == B43_PHYTYPE_LP ||
			      phy->type == B43_PHYTYPE_N ||
			      phy->type == B43_PHYTYPE_HT);
	u8 extra_ft = 0;
	struct b43_txhdr_ctl c;

	/* Only the first A-MPDU member's protection precedes the PPDU. */
	if (protect)
		b43_tx_ht_protect(dev, info);
	b43_txhdr_ctl_get(dev, info, &c);

	memset(txhdr, 0, sizeof(*txhdr));

	/*
	 * The members of an A-MPDU need an MCS as their fallback rate too:
	 * retry at the primary one rather than at a legacy rate, which
	 * minstrel_ht's CCK group gives at 2.4 GHz.
	 */
	if (ampdu && (rates[0].flags & IEEE80211_TX_RC_MCS) &&
	    !(t_fb->flags & IEEE80211_TX_RC_MCS))
		t_fb = &rates[0];
	if (!b43_tx_rate_get(dev, info, &rates[0], &r) ||
	    !b43_tx_rate_get(dev, info, t_fb, &fb))
		return -EINVAL;
	ht = r.enc == B43_TX_HT || fb.enc == B43_TX_HT;
	/* A-MPDU members use the primary HT rate; the caller checks limits. */
	if (ampdu && (r.enc != B43_TX_HT || !c.ack))
		return -EINVAL;

	if (r.enc == B43_TX_HT)
		txhdr->phy_rate = r.rate;
	else if (r.enc == B43_TX_OFDM)
		txhdr->phy_rate = r.ofdm->signal;
	else
		txhdr->phy_rate = b43_plcp_get_ratecode_cck(r.rate);
	txhdr->mac_frame_ctl = wlhdr->frame_control;
	memcpy(txhdr->tx_receiver, wlhdr->addr1, ETH_ALEN);

	/* Calculate duration for fallback rate */
	if (ht || fb.rate == r.rate ||
	    (wlhdr->duration_id & cpu_to_le16(0x8000)) ||
	    (wlhdr->duration_id == cpu_to_le16(0))) {
		/* If the fallback rate equals the normal rate or the
		 * dur_id field contains an AID, CFP magic or 0,
		 * use the original dur_id field. */
		txhdr->dur_fb = wlhdr->duration_id;
	} else {
		txhdr->dur_fb = ieee80211_generic_frame_duration(
			dev->wl->hw, info->control.vif, info->band,
			fragment_len,
			&dev->wl->hw->wiphy->bands[info->band]->bitrates[t_fb->idx]);
	}

	plcp_fragment_len = fragment_len + FCS_LEN;
	if (use_encryption) {
		u8 key_idx = info->control.hw_key->hw_key_idx;
		struct b43_key *key;
		int wlhdr_len;
		size_t iv_len;

		if (B43_WARN_ON(key_idx >= ARRAY_SIZE(dev->key)))
			return -ENOKEY;
		key = &(dev->key[key_idx]);

		if (unlikely(!key->keyconf)) {
			/* This key is invalid. This might only happen
			 * in a short timeframe after machine resume before
			 * we were able to reconfigure keys.
			 * Drop this packet completely. Do not transmit it
			 * unencrypted to avoid leaking information. */
			return -ENOKEY;
		}

		/* Hardware appends ICV. */
		plcp_fragment_len += info->control.hw_key->icv_len;

		key_idx = b43_kidx_to_fw(dev, key_idx);
		mac_ctl |= (key_idx << B43_TXH_MAC_KEYIDX_SHIFT) &
			   B43_TXH_MAC_KEYIDX;
		mac_ctl |= (key->algorithm << B43_TXH_MAC_KEYALG_SHIFT) &
			   B43_TXH_MAC_KEYALG;
		wlhdr_len = ieee80211_hdrlen(fctl);
		if (key->algorithm == B43_SEC_ALGO_TKIP) {
			u16 phase1key[5];
			int i;
			/* we give the phase1key and iv16 here, the key is stored in
			 * shm. With that the hardware can do phase 2 and encryption.
			 */
			ieee80211_get_tkip_p1k(info->control.hw_key, skb_frag, phase1key);
			/* phase1key is in host endian. Copy to little-endian txhdr->iv. */
			for (i = 0; i < 5; i++) {
				txhdr->iv[i * 2 + 0] = phase1key[i];
				txhdr->iv[i * 2 + 1] = phase1key[i] >> 8;
			}
			/* iv16 */
			memcpy(txhdr->iv + 10, ((u8 *) wlhdr) + wlhdr_len, 3);
		} else {
			iv_len = min_t(size_t, info->control.hw_key->iv_len,
				     ARRAY_SIZE(txhdr->iv));
			memcpy(txhdr->iv, ((u8 *) wlhdr) + wlhdr_len, iv_len);
		}
	}
	/* mac80211 leaves the duration to the driver for HT rates and for
	 * frames sent through fast-xmit. */
	if ((ht || info->control.flags & IEEE80211_TX_CTRL_FAST_XMIT) &&
	    !ieee80211_is_ctl(fctl)) {
		/* mac80211 does not export the next fragment's length. Reserve
		 * one full-sized fragment, an upper bound for the final one. */
		if (ieee80211_has_morefrags(fctl))
			next_len = plcp_fragment_len;
		wlhdr->duration_id = cpu_to_le16(c.ack ?
			b43_tx_nav(dev, info, &r, next_len, ampdu) : 0);
		txhdr->dur_fb = cpu_to_le16(c.ack ?
			b43_tx_nav(dev, info, &fb, next_len, ampdu) : 0);
	}
	/* The first header describes the whole A-MPDU PPDU. Continuation
	 * PLCP and mixed-mode lengths stay individual. */
	ppdu_len = ampdu && ampdu->first ? ampdu->len : plcp_fragment_len;
	plcp_main = b43_txhdr_at(txhdr, fh->tx_plcp);
	if (fh->tx_ppdu_plcp) {
		b43_tx_plcp(&r, ppdu_len, ampdu && ampdu->first, plcp_main);
		if (ampdu && ampdu->first)
			put_unaligned_le16(ampdu->preload,
					   b43_txhdr_at(txhdr, fh->tx_preload));
	} else {
		b43_tx_plcp(&r, plcp_fragment_len, false, plcp_main);
	}
	b43_tx_plcp(&fb, plcp_fragment_len, false, &txhdr->plcp_fb);
	if (r.enc == B43_TX_HT)
		txhdr->mimo_modelen = cpu_to_le16(b43_tx_lsig_len(&r, ppdu_len));
	if (fb.enc == B43_TX_HT)
		txhdr->mimo_ratelen_fb =
			cpu_to_le16(b43_tx_lsig_len(&fb, ppdu_len));
	if (ht) {
		fes = b43_tx_airtime(&r, ppdu_len, phy->gmode) +
		      (c.ack ? b43_tx_nav(dev, info, &r, 0, ampdu) : 0);
		fes_fb = b43_tx_airtime(&fb, ppdu_len, phy->gmode) +
			 (c.ack ? b43_tx_nav(dev, info, &fb, 0, ampdu) : 0);
	}

	/* Extra Frame Types */
	if (fb.enc == B43_TX_HT)
		extra_ft |= B43_TXH_EFT_FB_HT;
	else if (fb.enc == B43_TX_OFDM)
		extra_ft |= B43_TXH_EFT_FB_OFDM;
	else
		extra_ft |= B43_TXH_EFT_FB_CCK;

	/* Set channel radio code. Note that the micrcode ORs 0x100 to
	 * this value before comparing it to the value in SHM, if this
	 * is a 5Ghz packet.
	 */
	txhdr->chan_radio_code = phy->channel;
	/* On a 40 MHz channel, the centre channel. */
	if (b43_is_40mhz(dev))
		txhdr->chan_radio_code = b43_phy_center_channel(dev);

	/* PHY TX Control word */
	if (r.enc == B43_TX_HT)
		phy_ctl |= B43_TXH_PHY_ENC_HT;
	else if (r.enc == B43_TX_OFDM)
		phy_ctl |= B43_TXH_PHY_ENC_OFDM;
	else
		phy_ctl |= B43_TXH_PHY_ENC_CCK;
	if (r.short_preamble)
		phy_ctl |= B43_TXH_PHY_SHORTPRMBL;

	switch (b43_ieee80211_antenna_sanitize(dev, 0)) {
	case 0: /* Default */
		phy_ctl |= B43_TXH_PHY_ANT01AUTO;
		break;
	case 1: /* Antenna 0 */
		phy_ctl |= B43_TXH_PHY_ANT0;
		break;
	case 2: /* Antenna 1 */
		phy_ctl |= B43_TXH_PHY_ANT1;
		break;
	case 3: /* Antenna 2 */
		phy_ctl |= B43_TXH_PHY_ANT2;
		break;
	case 4: /* Antenna 3 */
		phy_ctl |= B43_TXH_PHY_ANT3;
		break;
	default:
		B43_WARN_ON(1);
	}
	/* On the pre-AC cores with HT the field holds the mask of TX cores. */
	if (b43_ht_core(dev)) {
		phy_ctl &= ~B43_TXH_PHY_ANT;
		phy_ctl |= dev->txchain << B43_TXH_PHY_ANT_SHIFT;
	}

	/* MAC control */
	if (c.ack)
		mac_ctl |= B43_TXH_MAC_ACK;
	if (c.hwseq)
		mac_ctl |= B43_TXH_MAC_HWSEQ;
	if (c.first_frag && (!ampdu || ampdu->first))
		mac_ctl |= B43_TXH_MAC_STMSDU;
	if (ampdu && ampdu->last)
		mac_ctl |= B43_TXH_MAC_AMPDU_LAST;
	else if (ampdu && ampdu->first)
		mac_ctl |= B43_TXH_MAC_AMPDU_FIRST;
	else if (ampdu)
		mac_ctl |= B43_TXH_MAC_AMPDU_INTER;
	if (!phy->gmode)
		mac_ctl |= B43_TXH_MAC_5GHZ;
	if (b43_is_40mhz(dev))
		mac_ctl |= B43_TXH_MAC_40MHZ;
	if (c.long_frame)
		mac_ctl |= B43_TXH_MAC_LONGFRAME;
	if (fb.short_preamble)
		mac_ctl |= B43_TXH_MAC_FB_SHORTPRMBL;

	/* Generate the RTS or CTS-to-self frame */
	if (protect && (c.rts || c.cts)) {
		unsigned int len;
		struct ieee80211_hdr *hdr;
		struct b43_tx_rate rts, rts_fb;
		struct b43_plcp_hdr6 *plcp;

		/* mac80211 selects no protection rate for MCS frames. */
		if (ht) {
			b43_tx_rate_bss(&rts, b43_tx_response_rate(dev, info,
								   &r, true),
					vif);
			b43_tx_rate_bss(&rts_fb, b43_tx_response_rate(dev, info,
								      &fb, true),
					vif);
		} else {
			b43_tx_rate_bss(&rts, c.rts_rate, vif);
			b43_tx_rate_bss(&rts_fb,
					b43_calc_fallback_rate(c.rts_rate,
							       phy->gmode),
					vif);
		}
		/* Ahead of a 40 MHz frame, on both halves */
		rts.bw = r.bw;
		rts_fb.bw = fb.bw;

		if (c.cts) {
			struct ieee80211_cts *cts;

			cts = b43_txhdr_at(txhdr, fh->tx_rts_frame);
			if (ht) {
				cts->frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_CTS);
				memcpy(cts->ra, wlhdr->addr2, ETH_ALEN);
			} else {
				ieee80211_ctstoself_get(dev->wl->hw,
							info->control.vif,
							fragment_data,
							fragment_len,
							info, cts);
			}
			mac_ctl |= B43_TXH_MAC_SENDCTS;
			len = sizeof(struct ieee80211_cts);
		} else {
			struct ieee80211_rts *rts;

			rts = b43_txhdr_at(txhdr, fh->tx_rts_frame);
			if (ht) {
				rts->frame_control =
					cpu_to_le16(IEEE80211_FTYPE_CTL |
						    IEEE80211_STYPE_RTS);
				memcpy(rts->ra, wlhdr->addr1, ETH_ALEN);
				memcpy(rts->ta, wlhdr->addr2, ETH_ALEN);
			} else {
				ieee80211_rts_get(dev->wl->hw,
						  info->control.vif,
						  fragment_data, fragment_len,
						  info, rts);
			}
			mac_ctl |= B43_TXH_MAC_SENDRTS;
			len = sizeof(struct ieee80211_rts);
		}
		len += FCS_LEN;

		/* Generate the PLCP headers for the RTS/CTS frame */
		plcp = b43_txhdr_at(txhdr, fh->tx_rts_plcp);
		b43_tx_plcp(&rts, len, false, plcp);
		b43_tx_plcp(&rts_fb, len, false, &txhdr->rts_plcp_fb);

		hdr = b43_txhdr_at(txhdr, fh->tx_rts_frame);
		txhdr->rts_dur_fb = hdr->duration_id;
		if (ht) {
			/* The HT RTS/CTS frames above carry no duration yet. */
			unsigned int sifs = phy->gmode ? 10 : 16;
			unsigned int nav = sifs + fes;
			unsigned int nav_fb = sifs + fes_fb;

			if (rts.short_preamble)
				mac_ctl |= B43_TXH_MAC_RTS_SHORTPRMBL;
			if (rts_fb.short_preamble)
				mac_ctl |= B43_TXH_MAC_RTS_FB_SHORTPRMBL;
			if (!c.cts) {
				nav += sifs + b43_tx_airtime(&rts, 14,
							     phy->gmode);
				nav_fb += sifs + b43_tx_airtime(&rts_fb, 14,
								phy->gmode);
			}
			hdr->duration_id = cpu_to_le16(nav);
			txhdr->rts_dur_fb = cpu_to_le16(nav_fb);
			fes = nav + b43_tx_airtime(&rts, len, phy->gmode);
			fes_fb = nav_fb + b43_tx_airtime(&rts_fb, len,
							 phy->gmode);
		}

		if (rts.enc == B43_TX_OFDM) {
			extra_ft |= B43_TXH_EFT_RTS_OFDM;
			txhdr->phy_rate_rts = rts.ofdm->signal;
		} else {
			extra_ft |= B43_TXH_EFT_RTS_CCK;
			txhdr->phy_rate_rts =
			    b43_plcp_get_ratecode_cck(rts.rate);
		}
		if (rts_fb.enc == B43_TX_OFDM)
			extra_ft |= B43_TXH_EFT_RTSFB_OFDM;
		else
			extra_ft |= B43_TXH_EFT_RTSFB_CCK;

		if (fill_phy_ctl1) {
			txhdr->phy_ctl1_rts =
				cpu_to_le16(b43_generate_tx_phy_ctl1(dev, &rts));
			txhdr->phy_ctl1_rts_fb =
				cpu_to_le16(b43_generate_tx_phy_ctl1(dev,
								     &rts_fb));
		}
	}
	/* The null delimiters after an A-MPDU member, from byte 5 of the RTS
	 * fallback PLCP; a legacy RTS fallback PLCP only uses bytes 0-3. */
	if (ampdu)
		txhdr->rts_plcp_fb.raw[5] = ampdu->ndelim;
	if (ht && ieee80211_is_data_qos(fctl) &&
	    skb_get_queue_mapping(skb_frag) < B43_QOS_QUEUE_NUM &&
	    dev->wl->qos_params[skb_get_queue_mapping(skb_frag)].p.txop) {
		txhdr->tx_fes_time_norm = cpu_to_le16(fes);
		txhdr->tx_fes_time_fb = cpu_to_le16(fes_fb);
	}

	/* Magic cookie */
	put_unaligned_le16(cookie, b43_txhdr_at(txhdr, fh->tx_cookie));

	if (fill_phy_ctl1) {
		txhdr->phy_ctl1 = cpu_to_le16(b43_generate_tx_phy_ctl1(dev, &r));
		txhdr->phy_ctl1_fb =
			cpu_to_le16(b43_generate_tx_phy_ctl1(dev, &fb));
	}

	/* Apply the bitfields */
	txhdr->mac_ctl = cpu_to_le32(mac_ctl);
	txhdr->phy_ctl = cpu_to_le16(phy_ctl);
	txhdr->extra_ft = extra_ft;

	return 0;
}

int b43_generate_txhdr(struct b43_wldev *dev,
		       u8 *_txhdr,
		       struct sk_buff *skb_frag,
		       struct ieee80211_tx_info *info,
		       u16 cookie,
		       const struct b43_txhdr_ampdu *ampdu)
{
	return dev->fw.layout.hdr->gen_txhdr(dev, _txhdr, skb_frag, info,
					     cookie, ampdu);
}

/* On-air PSDU length of a TX frame: hardware appends ICV and FCS. */
unsigned int b43_tx_psdu_len(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned int len = skb->len + FCS_LEN;

	if (info->control.hw_key)
		len += info->control.hw_key->icv_len;
	return len;
}

/* The mixed-mode L-SIG length field limits the HT PPDU duration. */
bool b43_ampdu_tx_fits(struct b43_wldev *dev, struct ieee80211_tx_info *info,
		       unsigned int ampdu_len)
{
	struct b43_tx_rate r;

	return b43_tx_rate_get(dev, info, &info->control.rates[0], &r) &&
	       b43_tx_lsig_len(&r, ampdu_len) <= 4095;
}

/* Minimum bytes from one MPDU start to the next for the peer's density
 * code: spacing 2^(density - 3) microseconds, none for zero. */
unsigned int b43_ampdu_tx_spacing(struct b43_wldev *dev,
				  struct ieee80211_tx_info *info, u8 density)
{
	struct b43_tx_rate r;

	if (!density ||
	    !b43_tx_rate_get(dev, info, &info->control.rates[0], &r))
		return 0;
	/* Bits per 0.1 us: short GI symbols last 3.6 us instead of 4 us. */
	return DIV_ROUND_UP((1 << density) * b43_tx_ndbps(&r) * 10,
			    8 * 8 * (r.sgi ? 36 : 40));
}

static s8 b43_rssi_postprocess(struct b43_wldev *dev,
			       u8 in_rssi, int ofdm,
			       int adjust_2053, int adjust_2050)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	s32 tmp;

	switch (phy->radio_ver) {
	case 0x2050:
		if (ofdm) {
			tmp = in_rssi;
			if (tmp > 127)
				tmp -= 256;
			tmp *= 73;
			tmp /= 64;
			if (adjust_2050)
				tmp += 25;
			else
				tmp -= 3;
		} else {
			if (dev->dev->bus_sprom->
			    boardflags_lo & B43_BFL_RSSI) {
				if (in_rssi > 63)
					in_rssi = 63;
				B43_WARN_ON(phy->type != B43_PHYTYPE_G);
				tmp = gphy->nrssi_lt[in_rssi];
				tmp = 31 - tmp;
				tmp *= -131;
				tmp /= 128;
				tmp -= 57;
			} else {
				tmp = in_rssi;
				tmp = 31 - tmp;
				tmp *= -149;
				tmp /= 128;
				tmp -= 68;
			}
			if (phy->type == B43_PHYTYPE_G && adjust_2050)
				tmp += 25;
		}
		break;
	case 0x2060:
		if (in_rssi > 127)
			tmp = in_rssi - 256;
		else
			tmp = in_rssi;
		break;
	default:
		tmp = in_rssi;
		tmp -= 11;
		tmp *= 103;
		tmp /= 64;
		if (adjust_2053)
			tmp -= 109;
		else
			tmp -= 83;
	}

	return (s8) tmp;
}

/*
 * The rate of a frame from the AC microcode, by the frame type of PHY RX
 * status 0, which here counts HT and VHT besides CCK and OFDM (Broadcom's
 * FT_HT and FT_VHT). For HT and VHT the six bytes in front of the frame are
 * the SIG fields, laid out as 802.11 defines them: HT-SIG1/2 (MCS and the
 * 40 MHz bit in byte 0, coding, STBC and short guard interval in byte 3,
 * as brcmsmac's brcms_c_compute_rspec() reads them) and VHT-SIG-A1/A2
 * (bandwidth, STBC and the streams in A1, short guard interval, coding and
 * MCS in A2). Returns the rate index for mac80211, or -1.
 */
/* x^8 + x^2 + x + 1, bit-reversed */
#define B43_HT_SIG_CRC_POLY	0xe0

/*
 * HT-SIG: 34 data bits, complemented CRC-8, then six zero tail bits. The
 * CRC runs over the data bits LSB first from 0xff, a bit at a time: 34
 * steps per frame, so no table, and no dependency on the kernel's CRC8.
 */
static bool b43_ht_sig_crc_valid(const u8 *plcp)
{
	u8 crc = 0xff;
	int i;

	for (i = 0; i < 34; i++) {
		u8 bit = (plcp[i / 8] >> (i % 8)) & 1;

		crc = (crc >> 1) ^ (((crc ^ bit) & 1) ? B43_HT_SIG_CRC_POLY : 0);
	}
	return (u8)~crc == ((plcp[4] >> 2) | (plcp[5] << 6));
}

static const u8 *b43_rx_ht_plcp(struct b43_wldev *dev, const u8 *plcp,
				u32 macstat, u16 mactime, u16 channel)
{
	u64 now;
	u32 tsf;

	if (get_unaligned_le32(plcp) || get_unaligned_le16(plcp + 4)) {
		dev->rx_ht_valid = false;
		if ((plcp[0] & 0x7f) >= 8 * hweight8(dev->rxchain))
			return NULL;
		if (!(macstat & B43_RX_MAC_RXST_VALID) &&
		    !b43_ht_sig_crc_valid(plcp))
			return NULL;
		if ((plcp[3] & 0x08) && !(macstat & B43_RX_MAC_RXST_VALID)) {
			now = ktime_get_ns();
			tsf = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_LOW);
			memcpy(dev->rx_ht_plcp, plcp, sizeof(dev->rx_ht_plcp));
			dev->rx_ht_tsf = tsf - (u16)(tsf - mactime);
			dev->rx_ht_channel = channel;
			dev->rx_ht_deadline = now + 64 * NSEC_PER_MSEC;
			dev->rx_ht_valid = true;
		}
		return plcp;
	}

	if (!dev->rx_ht_valid || (u16)dev->rx_ht_tsf != mactime ||
	    dev->rx_ht_channel != channel ||
	    ktime_get_ns() >= dev->rx_ht_deadline)
		goto invalid;
	/* Resolve the TSF epoch too: equal low 16 bits can belong to a later
	 * PPDU even when an older first member was processed only recently. */
	tsf = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_LOW);
	if (tsf - (u16)(tsf - mactime) != dev->rx_ht_tsf)
		goto invalid;
	if (macstat & B43_RX_MAC_RXST_VALID)
		dev->rx_ht_valid = false;
	return dev->rx_ht_plcp;

invalid:
	/*
	 * An MPDU without an HT-SIG that no first MPDU of its PPDU vouches
	 * for (why the HT-SIG is missing is not known). Its FCS is good, only
	 * its rate is unknown: it gets the last HT-SIG's, as dropping it loses
	 * an MPDU the block ack already acknowledged and mac80211's reorder
	 * buffer waits out every such hole. On a BCM4331 receiving MCS 15
	 * A-MPDUs with the short guard interval that was 2% of them, and TCP
	 * fell to 13 Mbit/s.
	 */
	dev->rx_ht_valid = false;
	return dev->rx_ht_plcp;
}

/* The rate of an HT frame from its HT-SIG @p. Returns the MCS. */
static int b43_rx_rate_ht(const u8 *p, struct ieee80211_rx_status *status)
{
	status->encoding = RX_ENC_HT;
	if (p[0] & 0x80)
		status->bw = RATE_INFO_BW_40;
	if (p[3] & 0x80)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	if (p[3] & 0x40)
		status->enc_flags |= RX_ENC_FLAG_LDPC;
	status->enc_flags |= ((p[3] >> 4) & 0x3) << RX_ENC_FLAG_STBC_SHIFT;
	return p[0] & 0x7f;
}

static int b43_rx_rate_ac(struct b43_plcp_hdr6 *plcp, u16 phystat0,
			  bool rx_5ghz, struct ieee80211_rx_status *status)
{
	const u8 *p = plcp->raw;
	u32 a1, a2;
	u8 nsts, mcs;

	switch (phystat0 & B43_RX_PHYST0_FTYPE) {
	case B43_RX_PHYST0_HT:
		return b43_rx_rate_ht(p, status);
	case B43_RX_PHYST0_VHT:
		a1 = p[0] | (p[1] << 8) | (p[2] << 16);
		a2 = p[3] | (p[4] << 8) | (p[5] << 16);
		mcs = (a2 >> 4) & 0xf;
		if (mcs > 9)
			return -1;
		status->encoding = RX_ENC_VHT;
		switch (a1 & 0x3) {
		case 1:
			status->bw = RATE_INFO_BW_40;
			break;
		case 2:
			status->bw = RATE_INFO_BW_80;
			break;
		case 3:
			status->bw = RATE_INFO_BW_160;
			break;
		}
		nsts = ((a1 >> 10) & 0x7) + 1;
		if (a1 & 0x8) {
			status->enc_flags |= 1 << RX_ENC_FLAG_STBC_SHIFT;
			nsts /= 2;
		}
		status->nss = max_t(u8, nsts, 1);
		if (a2 & 0x1)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (a2 & 0x4)
			status->enc_flags |= RX_ENC_FLAG_LDPC;
		return mcs;
	case B43_RX_PHYST0_OFDM:
		return b43_plcp_get_bitrate_idx_ofdm(plcp, rx_5ghz);
	default:
		return b43_plcp_get_bitrate_idx_cck(plcp);
	}
}

/* The PSDU length in a PLCP header. */
static unsigned int b43_rx_psdu_len(const u8 *plcp, u16 phystat0)
{
	switch (phystat0 & B43_RX_PHYST0_FTYPE) {
	case B43_RX_PHYST0_CCK:
		/* CCK gives microseconds at a rate in 100 kbit/s, and a bit
		 * saying that rounding them up added a byte at 11 Mbit/s.
		 */
		return get_unaligned_le16(plcp + 2) * (plcp[0] / 5) / 16 -
		       !!(plcp[1] & 0x80);
	case B43_RX_PHYST0_OFDM:
		return (get_unaligned_le32(plcp) >> 5) & 0xFFF;
	case B43_RX_PHYST0_PRE_N:
		return get_unaligned_le16(plcp + 1);
	}
	return 0;
}

/*
 * The length of the BCM43227's A-MSDU, with the PLCP header and the padding
 * before it, from the first piece; 0 if it is not usable.
 */
static unsigned int b43_rx_amsdu_len(const struct sk_buff *head,
				     const struct b43_rxhdr_fw4 *first)
{
	u32 macstat = le32_to_cpu(first->format_598.mac_status);
	unsigned int padding = (macstat & B43_RX_MAC_PADDING) ? 2 : 0;
	unsigned int len = 0;

	if (head->len >= padding + sizeof(struct b43_plcp_hdr6))
		len = b43_rx_psdu_len(head->data + padding,
				      le16_to_cpu(first->phy_status0));
	if (!len || len > IEEE80211_MAX_MPDU_LEN_VHT_7991)
		return 0;
	return padding + sizeof(struct b43_plcp_hdr6) + len;
}

/*
 * The AC cores take an A-MSDU apart before it reaches the host, each piece
 * in a buffer of its own with the A-MSDU bit and its place in the
 * aggregation type: the first carries the PLCP, the 802.11 and CCMP
 * headers and the first subframe, decrypted, a middle one a subframe, the
 * last one the last subframe, the MIC and the FCS (a OnePlus Nord 2's
 * uplink). Put together they are the MPDU as it was sent, which goes on
 * under the first piece's receive header; a piece in error spoils it.
 * Returns that MPDU, or NULL while it is incomplete or dropped.
 *
 * The BCM43227 does the same outside A-MPDUs, but each of its pieces has
 * padding of its own, and only the last one carries the status of the
 * whole frame. There the MPDU goes on under the last piece's receive
 * header, if it is as long as its PLCP header says.
 */
static struct sk_buff *b43_rx_amsdu(struct b43_wldev *dev,
				    struct sk_buff *skb, const void **rxhdr)
{
	struct b43_rxhdr_fw4 *first = (struct b43_rxhdr_fw4 *)dev->rx_amsdu_hdr;
	u32 macstat = le32_to_cpu(((const struct b43_rxhdr_fw4 *)*rxhdr)->
				  format_598.mac_status);
	unsigned int type = (macstat & B43_RX_MAC_AGGTYPE) >>
			    B43_RX_MAC_AGGTYPE_SHIFT;
	const u32 err = B43_RX_MAC_FCSERR | B43_RX_MAC_DECERR;
	struct sk_buff *head = dev->rx_amsdu;
	int need;

	BUILD_BUG_ON(sizeof(*first) > sizeof(dev->rx_amsdu_hdr));
	if (type == B43_RX_AGG_FIRST || type == B43_RX_AGG_SINGLE) {
		if (head)
			dev_kfree_skb_any(head);
		dev->rx_amsdu = NULL;
		memcpy(first, *rxhdr, sizeof(*first));
		macstat &= ~(B43_RX_MAC_AMSDU | B43_RX_MAC_AGGTYPE);
		first->format_598.mac_status = cpu_to_le32(macstat);
		*rxhdr = first;
		if (type == B43_RX_AGG_SINGLE)
			return skb;
		dev->rx_amsdu = skb;
		dev->rx_amsdu_err = 0;
		return NULL;
	}

	if (!head) {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	if (dev->phy.nphy_ht) {
		if (macstat & B43_RX_MAC_PADDING) {
			if (skb->len < 2)
				goto drop;
			skb_pull(skb, 2);
		}
		if (head->len + skb->len > b43_rx_amsdu_len(head, first))
			goto drop;
	}
	dev->rx_amsdu_err |= macstat & err;
	need = skb->len - skb_tailroom(head);
	if (need > 0 && pskb_expand_head(head, 0, need, GFP_ATOMIC)) {
		dev_kfree_skb_any(head);
		dev_kfree_skb_any(skb);
		dev->rx_amsdu = NULL;
		return NULL;
	}
	skb_put_data(head, skb->data, skb->len);
	dev_kfree_skb_any(skb);
	if (type != B43_RX_AGG_LAST)
		return NULL;

	dev->rx_amsdu = NULL;
	if (dev->phy.nphy_ht) {
		u32 padding = le32_to_cpu(first->format_598.mac_status) &
			      B43_RX_MAC_PADDING;

		if (head->len != b43_rx_amsdu_len(head, first))
			goto drop_head;
		memcpy(first, *rxhdr, sizeof(*first));
		macstat &= ~(B43_RX_MAC_AMSDU | B43_RX_MAC_AGGTYPE |
			     B43_RX_MAC_PADDING);
		first->format_598.mac_status = cpu_to_le32(macstat | padding);
		*rxhdr = first;
		return head;
	}
	macstat = le32_to_cpu(first->format_598.mac_status) |
		  dev->rx_amsdu_err;
	first->format_598.mac_status = cpu_to_le32(macstat);
	*rxhdr = first;
	return head;

drop:
	dev_kfree_skb_any(skb);
drop_head:
	b43dbg(dev->wl, "RX: Dropping incomplete A-MSDU\n");
	dev_kfree_skb_any(head);
	dev->rx_amsdu = NULL;
	return NULL;
}

void b43_rx_amsdu_free(struct b43_wldev *dev)
{
	if (dev->rx_amsdu)
		dev_kfree_skb_any(dev->rx_amsdu);
	dev->rx_amsdu = NULL;
}

void b43_rx(struct b43_wldev *dev, struct sk_buff *skb, const void *_rxhdr)
{
	struct ieee80211_rx_status status;
	struct b43_plcp_hdr6 *plcp;
	const u8 *ht_plcp = NULL;
	struct ieee80211_hdr *wlhdr;
	const struct b43_rxhdr_fw4 *rxhdr = _rxhdr;
	const struct b43_fw_hdr *fh = dev->fw.layout.hdr;
	__le16 fctl;
	u16 phystat0, phystat3;
	u16 chanstat, mactime;
	u32 macstat;
	u16 chanid;
	u8 phytype;
	bool rx_5ghz;
	bool ht = b43_ht_enabled(dev);
	int padding, rate_idx;

	memset(&status, 0, sizeof(status));

	if (fh->rx_amsdu &&
	    (b43_nphy_ht_enabled(dev) || !fh->rx_amsdu_ht_only) &&
	    (get_unaligned_le32(b43_rxhdr_at(rxhdr, fh->rx_mac_status)) &
	     B43_RX_MAC_AMSDU)) {
		skb = b43_rx_amsdu(dev, skb, &_rxhdr);
		if (!skb)
			return;
		rxhdr = _rxhdr;
	}

	/* Get metadata about the frame from the header. */
	phystat0 = le16_to_cpu(rxhdr->phy_status0);
	phystat3 = le16_to_cpu(rxhdr->phy_status3);
	macstat = get_unaligned_le32(b43_rxhdr_at(rxhdr, fh->rx_mac_status));
	mactime = get_unaligned_le16(b43_rxhdr_at(rxhdr, fh->rx_mac_time));
	chanstat = get_unaligned_le16(b43_rxhdr_at(rxhdr, fh->rx_channel));

	/* The channel field of the older microcode is (cookie << 3) | phytype,
	 * and three bits cannot carry B43_PHYTYPE_AC. What the AC microcode
	 * puts there is not known, so on AC the PHY type comes from the device
	 * and the band from the channel the radio is tuned to. */
	if (dev->phy.type == B43_PHYTYPE_AC) {
		phytype = B43_PHYTYPE_AC;
		rx_5ghz = b43_current_band(dev->wl) == NL80211_BAND_5GHZ;
	} else {
		phytype = chanstat & B43_RX_CHAN_PHYTYPE;
		rx_5ghz = !!(chanstat & B43_RX_CHAN_5GHZ);
	}
	/* brcmsmac treats frame type 3 as an error, and the BCM43227 was
	 * not seen to report it.
	 */
	if (b43_ht_core(dev) &&
	    (phystat0 & B43_RX_PHYST0_FTYPE) == B43_RX_PHYST0_STD_N)
		goto drop;
	if (ht && !(phystat0 & B43_RX_PHYST0_PRE_N))
		dev->rx_ht_valid = false;

	if (unlikely(macstat & B43_RX_MAC_FCSERR)) {
		/* The BCM43227's microcode counts all of them. */
		if (!dev->phy.nphy_ht)
			dev->wl->ieee_stats.dot11FCSErrorCount++;
		status.flag |= RX_FLAG_FAILED_FCS_CRC;
	}
	if (unlikely(phystat0 & (B43_RX_PHYST0_PLCPHCF | B43_RX_PHYST0_PLCPFV)))
		status.flag |= RX_FLAG_FAILED_PLCP_CRC;
	if (phystat0 & B43_RX_PHYST0_SHORTPRMBL)
		status.enc_flags |= RX_ENC_FLAG_SHORTPRE;
	if (ht && (phystat0 & B43_RX_PHYST0_PRE_N)) {
		if (!(macstat & B43_RX_MAC_RXST_VALID))
			status.flag &= ~RX_FLAG_FAILED_PLCP_CRC;
		padding = (macstat & B43_RX_MAC_PADDING) ? 2 : 0;
		if (fh->rx_ht_plcp &&
		    !(status.flag & RX_FLAG_FAILED_PLCP_CRC) &&
		    skb->len >= sizeof(struct b43_plcp_hdr6) + padding)
			ht_plcp = b43_rx_ht_plcp(dev, skb->data + padding,
						 macstat, mactime, chanstat);
	}
	/*
	 * Decryption with the given key failed: drop the packet, which the key
	 * in software would not decrypt either. On the AC the error stands
	 * only where decryption was attempted. Its microcode also sets the
	 * bit on frames it did not try to decrypt, with the attempt bit clear,
	 * when no hardware key applies: gonsolo found the MacBookAir6,1's
	 * EAPOL key messages, DHCP and ARP replies flagged so, unprotected
	 * ones among them, and its download stalling on the drops (his
	 * notes/50). Those go up, for mac80211 to decrypt or take as they are.
	 */
	if ((macstat & B43_RX_MAC_DECERR) &&
	    (dev->phy.type != B43_PHYTYPE_AC || (macstat & B43_RX_MAC_DEC)))
		goto drop;
	/*
	 * A piece of an A-MSDU on another core is not a complete MPDU: the one
	 * seen had an intact 802.11 header and unrelated data from the QoS
	 * control on, so mac80211 would read a random CCMP PN from it and drop
	 * every later frame of that TID as a replay. Drop it, as brcmsmac
	 * does; the AC's and the BCM43227's are put together above.
	 */
	if (unlikely(macstat & B43_RX_MAC_AMSDU))
		goto drop;

	/* Skip PLCP and padding */
	padding = (macstat & B43_RX_MAC_PADDING) ? 2 : 0;
	if (unlikely(skb->len < (sizeof(struct b43_plcp_hdr6) + padding))) {
		b43dbg(dev->wl, "RX: Packet size underrun (1)\n");
		goto drop;
	}
	plcp = (struct b43_plcp_hdr6 *)(skb->data + padding);
	skb_pull(skb, sizeof(struct b43_plcp_hdr6) + padding);
	/* The skb contains the Wireless Header + payload data now */
	if (unlikely(skb->len < (2 + 2 + 6 /*minimum hdr */  + FCS_LEN))) {
		b43dbg(dev->wl, "RX: Packet size underrun (2)\n");
		goto drop;
	}
	wlhdr = (struct ieee80211_hdr *)(skb->data);
	fctl = wlhdr->frame_control;

	if (macstat & B43_RX_MAC_DEC) {
		unsigned int keyidx;
		int wlhdr_len;

		keyidx = ((macstat & B43_RX_MAC_KEYIDX)
			  >> B43_RX_MAC_KEYIDX_SHIFT);
		/* We must adjust the key index here. We want the "physical"
		 * key index, but the ucode passed it slightly different.
		 */
		keyidx = b43_kidx_to_raw(dev, keyidx);
		/*
		 * The BCM43227 reports index 63, past the key table,
		 * for some frames it decrypted: no decryption error, the LLC
		 * header in the clear. Elsewhere such an index stays an error.
		 */
		if (B43_WARN_ON(keyidx >= ARRAY_SIZE(dev->key) &&
				!dev->phy.nphy_ht))
			goto drop;
		if (keyidx >= ARRAY_SIZE(dev->key) ||
		    dev->key[keyidx].algorithm != B43_SEC_ALGO_NONE) {
			wlhdr_len = ieee80211_hdrlen(fctl);
			if (unlikely(skb->len < (wlhdr_len + 3))) {
				b43dbg(dev->wl,
				       "RX: Packet size underrun (3)\n");
				goto drop;
			}
			status.flag |= RX_FLAG_DECRYPTED;
		}
	}

	/* Link quality statistics */
	switch (phytype) {
	case B43_PHYTYPE_AC: {
		/*
		 * The power of each core in dBm, bytes 9 and 10 of the header,
		 * -128 on a core that did not receive (gonsolo's port).
		 */
		const s8 *pwr = (const s8 *)_rxhdr + 9;

		if (pwr[0] == -128 && pwr[1] == -128)
			status.flag |= RX_FLAG_NO_SIGNAL_VAL;
		else if (pwr[0] == -128)
			status.signal = pwr[1];
		else if (pwr[1] == -128)
			status.signal = pwr[0];
		else
			status.signal = max(pwr[0], pwr[1]);
		break;
	}
	case B43_PHYTYPE_HT:
		/* TODO: is max the right choice? */
		status.signal = max_t(__s8,
			max(rxhdr->phy_ht_power0, rxhdr->phy_ht_power1),
			rxhdr->phy_ht_power2);
		break;
	case B43_PHYTYPE_N:
		/* Broadcom has code for min and avg, but always uses max */
		if (rxhdr->power0 == 16 || rxhdr->power0 == 32)
			status.signal = max(rxhdr->power1, rxhdr->power2);
		else
			status.signal = max(rxhdr->power0, rxhdr->power1);
		if (!dev->phy.nphy_ht)
			break;
		/* On the BCM43227, power0 is core 0's and power1 core 1's. A
		 * core that does not receive reads 116.
		 */
		if (dev->rxchain == 0x1)
			status.signal = rxhdr->power0;
		else if (dev->rxchain == 0x2)
			status.signal = rxhdr->power1;
		/* Frames without PHY status carry no power. */
		if (macstat & B43_RX_MAC_RXST_VALID) {
			status.chains = dev->rxchain;
			status.chain_signal[0] = rxhdr->power0;
			status.chain_signal[1] = rxhdr->power1;
		}
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
	case B43_PHYTYPE_LP:
		status.signal = b43_rssi_postprocess(dev, rxhdr->jssi,
						  (phystat0 & B43_RX_PHYST0_OFDM),
						  (phystat0 & B43_RX_PHYST0_GAINCTL),
						  (phystat3 & B43_RX_PHYST3_TRSTATE));
		break;
	}

	if (fh->rx_rate) {
		rate_idx = fh->rx_rate(plcp, phystat0, rx_5ghz, &status);
	} else if (ht && (phystat0 & B43_RX_PHYST0_PRE_N)) {
		/* Type 2 is HT, not a legacy CCK rate code. A
		 * failed HT-SIG cannot provide trustworthy MCS metadata. */
		if (!ht_plcp)
			goto drop;
		if (!(macstat & B43_RX_MAC_RXST_VALID))
			status.flag |= RX_FLAG_NO_SIGNAL_VAL;
		status.enc_flags &= ~RX_ENC_FLAG_SHORTPRE;
		rate_idx = b43_rx_rate_ht(ht_plcp, &status);
	} else if (phystat0 & B43_RX_PHYST0_OFDM) {
		rate_idx = b43_plcp_get_bitrate_idx_ofdm(plcp, rx_5ghz);
	} else {
		rate_idx = b43_plcp_get_bitrate_idx_cck(plcp);
	}
	if (unlikely(rate_idx == -1)) {
		/* PLCP seems to be corrupted.
		 * Drop the frame, if we are not interested in corrupted frames. */
		if (!(dev->wl->filter_flags & FIF_PLCPFAIL) ||
		    !(status.flag & RX_FLAG_FAILED_PLCP_CRC))
			goto drop;
	}
	status.rate_idx = rate_idx;
	status.antenna = !!(phystat0 & B43_RX_PHYST0_ANT);

	/*
	 * All frames on monitor interfaces and beacons always need a full
	 * 64-bit timestamp. Monitor interfaces need it for diagnostic
	 * purposes and beacons for IBSS merging.
	 * This code assumes we get to process the packet within 16 bits
	 * of timestamp, i.e. about 65 milliseconds after the PHY received
	 * the first symbol.
	 */
	if (ieee80211_is_beacon(fctl) || dev->wl->radiotap_enabled) {
		u16 low_mactime_now;

		b43_tsf_read(dev, &status.mactime);
		low_mactime_now = status.mactime;
		status.mactime = status.mactime & ~0xFFFFULL;
		status.mactime += mactime;
		if (low_mactime_now <= mactime)
			status.mactime -= 0x10000;
		status.flag |= RX_FLAG_MACTIME_START;
	}
	if (dev->wl->ps_enabled && !ieee80211_is_ctl(fctl) &&
	    !(status.flag & RX_FLAG_FAILED_FCS_CRC) &&
	    skb->len >= sizeof(struct ieee80211_hdr_3addr) &&
	    ether_addr_equal(wlhdr->addr2, dev->wl->bssid))
		b43_ps_rx(dev, skb);

	chanid = (chanstat & B43_RX_CHAN_ID) >> B43_RX_CHAN_ID_SHIFT;
	switch (phytype) {
	case B43_PHYTYPE_G:
		status.band = NL80211_BAND_2GHZ;
		if (dev->fw.layout.g_chanid_number)
			status.freq = ieee80211_channel_to_frequency(chanid, status.band);
		else
			status.freq = chanid + 2400;
		break;
	case B43_PHYTYPE_N:
	case B43_PHYTYPE_LP:
	case B43_PHYTYPE_HT:
		/* chanid is the SHM channel cookie. Which is the plain
		 * channel number in b43. */
		if (rx_5ghz)
			status.band = NL80211_BAND_5GHZ;
		else
			status.band = NL80211_BAND_2GHZ;
		status.freq =
			ieee80211_channel_to_frequency(chanid, status.band);
		/* A 40 MHz channel is reported by its centre channel. */
		if ((chanstat & B43_RX_CHAN_40MHZ) && b43_is_40mhz(dev))
			status.freq += dev->phy.chandef->chan->center_freq -
				       dev->phy.chandef->center_freq1;
		break;
	case B43_PHYTYPE_AC:
		status.band = b43_current_band(dev->wl);
		status.freq = dev->wl->hw->conf.chandef.chan->center_freq;
		break;
	default:
		B43_WARN_ON(1);
		goto drop;
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
	/* Delivered by b43_rx_deliver(), outside wl->mutex. */
	skb_queue_tail(&dev->wl->rx_queue, skb);

#if B43_DEBUG
	dev->rx_count++;
#endif
	return;
drop:
	if (!ht_plcp)
		dev->rx_ht_valid = false;
	dev_kfree_skb_any(skb);
}

/*
 * A TX status whose cookie matches no queue, as the words it came in where
 * the status keeps them.
 */
void b43_txstatus_dump(struct b43_wldev *dev,
		       const struct b43_txstatus *status)
{
	const u32 *w = status->raw;

	if (!dev->fw.layout.hdr->txstatus_raw)
		return;
	b43warn(dev->wl, "TX status %08x %08x %08x %08x / "
		"%08x %08x %08x %08x\n",
		w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
}

void b43_handle_txstatus(struct b43_wldev *dev,
			 const struct b43_txstatus *status)
{
	b43_debugfs_log_txstat(dev, status);

	if (dev->phy.nphy_ht && ++dev->macstat_txstatus >= 1024)
		b43_macstat_update(dev, true);
	if (status->for_ampdu) {
		/* A Block Ack was missed, as brcmsmac counts it. */
		if (dev->phy.nphy_ht && !status->acked &&
		    status->supp_reason == B43_TXST_SUPP_NONE)
			dev->wl->ieee_stats.dot11ACKFailureCount++;
		/* Only DMA sends A-MPDUs (b43_ampdu_tx()). */
		if (!b43_using_pio_transfers(dev))
			b43_ampdu_txstatus(dev, status);
		return;
	}
	if (status->intermediate)
		return;
	/* A frame held for a station in power save or flushed is not an error. */
	if (dev->fw.layout.hdr->txstatus_raw &&
	    status->supp_reason != B43_TXST_SUPP_NONE &&
	    status->supp_reason != B43_TXST_SUPP_PMQ &&
	    status->supp_reason != B43_TXST_SUPP_FLUSH)
		b43warn(dev->wl, "TX suppressed: reason %u, cookie 0x%04x, status %08x\n",
			status->supp_reason, status->cookie, status->raw[0]);
	if (dev->phy.nphy_ht) {
		/* Every transmission without an ACK; the microcode counts
		 * RTS frames (b43_macstat_update()). */
		if (status->frame_count)
			dev->wl->ieee_stats.dot11ACKFailureCount +=
				status->frame_count - !!status->acked;
	} else if (!status->acked) {
		dev->wl->ieee_stats.dot11ACKFailureCount++;
	}
	if (status->rts_count && !dev->phy.nphy_ht) {
		if (status->rts_count == 0xF)	//FIXME
			dev->wl->ieee_stats.dot11RTSFailureCount++;
		else
			dev->wl->ieee_stats.dot11RTSSuccessCount++;
	}

	if (b43_using_pio_transfers(dev))
		b43_pio_handle_txstatus(dev, status);
	else
		b43_dma_handle_txstatus(dev, status);

	b43_phy_txpower_check(dev, 0);
}

/*
 * Whether an MPDU of an A-MPDU status was acknowledged: the block ack came
 * and has its bit, counted from the first MPDU's sequence number.
 */
bool b43_txstatus_mpdu_acked(const struct b43_txstatus *status,
			     const struct sk_buff *skb)
{
	const struct ieee80211_hdr *hdr = (const void *)skb->data;
	u16 sn = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
	unsigned int off = (sn - status->ba_ssn) & IEEE80211_MAX_SN;

	return status->acked && off < 64 && (status->ba_bitmap & BIT_ULL(off));
}

/*
 * The AC status counts the attempts at each rate block, and the frame's
 * rates are its blocks (b43_generate_txhdr_ac()). With one block, FIX_RATE,
 * only block 0's count is attempts (b43_txstatus_read_ac()).
 *
 * The counts of an A-MPDU's status are its MPDUs' attempts: mac80211 takes
 * them as the A-MPDU's, each times ampdu_len, so they are divided by the
 * MPDUs. Its first MPDU carries the A-MPDU's figures for rate control, the
 * others only their acknowledgement.
 */
static void b43_fill_txstatus_rates_ac(struct ieee80211_tx_info *report,
				       const struct b43_txstatus *status)
{
	struct ieee80211_tx_rate *rates = report->status.rates;
	unsigned int mpdus = 1, n, i;

	if (report->flags & IEEE80211_TX_CTL_AMPDU) {
		mpdus = max_t(unsigned int, status->mpdus, 1);
		if (status->mpdu_index == 0) {
			report->flags |= IEEE80211_TX_STAT_AMPDU;
			report->status.ampdu_len = mpdus;
			report->status.ampdu_ack_len = status->mpdus_acked;
		}
	}

	for (n = 0; n < B43_TXH_AC_RATES && rates[n].idx >= 0; n++)
		;
	if (n <= 1) {
		rates[0].count = DIV_ROUND_UP(status->frame_count, mpdus);
		return;
	}
	for (i = 0; i < n; i++)
		rates[i].count = DIV_ROUND_UP(status->rate_tries[i], mpdus);
}

/* Fill out the mac80211 TXstatus report based on the b43-specific
 * txstatus report data. This returns a boolean whether the frame was
 * successfully transmitted. */
bool b43_fill_txstatus_report(struct b43_wldev *dev,
			      struct ieee80211_tx_info *report,
			      const struct b43_txstatus *status)
{
	bool frame_success = true;
	int retry_limit;

	/* preserve the confiured retry limit before clearing the status
	 * The xmit function has overwritten the rc's value with the actual
	 * retry limit done by the hardware */
	retry_limit = report->status.rates[0].count;
	ieee80211_tx_info_clear_status(report);

	if (status->acked) {
		/* The frame was ACKed. */
		report->flags |= IEEE80211_TX_STAT_ACK;
	} else {
		/* The frame was not ACKed... */
		if (!(report->flags & IEEE80211_TX_CTL_NO_ACK)) {
			/* ...but we expected an ACK. */
			frame_success = false;
		}
	}
	if (dev->fw.layout.hdr->txstatus_rates) {
		dev->fw.layout.hdr->txstatus_rates(report, status);
		return frame_success;
	}
	if (status->frame_count == 0) {
		/* The frame was not transmitted at all. */
		report->status.rates[0].count = 0;
	} else if (status->rts_count > dev->wl->hw->conf.short_frame_max_tx_count) {
		/*
		 * If the short retries (RTS, not data frame) have exceeded
		 * the limit, the hw will not have tried the selected rate,
		 * but will have used the fallback rate instead.
		 * Don't let the rate control count attempts for the selected
		 * rate in this case, otherwise the statistics will be off.
		 */
		report->status.rates[0].count = 0;
		report->status.rates[1].count = status->frame_count;
	} else {
		if (status->frame_count > retry_limit &&
		    report->status.rates[1].idx >= 0) {
			report->status.rates[0].count = retry_limit;
			report->status.rates[1].count = status->frame_count -
					retry_limit;

		} else {
			report->status.rates[0].count = status->frame_count;
			report->status.rates[1].idx = -1;
		}
	}

	return frame_success;
}

/* Stop any TX operation on the device (suspend the hardware queues) */
void b43_tx_suspend(struct b43_wldev *dev)
{
	if (b43_using_pio_transfers(dev))
		b43_pio_tx_suspend(dev);
	else
		b43_dma_tx_suspend(dev);
}

/* Resume any TX operation on the device (resume the hardware queues) */
void b43_tx_resume(struct b43_wldev *dev)
{
	if (b43_using_pio_transfers(dev))
		b43_pio_tx_resume(dev);
	else
		b43_dma_tx_resume(dev);
}

const struct b43_fw_hdr b43_hdr_351 = {
	.tx_stbc	= true,
	.gen_txhdr	= b43_generate_txhdr_fw4,
	.txstatus_poll	= b43_txstatus_poll_fw4,
	.txstatus_drain	= b43_txstatus_drain_fw4,
	.tx_len		= offsetofend(struct b43_txhdr, format_351.plcp),
	.tx_cookie	= offsetof(struct b43_txhdr, format_351.cookie),
	.tx_rts_plcp	= offsetof(struct b43_txhdr, format_351.rts_plcp),
	.tx_rts_frame	= offsetof(struct b43_txhdr, format_351.rts_frame),
	.tx_plcp	= offsetof(struct b43_txhdr, format_351.plcp),
	.rx_len		= offsetofend(struct b43_rxhdr_fw4, format_351),
	.rx_frame_offset = B43_DMA0_RX_FW351_FO,
	.rx_buffersize	= B43_DMA0_RX_FW351_BUFSIZE,
	.rx_mac_status	= offsetof(struct b43_rxhdr_fw4, format_351.mac_status),
	.rx_mac_time	= offsetof(struct b43_rxhdr_fw4, format_351.mac_time),
	.rx_channel	= offsetof(struct b43_rxhdr_fw4, format_351.channel),
};

const struct b43_fw_hdr b43_hdr_410 = {
	.tx_stbc	= true,
	.gen_txhdr	= b43_generate_txhdr_fw4,
	.txstatus_poll	= b43_txstatus_poll_fw4,
	.txstatus_drain	= b43_txstatus_drain_fw4,
	.tx_len		= offsetofend(struct b43_txhdr, format_410.plcp),
	.tx_cookie	= offsetof(struct b43_txhdr, format_410.cookie),
	.tx_rts_plcp	= offsetof(struct b43_txhdr, format_410.rts_plcp),
	.tx_rts_frame	= offsetof(struct b43_txhdr, format_410.rts_frame),
	.tx_plcp	= offsetof(struct b43_txhdr, format_410.plcp),
	.rx_len		= offsetofend(struct b43_rxhdr_fw4, format_351),
	.rx_frame_offset = B43_DMA0_RX_FW351_FO,
	.rx_buffersize	= B43_DMA0_RX_FW351_BUFSIZE,
	.rx_mac_status	= offsetof(struct b43_rxhdr_fw4, format_351.mac_status),
	.rx_mac_time	= offsetof(struct b43_rxhdr_fw4, format_351.mac_time),
	.rx_channel	= offsetof(struct b43_rxhdr_fw4, format_351.channel),
};

const struct b43_fw_hdr b43_hdr_598 = {
	.tx_stbc	= true,
	.gen_txhdr	= b43_generate_txhdr_fw4,
	.txstatus_poll	= b43_txstatus_poll_fw4,
	.txstatus_drain	= b43_txstatus_drain_fw4,
	.tx_len		= offsetofend(struct b43_txhdr, format_598.plcp),
	.tx_cookie	= offsetof(struct b43_txhdr, format_598.cookie),
	.tx_rts_plcp	= offsetof(struct b43_txhdr, format_598.rts_plcp),
	.tx_rts_frame	= offsetof(struct b43_txhdr, format_598.rts_frame),
	.tx_plcp	= offsetof(struct b43_txhdr, format_598.plcp),
	.tx_ppdu_plcp	= true,
	.tx_preload	= offsetof(struct b43_txhdr, format_598.preload_size),
	.rx_len		= offsetofend(struct b43_rxhdr_fw4, format_598),
	.rx_frame_offset = B43_DMA0_RX_FW598_FO,
	.rx_buffersize	= B43_DMA0_RX_FW598_BUFSIZE,
	.rx_mac_status	= offsetof(struct b43_rxhdr_fw4, format_598.mac_status),
	.rx_mac_time	= offsetof(struct b43_rxhdr_fw4, format_598.mac_time),
	.rx_channel	= offsetof(struct b43_rxhdr_fw4, format_598.channel),
	.rx_amsdu	= true,
	.rx_amsdu_ht_only = true,
	.rx_ht_plcp	= true,
};

/*
 * The AC cores' header, seen on 784, 832 and 928: no PIO, an eight-word TX
 * status. The RX fields are assumed where the 598 header has them, A-MSDU
 * path included; not verified, as they are not visible at the bus.
 */
const struct b43_fw_hdr b43_ac_hdr_784 = {
	.tx_ldpc	= true,
	.gen_txhdr	= b43_generate_txhdr_ac,
	.tx_rates_cut	= true,
	.rx_rate	= b43_rx_rate_ac,
	.txstatus_poll	= b43_txstatus_poll_ac,
	.txstatus_drain	= b43_txstatus_drain_ac,
	.txstatus_rates	= b43_fill_txstatus_rates_ac,
	.txstatus_raw	= true,
	.tx_len		= sizeof(struct b43_txhdr_ac),
	.rx_frame_offset = B43_DMA0_RX_AC_FO,
	.rx_buffersize	= B43_DMA0_RX_AC_BUFSIZE,
	.rx_mac_status	= offsetof(struct b43_rxhdr_fw4, format_598.mac_status),
	.rx_mac_time	= offsetof(struct b43_rxhdr_fw4, format_598.mac_time),
	.rx_channel	= offsetof(struct b43_rxhdr_fw4, format_598.channel),
	.rx_amsdu	= true,
};
