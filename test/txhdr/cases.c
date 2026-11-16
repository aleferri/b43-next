// SPDX-License-Identifier: GPL-2.0
/*
 * The TX headers of b43-next/xmit.c for a set of devices, rates and frames.
 * Each case hands out.c b43_generate_txhdr()'s return, the header, the
 * Duration it left in the frame and the rates it left in the TX info.
 */
#include "b43.h"
#include "xmit.h"
#include "phy_ac.h"

void txhdr_emit(const char *name, int ret, const u8 *hdr, unsigned int len,
		const u8 *tail, unsigned int tail_len);
void txhdr_value(const char *name, unsigned int value);
void txhdr_text(const char *name, const char *text);

struct dev_spec {
	const char *name;
	u8 phy_type;
	bool nphy_ht;
	const struct b43_fw_hdr *hdr;
	enum nl80211_band band;
	u16 freq;		/* the primary channel's */
	u16 center;
	enum nl80211_chan_width width;
	u8 txchain, rxchain;
	bool ht, vht;
};

static const struct dev_spec devs[] = {
	{ "g-410", B43_PHYTYPE_G, false, &b43_hdr_410, NL80211_BAND_2GHZ,
	  2437, 2437, NL80211_CHAN_WIDTH_20_NOHT, 1, 1 },
	{ "lp-351", B43_PHYTYPE_LP, false, &b43_hdr_351, NL80211_BAND_2GHZ,
	  2412, 2412, NL80211_CHAN_WIDTH_20_NOHT, 1, 1 },
	{ "n-598-5g", B43_PHYTYPE_N, false, &b43_hdr_598, NL80211_BAND_5GHZ,
	  5180, 5180, NL80211_CHAN_WIDTH_20_NOHT, 3, 3 },
	{ "nht-20", B43_PHYTYPE_N, true, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2437, 2437, NL80211_CHAN_WIDTH_20, 3, 3, true },
	{ "nht-40u", B43_PHYTYPE_N, true, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2437, 2447, NL80211_CHAN_WIDTH_40, 3, 3, true },
	{ "nht-40l", B43_PHYTYPE_N, true, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2457, 2447, NL80211_CHAN_WIDTH_40, 3, 3, true },
	{ "nht-1tx", B43_PHYTYPE_N, true, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2437, 2437, NL80211_CHAN_WIDTH_20, 1, 3, true },
	{ "ht-20", B43_PHYTYPE_HT, false, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2462, 2462, NL80211_CHAN_WIDTH_20, 1, 7, true },
	{ "ht-40", B43_PHYTYPE_HT, false, &b43_hdr_598, NL80211_BAND_2GHZ,
	  2437, 2447, NL80211_CHAN_WIDTH_40, 1, 7, true },
	{ "ac-2g", B43_PHYTYPE_AC, false, &b43_ac_hdr_784, NL80211_BAND_2GHZ,
	  2437, 2437, NL80211_CHAN_WIDTH_20, 3, 3, true },
	{ "ac-5g80", B43_PHYTYPE_AC, false, &b43_ac_hdr_784, NL80211_BAND_5GHZ,
	  5180, 5210, NL80211_CHAN_WIDTH_80, 3, 3, true, true },
};

#define RATE(_hw, _f) { .bitrate = B43_RATE_TO_BASE100KBPS(_hw), \
			.hw_value = (_hw), .flags = (_f) }
static struct ieee80211_rate rates_g[] = {
	RATE(B43_CCK_RATE_1MB, 0),
	RATE(B43_CCK_RATE_2MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(B43_CCK_RATE_5MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(B43_CCK_RATE_11MB, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(B43_OFDM_RATE_6MB, 0), RATE(B43_OFDM_RATE_9MB, 0),
	RATE(B43_OFDM_RATE_12MB, 0), RATE(B43_OFDM_RATE_18MB, 0),
	RATE(B43_OFDM_RATE_24MB, 0), RATE(B43_OFDM_RATE_36MB, 0),
	RATE(B43_OFDM_RATE_48MB, 0), RATE(B43_OFDM_RATE_54MB, 0),
};

static struct wiphy wiphy;
static struct ieee80211_hw hw;
static struct b43_wl wl;
static struct b43_wldev dev;
static struct b43_bus_dev busdev;
static struct ssbn_sprom sprom;
static struct b43_phy_ac ac;
static struct ieee80211_channel chan;
static struct cfg80211_chan_def chandef;
static struct ieee80211_supported_band band;
static struct ieee80211_vif vif;
static struct {
	struct ieee80211_sta sta;
	struct b43_sta priv;
} sta;
static struct {
	struct ieee80211_key_conf conf;
	u8 key[32];
} key;
static struct sk_buff skb;
static u8 frame[256];
static u8 txhdr[256];

/* For kstubs.c */
struct ieee80211_sta *txhdr_sta = &sta.sta;

static const struct dev_spec *cur;

static void setup(const struct dev_spec *d)
{
	struct ieee80211_sta_ht_cap *ht = &band.ht_cap;
	unsigned int i, rx_streams = hweight8(d->rxchain);
	unsigned int tx_streams = hweight8(d->txchain);

	cur = d;
	memset(&dev, 0, sizeof(dev));
	memset(&wl, 0, sizeof(wl));
	memset(&band, 0, sizeof(band));
	memset(&ac, 0, sizeof(ac));
	memset(&wiphy, 0, sizeof(wiphy));
	memset(&hw, 0, sizeof(hw));

	hw.wiphy = &wiphy;
	hw.priv = &wl;
	hw.conf.long_frame_max_tx_count = 4;
	hw.conf.short_frame_max_tx_count = 7;
	wl.hw = &hw;
	wl.current_dev = &dev;
	for (i = 0; i < B43_QOS_QUEUE_NUM; i++)
		wl.qos_params[i].p.txop = i == 1 ? 94 : 0;

	busdev.bus_sprom = &sprom;
	sprom.txchain = d->txchain;
	dev.dev = &busdev;
	dev.wl = &wl;
	dev.txchain = d->txchain;
	dev.rxchain = d->rxchain;
	dev.fw.layout.hdr = d->hdr;
	dev.fw.layout.new_kidx_api = d->hdr == &b43_ac_hdr_784;
	dev.fw.layout.sec_algo_d11 = d->hdr == &b43_ac_hdr_784;

	chan.band = d->band;
	chan.center_freq = d->freq;
	chandef.chan = &chan;
	chandef.width = d->width;
	chandef.center_freq1 = d->center;
	dev.phy.type = d->phy_type;
	dev.phy.nphy_ht = d->nphy_ht;
	dev.phy.gmode = d->band == NL80211_BAND_2GHZ;
	dev.phy.chandef = &chandef;
	dev.phy.channel = ieee80211_frequency_to_channel(d->freq);

	band.band = d->band;
	if (d->band == NL80211_BAND_2GHZ) {
		band.bitrates = rates_g;
		band.n_bitrates = ARRAY_SIZE(rates_g);
	} else {
		band.bitrates = rates_g + 4;
		band.n_bitrates = ARRAY_SIZE(rates_g) - 4;
	}
	if (d->ht) {
		ht->ht_supported = true;
		ht->cap = IEEE80211_HT_CAP_SGI_20;
		if (d->width >= NL80211_CHAN_WIDTH_40)
			ht->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				   IEEE80211_HT_CAP_SGI_40;
		for (i = 0; i < rx_streams; i++)
			ht->mcs.rx_mask[i] = 0xff;
		ht->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
		if (tx_streams != rx_streams)
			ht->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF |
				(tx_streams - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT;
	}
	if (d->vht) {
		u16 map = 0xffff;

		for (i = 0; i < tx_streams; i++)
			map &= ~(3 << (2 * i)) |
			       IEEE80211_VHT_MCS_SUPPORT_0_9 << (2 * i);
		band.vht_cap.vht_supported = true;
		band.vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(map);
	}
	wiphy.bands[d->band] = &band;

	if (d->phy_type == B43_PHYTYPE_AC) {
		dev.phy.ac = &ac;
		ac.coremask = d->txchain;
		ac.chanspec = 0x1000 | dev.phy.channel |
			      (d->width == NL80211_CHAN_WIDTH_80 ? 0x2c00 : 0);
		for (i = 0; i < B43_PHY_AC_TXCORE_NUM; i++)
			ac.txcore[i] = i & 1 ? 0 : 1 + i % 3;
		for (i = 0; i < 8; i++)
			ac.rate_po_ofdm[i] = 3 * i + 1;
		ac.rate_po_cck = 5;
		for (i = 0; i < B43_PPR_BW_NUM * B43_PPR_MCS_RATES; i++)
			ac.rate_po_vht[i / B43_PPR_MCS_RATES]
				      [i % B43_PPR_MCS_RATES] = 0x40 + i;
	}

	memset(&vif, 0, sizeof(vif));
	vif.bss_conf.basic_rates = d->band == NL80211_BAND_2GHZ ?
				   0x0f | BIT(4) | BIT(6) | BIT(8) :
				   BIT(0) | BIT(2) | BIT(4);
	memset(&sta, 0, sizeof(sta));
	sta.sta.deflink.ht_cap = *ht;
	sta.sta.deflink.ht_cap.ampdu_density = 5;
	sta.sta.deflink.ht_cap.ampdu_factor = 3;
	sta.sta.deflink.vht_cap.vht_supported = d->vht;
}

/* A rate is there with an index or flags; P marks legacy rate 0. */
struct tx_rate_spec {
	s8 idx;
	u16 flags;
};

#define P	0x8000

struct tx_case {
	const char *name;
	struct tx_rate_spec r[4];
	u32 info_flags;
	u32 ctl_flags;
	bool morefrags;
	u16 fc;			/* 0: QoS data */
	bool cts_prot, short_preamble;
	u16 ht_op;
	u32 key_cipher;
	const struct b43_txhdr_ampdu *ampdu;
	u8 ampdu_win;
	u8 txbf_slot;
	bool throttled;
};

static void run(const struct tx_case *c)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(&skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)frame;
	u16 fc = c->fc ? c->fc : IEEE80211_FTYPE_DATA |
				 IEEE80211_STYPE_QOS_DATA;
	unsigned int hdrlen, i, len;
	u8 tail[2 + 4 * IEEE80211_TX_MAX_RATES];
	char name[96];
	int ret;

	memset(frame, 0, sizeof(frame));
	hdr->frame_control = cpu_to_le16(fc |
		(c->morefrags ? IEEE80211_FCTL_MOREFRAGS : 0));
	hdr->duration_id = cpu_to_le16(c->fc ? 0 : 44);
	memcpy(hdr->addr1, "\x02\x11\x22\x33\x44\x01", ETH_ALEN);
	memcpy(hdr->addr2, "\x02\xaa\xbb\xcc\xdd\x00", ETH_ALEN);
	memcpy(hdr->addr3, "\x02\xaa\xbb\xcc\xdd\x00", ETH_ALEN);
	hdr->seq_ctrl = cpu_to_le16(0x1230);
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (ieee80211_is_data_qos(hdr->frame_control))
		*ieee80211_get_qos_ctl(hdr) = 5;
	len = hdrlen + 200;
	for (i = hdrlen; i < len; i++)
		frame[i] = i;

	memset(info, 0, sizeof(*info));
	info->band = cur->band;
	info->flags = c->info_flags | IEEE80211_TX_CTL_FIRST_FRAGMENT;
	info->control.vif = &vif;
	info->control.flags = c->ctl_flags;
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		const struct tx_rate_spec *s = &c->r[i];
		struct ieee80211_tx_rate *r = &info->control.rates[i];

		if (s->idx < 0 || (!s->idx && !s->flags)) {
			r->idx = -1;
			continue;
		}
		r->idx = s->idx;
		r->flags = s->flags & ~P;
		r->count = 2;
	}

	vif.bss_conf.use_cts_prot = c->cts_prot;
	vif.bss_conf.use_short_preamble = c->short_preamble;
	vif.bss_conf.ht_operation_mode = c->ht_op;

	memset(&key, 0, sizeof(key));
	memset(dev.key, 0, sizeof(dev.key));
	if (c->key_cipher) {
		bool tkip = c->key_cipher == WLAN_CIPHER_SUITE_TKIP;

		key.conf.cipher = c->key_cipher;
		key.conf.hw_key_idx = 5;
		key.conf.iv_len = 8;
		key.conf.icv_len = tkip ? 4 : 8;
		dev.key[5].keyconf = &key.conf;
		dev.key[5].algorithm = tkip ? B43_SEC_ALGO_TKIP :
					      B43_SEC_ALGO_AES;
		info->control.hw_key = &key.conf;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		memcpy(frame + hdrlen, "\x01\x02\x00\x20\x05\x06\x07\x08", 8);
	}

	sta.priv.ampdu_win[5] = c->ampdu_win;
	sta.priv.txbf_slot = c->txbf_slot;
	dev.txbf_base = c->txbf_slot ? 0x100 : 0;
	ac.thermal_throttled = c->throttled;

	skb.data = frame;
	skb.len = len;
	skb.queue_mapping = 1;

	memset(txhdr, 0xa5, sizeof(txhdr));
	ret = b43_generate_txhdr(&dev, txhdr, &skb, info, 0x4321, c->ampdu);

	memcpy(tail, &hdr->duration_id, 2);
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		struct ieee80211_tx_rate *r = &info->control.rates[i];

		tail[2 + 4 * i] = r->idx;
		tail[3 + 4 * i] = r->count;
		tail[4 + 4 * i] = r->flags;
		tail[5 + 4 * i] = r->flags >> 8;
	}
	snprintf(name, sizeof(name), "%s %s", cur->name, c->name);
	txhdr_emit(name, ret, txhdr, cur->hdr->tx_len, tail, sizeof(tail));
}

#define SP	IEEE80211_TX_RC_USE_SHORT_PREAMBLE
#define MCS	IEEE80211_TX_RC_MCS
#define VHT	IEEE80211_TX_RC_VHT_MCS
#define SGI	IEEE80211_TX_RC_SHORT_GI
#define W40	IEEE80211_TX_RC_40_MHZ_WIDTH
#define W80	IEEE80211_TX_RC_80_MHZ_WIDTH
#define RTS	IEEE80211_TX_RC_USE_RTS_CTS
#define CTS	IEEE80211_TX_RC_USE_CTS_PROTECT

static void legacy(void)
{
	unsigned int i;
	char name[32];

	for (i = 0; i < band.n_bitrates; i++) {
		bool sp = band.bitrates[i].flags & IEEE80211_RATE_SHORT_PREAMBLE;
		struct tx_case c = {
			.name = name,
			.r = { { i, P | (sp ? SP : 0) },
			       { i ? i - 1 : -1, P | (i > 1 && sp ? SP : 0) } },
			.short_preamble = sp,
		};

		snprintf(name, sizeof(name), "legacy %u", i);
		run(&c);
		c.r[0].flags |= RTS;
		snprintf(name, sizeof(name), "legacy %u rts", i);
		run(&c);
		c.r[0].flags ^= RTS | CTS;
		snprintf(name, sizeof(name), "legacy %u cts", i);
		run(&c);
		c.r[0].flags &= ~CTS;
		c.fc = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_RESP;
		c.r[1].idx = -1;
		snprintf(name, sizeof(name), "legacy %u mgmt", i);
		run(&c);
	}
}

/* The MCS of up to a stream per TX core, as b43_tx_rates_ok() takes them */
static bool mcs_ok(unsigned int mcs)
{
	return mcs < 8 * hweight8(cur->txchain);
}

static void ht(void)
{
	static const u8 mcs[] = { 0, 1, 4, 7, 8, 11, 15, 16, 19, 23 };
	bool w40 = cur->width >= NL80211_CHAN_WIDTH_40;
	u16 flagsets[] = { 0, SGI, W40, SGI | W40 };
	unsigned int i, f;
	char name[48];

	for (i = 0; i < ARRAY_SIZE(mcs); i++) {
		for (f = 0; f < ARRAY_SIZE(flagsets); f++) {
			u16 fl = MCS | flagsets[f];
			struct tx_case c = {
				.name = name,
				.r = { { mcs[i], fl },
				       { mcs[i] ? mcs[i] - 1 : -1, fl } },
			};

			if (((fl & W40) && !w40) || !mcs_ok(mcs[i]))
				continue;
			snprintf(name, sizeof(name), "mcs %u/%#x", mcs[i], fl);
			run(&c);
			if (mcs[i] < 8) {
				c.info_flags = IEEE80211_TX_CTL_STBC |
					       IEEE80211_TX_CTL_LDPC;
				snprintf(name, sizeof(name),
					 "mcs %u/%#x stbc ldpc", mcs[i], fl);
				run(&c);
			}
		}
	}
}

static void ht_frames(void)
{
	static const struct b43_txhdr_ampdu first = {
		.first = true, .len = 3000, .ndelim = 2, .preload = 0x300,
	}, inter = { .len = 3000, .ndelim = 1 }, last = {
		.last = true, .len = 3000,
	};
	const u8 m = 7, l = cur->band == NL80211_BAND_2GHZ ? 4 : 0;
	const u8 hi = 8 * hweight8(cur->txchain) - 1, mid = hi > 7 ? 12 : 5;
	const u16 w = cur->width >= NL80211_CHAN_WIDTH_40 ? W40 : 0;
	const struct tx_case cases[] = {
		{ "ht legacy-fb", { { m, MCS | SGI }, { l, P } } },
		{ "ht no-fb", { { hi, MCS }, { -1 } } },
		{ "ht rts", { { m, MCS | RTS }, { 3, MCS } } },
		{ "ht cts", { { m, MCS | CTS }, { 3, MCS } } },
		{ "ht rts-on-fb", { { m, MCS }, { 3, MCS | RTS } } },
		{ "ht erp", { { m, MCS }, { 3, MCS } }, .cts_prot = true },
		{ "ht nonmember", { { m, MCS }, { 3, MCS } },
		  .ht_op = IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER },
		{ "ht 20prot", { { m, MCS | w }, { 3, MCS | w } },
		  .ht_op = IEEE80211_HT_OP_MODE_PROTECTION_20MHZ },
		{ "ht noack", { { m, MCS }, { 3, MCS } },
		  .info_flags = IEEE80211_TX_CTL_NO_ACK },
		{ "ht morefrags", { { m, MCS }, { 3, MCS } }, .morefrags = true },
		{ "ht ccmp", { { m, MCS }, { 3, MCS } },
		  .key_cipher = WLAN_CIPHER_SUITE_CCMP },
		{ "ht tkip", { { m, MCS }, { 3, MCS } },
		  .key_cipher = WLAN_CIPHER_SUITE_TKIP },
		{ "ht ampdu-first", { { m, MCS | SGI }, { 3, MCS } },
		  .ampdu = &first, .ampdu_win = 64 },
		{ "ht ampdu-inter", { { m, MCS }, { 3, MCS } }, .ampdu = &inter,
		  .ampdu_win = 64 },
		{ "ht ampdu-last", { { m, MCS }, { 3, MCS } }, .ampdu = &last,
		  .ampdu_win = 64 },
		{ "ht ampdu-legacy", { { l, P }, { -1 } }, .ampdu = &first },
		{ "ht ampdu-flag", { { m, MCS }, { 3, MCS } },
		  .info_flags = IEEE80211_TX_CTL_AMPDU, .ampdu_win = 32 },
		{ "ht ampdu-flag-nowin", { { m, MCS }, { 3, MCS } },
		  .info_flags = IEEE80211_TX_CTL_AMPDU },
		{ "ht 4rates", { { hi, MCS }, { mid, MCS }, { 3, MCS },
				 { l, P } } },
		{ "ht fastxmit", { { l + 2, P }, { l, P } },
		  .ctl_flags = IEEE80211_TX_CTRL_FAST_XMIT },
		{ "ht txbf", { { m, MCS }, { 3, MCS } }, .txbf_slot = 2 },
		{ "ht throttled", { { hi, MCS }, { mid, MCS }, { 3, MCS } },
		  .throttled = true },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++)
		run(&cases[i]);
}

static void vht(void)
{
	static const u16 widths[] = { 0, W40, W80 };
	unsigned int mcs, nss, w, sgi;
	char name[48];

	for (nss = 1; nss <= 2; nss++)
		for (w = 0; w < ARRAY_SIZE(widths); w++)
			for (mcs = 0; mcs <= 9; mcs++)
				for (sgi = 0; sgi < 2; sgi++) {
					u16 fl = VHT | widths[w] |
						 (sgi ? SGI : 0);
					struct tx_case c = {
						.name = name,
						.r = { { (nss - 1) << 4 | mcs, fl },
						       { (nss - 1) << 4 |
							 (mcs ? mcs - 1 : 0),
							 fl } },
						.info_flags = mcs & 1 ?
							IEEE80211_TX_CTL_LDPC : 0,
					};

					snprintf(name, sizeof(name),
						 "vht %ux%u/%#x", nss, mcs, fl);
					run(&c);
				}
}

static void ampdu_limits(void)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(&skb);
	static const u8 mcs[] = { 0, 3, 7, 8, 15, 16, 23 };
	static const unsigned int lens[] = { 1500, 8000, 30000, 65535 };
	bool w40 = cur->width >= NL80211_CHAN_WIDTH_40;
	unsigned int i, f, j;
	char name[64];

	for (i = 0; i < ARRAY_SIZE(mcs); i++)
		for (f = 0; f < 4; f++) {
			u16 fl = MCS | (f & 1 ? SGI : 0) | (f & 2 ? W40 : 0);

			if (((fl & W40) && !w40) || !mcs_ok(mcs[i]))
				continue;
			memset(info, 0, sizeof(*info));
			info->control.rates[0].idx = mcs[i];
			info->control.rates[0].flags = fl;
			info->control.rates[1].idx = -1;
			for (j = 0; j < ARRAY_SIZE(lens); j++) {
				snprintf(name, sizeof(name),
					 "%s fits mcs %u/%#x len %u", cur->name,
					 mcs[i], fl, lens[j]);
				txhdr_value(name,
					    b43_ampdu_tx_fits(&dev, info, lens[j]));
			}
			info->flags = IEEE80211_TX_CTL_STBC;
			snprintf(name, sizeof(name), "%s fits mcs %u/%#x stbc",
				 cur->name, mcs[i], fl);
			txhdr_value(name, b43_ampdu_tx_fits(&dev, info, 30000));
			for (j = 0; j < 8; j++) {
				snprintf(name, sizeof(name),
					 "%s spacing mcs %u/%#x density %u",
					 cur->name, mcs[i], fl, j);
				txhdr_value(name,
					    b43_ampdu_tx_spacing(&dev, info, j));
			}
		}
}

/* Which pairs of a primary and a fallback rate b43_tx_rates_ok() takes. */
static void rates_ok(void)
{
	static const struct tx_rate_spec set[] = {
		{ 0, P }, { 11, P }, { 12, P },
		{ 7, MCS }, { 15, MCS }, { 16, MCS | SGI }, { 23, MCS },
		{ 24, MCS }, { 32, MCS | W40 },
		{ 1 << 4 | 9, VHT }, { 2 << 4 | 9, VHT | W80 },
		{ 3 << 4 | 7, VHT }, { 1 << 4 | 11, VHT },
		{ 1 << 4 | 9, VHT | IEEE80211_TX_RC_160_MHZ_WIDTH },
	};
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(&skb);
	char line[ARRAY_SIZE(set) + 2], name[48];
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(set); i++) {
		for (j = 0; j <= ARRAY_SIZE(set); j++) {
			struct ieee80211_tx_rate *r = info->control.rates;

			memset(info, 0, sizeof(*info));
			info->band = cur->band;
			r[0].idx = set[i].idx;
			r[0].flags = set[i].flags & ~P;
			r[0].count = 2;
			r[1].idx = j < ARRAY_SIZE(set) ? set[j].idx : -1;
			r[1].flags = j < ARRAY_SIZE(set) ? set[j].flags & ~P : 0;
			r[1].count = 2;
			r[2].idx = -1;
			line[j] = '0' + b43_tx_rates_ok(&dev, info);
		}
		line[j] = 0;
		snprintf(name, sizeof(name), "%s rates_ok %d/%#x", cur->name,
			 set[i].idx, set[i].flags & ~P);
		txhdr_text(name, line);
	}
}

void txhdr_run(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(devs); i++) {
		setup(&devs[i]);
		legacy();
		if (devs[i].ht) {
			ht();
			ht_frames();
		}
		if (devs[i].vht)
			vht();
		if (devs[i].ht && devs[i].phy_type != B43_PHYTYPE_AC)
			ampdu_limits();
		rates_ok();
	}
}
