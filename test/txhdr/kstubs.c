// SPDX-License-Identifier: GPL-2.0
/*
 * What xmit.c's TX header path calls outside it. The mac80211 and cfg80211
 * stand-ins return values made of their arguments, so that a header that
 * asks for a different duration or bitrate reads differently.
 */
#include "b43.h"
#include "xmit.h"

extern struct ieee80211_sta *txhdr_sta;

u32 cfg80211_calculate_bitrate(struct rate_info *ri)
{
	return 5 * ((ri->flags & 0x1f) << 10 | ri->mcs << 6 | ri->nss << 3 |
		    ri->bw);
}

__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					enum nl80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	return cpu_to_le16(band * 7 + frame_len * 3 + rate->bitrate +
			   (vif && vif->bss_conf.use_short_preamble) * 1000);
}

void ieee80211_rts_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_info *info,
		       struct ieee80211_rts *rts)
{
	const struct ieee80211_hdr *hdr = frame;

	rts->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					 IEEE80211_STYPE_RTS);
	rts->duration = cpu_to_le16(0x100 + frame_len +
				    info->control.rates[0].idx);
	memcpy(rts->ra, hdr->addr1, ETH_ALEN);
	memcpy(rts->ta, hdr->addr2, ETH_ALEN);
}

void ieee80211_ctstoself_get(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     const void *frame, size_t frame_len,
			     const struct ieee80211_tx_info *info,
			     struct ieee80211_cts *cts)
{
	const struct ieee80211_hdr *hdr = frame;

	cts->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					 IEEE80211_STYPE_CTS);
	cts->duration = cpu_to_le16(0x200 + frame_len +
				    info->control.rates[0].idx);
	memcpy(cts->ra, hdr->addr2, ETH_ALEN);
}

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr)
{
	return txhdr_sta;
}

unsigned int ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;

	if (ieee80211_is_ext(fc))
		return 4;
	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc))
			hdrlen = 30;
		if (ieee80211_is_data_qos(fc)) {
			hdrlen += IEEE80211_QOS_CTL_LEN;
			if (ieee80211_has_order(fc))
				hdrlen += IEEE80211_HT_CTL_LEN;
		}
		return hdrlen;
	}
	if (ieee80211_is_mgmt(fc))
		return ieee80211_has_order(fc) ? 28 : 24;
	if (ieee80211_is_ctl(fc))
		return ieee80211_is_cts(fc) || ieee80211_is_ack(fc) ? 10 : 16;
	return hdrlen;
}

void ieee80211_get_tkip_p1k_iv(struct ieee80211_key_conf *keyconf,
			       u32 iv32, u16 *p1k)
{
	unsigned int i;

	for (i = 0; i < 5; i++)
		p1k[i] = iv32 + i;
}

int ieee80211_freq_khz_to_channel(u32 freq)
{
	freq /= 1000;
	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return (freq - 2407) / 5;
	return (freq - 5000) / 5;
}

bool b43_is_40mhz(struct b43_wldev *dev)
{
	return dev->phy.chandef->width == NL80211_CHAN_WIDTH_40;
}

unsigned int b43_phy_center_channel(struct b43_wldev *dev)
{
	return ieee80211_frequency_to_channel(dev->phy.chandef->center_freq1);
}

u8 b43_ieee80211_antenna_sanitize(struct b43_wldev *dev, u8 antenna_nr)
{
	return 0;
}

void __rcu_read_lock(void)
{
}

void __rcu_read_unlock(void)
{
}

/*
 * Called from arch_hweight.h's inline asm, which declares %rdi in and %rax
 * out only: every other register is the caller's, as arch/x86/lib/hweight.S
 * keeps them.
 */
__attribute__((no_caller_saved_registers))
unsigned int __sw_hweight32(unsigned int w)
{
	w -= (w >> 1) & 0x55555555;
	w = (w & 0x33333333) + ((w >> 2) & 0x33333333);
	w = (w + (w >> 4)) & 0x0f0f0f0f;
	return (w * 0x01010101) >> 24;
}

u16 b43_antenna_to_phyctl(struct b43_wldev *dev, int antenna)
{
	if (b43_ht_core(dev))
		return dev->txchain << B43_TXH_PHY_ANT_SHIFT;
	if (dev->phy.type == B43_PHYTYPE_LCN)
		return B43_TXH_PHY_ANT_LCN_LAST;
	return B43_TXH_PHY_ANT01AUTO;
}
