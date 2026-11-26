// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  Beacons: the two beacon templates and the further APs' beacons

  Moved out of main.c; the code and its copyright are main.c's:

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2009 Michael Buesch <m@bues.ch>
  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (c) 2010-2011 Rafał Miłecki <zajec5@gmail.com>
  Copyright (c) 2026 Alessio Ferri <alessio.ferri.3012@gmail.com>
  Copyright (c) 2026 Andreas Wendleder <andreas.wendleder@proton.me>
  Copyright (c) 2026 Ville Takio <ville+git@takio.fi>

*/

#include <linux/skbuff.h>
#include <linux/workqueue.h>

#include "b43.h"
#include "main.h"
#include "beacon.h"
#include "phy_common.h"
#include "xmit.h"

/* The modes in which the first interface sends beacons from the templates. */
static bool b43_beacon_mode(struct b43_wl *wl)
{
	return b43_is_mode(wl, NL80211_IFTYPE_AP) ||
	       b43_is_mode(wl, NL80211_IFTYPE_MESH_POINT) ||
	       b43_is_mode(wl, NL80211_IFTYPE_ADHOC);
}

bool b43_beaconing(struct b43_wl *wl)
{
	return wl->vif && wl->vif->bss_conf.enable_beacon;
}

/*
 * The beacons of the further AP interfaces, which the microcode's single
 * beacon template cannot hold: fetched from mac80211 at the primary's TBTT
 * and queued as management frames on the voice queue, with the TSF of now
 * in the timestamp. Their DTIM and TIM are mac80211's, per interface.
 */
static void b43_mbss_beacons(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_vif *vif;
	struct sk_buff *skb;
	bool queued = false;
	unsigned int i;
	u64 tsf = 0;

	/*
	 * These beacons bypass mac80211's flow control: skip them while the
	 * voice queue is stopped or backed up, or they pile up without end
	 * when TX stalls. A beacon that missed its TBTT is stale anyway.
	 */
	if (wl->tx_queue_stopped[IEEE80211_AC_VO] ||
	    skb_queue_len(&wl->tx_queue[IEEE80211_AC_VO]) >= 2 * B43_MBSS_MAX)
		return;

	for (i = 0; i < ARRAY_SIZE(wl->mbss_vif); i++) {
		vif = wl->mbss_vif[i];
		if (!vif || !vif->bss_conf.enable_beacon)
			continue;
		skb = ieee80211_beacon_get(wl->hw, vif, 0);
		if (!skb)
			continue;
		if (!queued)
			b43_tsf_read(dev, &tsf);
		mgmt = (struct ieee80211_mgmt *)skb->data;
		mgmt->u.beacon.timestamp = cpu_to_le64(tsf);
		skb_set_queue_mapping(skb, IEEE80211_AC_VO);
		skb_queue_tail(&wl->tx_queue[IEEE80211_AC_VO], skb);
		queued = true;
	}
	if (queued && !wl->tx_queue_stopped[IEEE80211_AC_VO])
		ieee80211_queue_work(wl->hw, &wl->tx_work);
}

/* Byte @i of a template: a @hdr_len header with the PLCP at @plcp_off, then
 * the frame. */
static u8 b43_tpl_byte(u8 hdr_len, u8 plcp_off,
		       const struct b43_plcp_hdr4 *plcp,
		       const u8 *data, u16 size, unsigned int i)
{
	if (i >= hdr_len)
		return i - hdr_len < size ? data[i - hdr_len] : 0;
	if (i >= plcp_off && i - plcp_off < sizeof(plcp->raw))
		return plcp->raw[i - plcp_off];
	return 0;
}

static void b43_write_template_common(struct b43_wldev *dev,
				      const u8 *data, u16 size,
				      u16 ram_offset,
				      u16 shm_size_offset, u8 rate)
{
	const struct b43_tpl_layout *tpl = dev->fw.layout.tpl;
	u8 plcp_off = b43_is_cck_rate(rate) ? tpl->plcp_off_cck : tpl->plcp_off;
	unsigned int i, total = tpl->hdr_len + size;
	struct b43_ram_seq seq = { .next = U16_MAX }, *s = NULL;
	struct b43_plcp_hdr4 plcp;

	if (dev->phy.type == B43_PHYTYPE_AC) {
		seq.swap = b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_BE;
		s = &seq;
	}

	plcp.data = 0;
	b43_generate_plcp_hdr(&plcp, size + FCS_LEN, rate);

	for (i = 0; i < total; i += sizeof(u32)) {
		u32 tmp = 0;
		unsigned int b;

		for (b = 0; b < sizeof(u32); b++)
			tmp |= (u32)b43_tpl_byte(tpl->hdr_len, plcp_off, &plcp,
						 data, size, i + b) << (8 * b);
		b43_tpl_write(dev, s, ram_offset + i, tmp);
	}
	b43_shm_write16(dev, B43_SHM_SHARED, shm_size_offset, total);
}

static void b43_write_beacon_phytxctl(struct b43_wldev *dev, u16 rate)
{
	u16 ctl;

	ctl = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL);
	/* We can't send beacons with short preamble. Would get PHY errors. */
	ctl &= ~B43_TXH_PHY_SHORTPRMBL;
	ctl &= ~B43_TXH_PHY_ANT;
	ctl &= ~B43_TXH_PHY_ENC;
	ctl |= b43_antenna_to_phyctl(dev, B43_ANTENNA_DEFAULT);
	if (b43_is_cck_rate(rate))
		ctl |= B43_TXH_PHY_ENC_CCK;
	else
		ctl |= B43_TXH_PHY_ENC_OFDM;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL, ctl);

	/*
	 * Word 1 of the N and HT microcode's beacon PHY control is in
	 * EXTNPHYCTL: its bandwidth field picks the 20 MHz half of a 40 MHz
	 * channel, as b43_generate_tx_phy_ctl1() does for a frame's. Left
	 * alone, the first BSS's beacon went out on the lower half, the
	 * secondary channel at HT40- (egc112, R6400 v2, b43-next#33).
	 */
	if (dev->phy.type == B43_PHYTYPE_N || dev->phy.type == B43_PHYTYPE_HT) {
		const struct cfg80211_chan_def *cd = dev->phy.chandef;
		u16 ctl1;

		ctl1 = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_EXTNPHYCTL);
		ctl1 &= ~B43_TXH_PHY1_BW;
		if (b43_is_40mhz(dev) && cd &&
		    cd->chan->center_freq > cd->center_freq1)
			ctl1 |= B43_TXH_PHY1_BW_20U;
		else
			ctl1 |= B43_TXH_PHY1_BW_20;
		b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_EXTNPHYCTL, ctl1);
	}
}

/*
 * The PHY keeps the chain mask in this word (b43_phy_ac_bss_cc_update()) and
 * the beacon's power offset in the next, which follows the rate class
 * (b43_phy_ac_op_beacon_rate_set()). The encoding is the core's: the
 * stock drivers set it before each beacon template, 0x00c4 to 0x00c5 on the
 * DSL-3580L, 0x0144 to 0x0145 on the agcombo; the initvals' 0x0044 says CCK,
 * which 5 GHz cannot carry.
 */
static void b43_write_beacon_phytxctl_ac(struct b43_wldev *dev, u16 rate)
{
	u16 ctl;

	ctl = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL_AC);
	ctl &= ~B43_TXH_PHY_ENC;
	ctl |= b43_is_cck_rate(rate) ? B43_TXH_PHY_ENC_CCK : B43_TXH_PHY_ENC_OFDM;
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_BEACPHYCTL_AC, ctl);
	if (dev->phy.ops->beacon_rate_set)
		dev->phy.ops->beacon_rate_set(dev, b43_is_cck_rate(rate));
}

static void b43_write_beacon_template(struct b43_wldev *dev,
				      u16 ram_offset,
				      u16 shm_size_offset)
{
	const struct b43_tpl_layout *tpl = dev->fw.layout.tpl;
	unsigned int i, len, variable_len;
	const struct ieee80211_mgmt *bcn;
	const u8 *ie;
	bool tim_found = false;
	unsigned int rate;
	struct ieee80211_tx_info *info;
	unsigned long flags;
	struct sk_buff *beacon_skb;

	spin_lock_irqsave(&dev->wl->beacon_lock, flags);
	info = IEEE80211_SKB_CB(dev->wl->current_beacon);
	rate = ieee80211_get_tx_rate(dev->wl->hw, info)->hw_value;
	/* Clone the beacon, so it cannot go away, while we write it to hw. */
	beacon_skb = skb_clone(dev->wl->current_beacon, GFP_ATOMIC);
	spin_unlock_irqrestore(&dev->wl->beacon_lock, flags);

	if (!beacon_skb) {
		b43dbg(dev->wl, "Could not upload beacon. "
		       "Failed to clone beacon skb.");
		return;
	}

	bcn = (const struct ieee80211_mgmt *)(beacon_skb->data);
	len = min_t(size_t, beacon_skb->len,
		    tpl->bcn_size - tpl->hdr_len);

	b43_write_template_common(dev, (const u8 *)bcn,
				  len, ram_offset, shm_size_offset, rate);

	if (dev->fw.layout.phytxctl_shm)
		b43_write_beacon_phytxctl(dev, rate);
	else
		b43_write_beacon_phytxctl_ac(dev, rate);

	/* Find the position of the TIM and the DTIM_period value
	 * and write them to SHM. */
	ie = bcn->u.beacon.variable;
	variable_len = len - offsetof(struct ieee80211_mgmt, u.beacon.variable);
	for (i = 0; i < variable_len - 2; ) {
		u8 ie_id, ie_len;

		ie_id = ie[i];
		ie_len = ie[i + 1];
		if (ie_id == 5) {
			u16 tim_position;
			u16 dtim_period;
			/* This is the TIM Information Element */

			/* Check whether the ie_len is in the beacon data range. */
			if (variable_len < ie_len + 2 + i)
				break;
			/* A valid TIM is at least 4 bytes long. */
			if (ie_len < 4)
				break;
			tim_found = true;

			tim_position = tpl->hdr_len;
			tim_position += offsetof(struct ieee80211_mgmt, u.beacon.variable);
			tim_position += i;

			dtim_period = ie[i + 3];

			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_SHM_SH_TIMBPOS, tim_position);
			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_SHM_SH_DTIMPER, dtim_period);
			break;
		}
		i += ie_len + 2;
	}
	if (!tim_found) {
		/*
		 * If ucode wants to modify TIM do it behind the beacon, this
		 * will happen, for example, when doing mesh networking.
		 */
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_TIMBPOS,
				len + tpl->hdr_len);
		b43_shm_write16(dev, B43_SHM_SHARED,
				B43_SHM_SH_DTIMPER, 0);
	}
	b43dbg(dev->wl, "Updated beacon template at 0x%x\n", ram_offset);

	dev_kfree_skb_any(beacon_skb);
}

static void b43_upload_beacon0(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;

	if (wl->beacon0_uploaded)
		return;
	b43_write_beacon_template(dev, dev->fw.layout.tpl->bcn_base[0],
				  B43_SHM_SH_BTL0);
	wl->beacon0_uploaded = true;
}

static void b43_upload_beacon1(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;

	if (wl->beacon1_uploaded)
		return;
	b43_write_beacon_template(dev, dev->fw.layout.tpl->bcn_base[1],
				  B43_SHM_SH_BTL1);
	wl->beacon1_uploaded = true;
}

void b43_beacon_irq(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	u32 cmd, beacon0_valid, beacon1_valid;

	if (!b43_beacon_mode(wl))
		return;

	/* This is the bottom half of the asynchronous beacon update. */

	/* Ignore interrupt in the future. */
	dev->irq_mask &= ~B43_IRQ_BEACON;

	cmd = b43_read32(dev, B43_MMIO_MACCMD);
	beacon0_valid = (cmd & B43_MACCMD_BEACON0_VALID);
	beacon1_valid = (cmd & B43_MACCMD_BEACON1_VALID);

	/* Schedule interrupt manually, if busy. */
	if (beacon0_valid && beacon1_valid) {
		b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, B43_IRQ_BEACON);
		dev->irq_mask |= B43_IRQ_BEACON;
		return;
	}

	if (unlikely(wl->beacon_templates_virgin)) {
		/* We never uploaded a beacon before.
		 * Upload both templates now, but only mark one valid. */
		wl->beacon_templates_virgin = false;
		b43_upload_beacon0(dev);
		b43_upload_beacon1(dev);
		cmd = b43_read32(dev, B43_MMIO_MACCMD);
		cmd |= B43_MACCMD_BEACON0_VALID;
		b43_write32(dev, B43_MMIO_MACCMD, cmd);
	} else {
		if (!beacon0_valid) {
			b43_upload_beacon0(dev);
			cmd = b43_read32(dev, B43_MMIO_MACCMD);
			cmd |= B43_MACCMD_BEACON0_VALID;
			b43_write32(dev, B43_MMIO_MACCMD, cmd);
		} else if (!beacon1_valid) {
			b43_upload_beacon1(dev);
			cmd = b43_read32(dev, B43_MMIO_MACCMD);
			cmd |= B43_MACCMD_BEACON1_VALID;
			b43_write32(dev, B43_MMIO_MACCMD, cmd);
		}
	}
}

static void b43_do_beacon_update_trigger_work(struct b43_wldev *dev)
{
	u32 old_irq_mask = dev->irq_mask;

	/* update beacon right away or defer to irq */
	b43_beacon_irq(dev);
	if (old_irq_mask != dev->irq_mask) {
		/* The handler updated the IRQ mask. */
		B43_WARN_ON(!dev->irq_mask);
		if (b43_read32(dev, B43_MMIO_GEN_IRQ_MASK)) {
			b43_write32(dev, B43_MMIO_GEN_IRQ_MASK, dev->irq_mask);
		} else {
			/* Device interrupts are currently disabled. That means
			 * we just ran the hardirq handler and scheduled the
			 * IRQ thread. The thread will write the IRQ mask when
			 * it finished, so there's nothing to do here. Writing
			 * the mask _here_ would incorrectly re-enable IRQs. */
		}
	}
}

static void b43_beacon_update_trigger_work(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work, struct b43_wl,
					 beacon_update_trigger);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (likely(dev && (b43_status(dev) >= B43_STAT_INITIALIZED))) {
		if (b43_bus_host_is_sdio(dev->dev)) {
			/* wl->mutex is enough. */
			b43_do_beacon_update_trigger_work(dev);
		} else {
			spin_lock_irq(&wl->hardirq_lock);
			b43_do_beacon_update_trigger_work(dev);
			spin_unlock_irq(&wl->hardirq_lock);
		}
	}
	mutex_unlock(&wl->mutex);
}

/* Asynchronously update the packet templates in template RAM. */
void b43_beacon_update(struct b43_wl *wl)
{
	struct sk_buff *beacon, *old_beacon;
	unsigned long flags;

	if (!b43_beacon_mode(wl))
		return;

	beacon = ieee80211_beacon_get(wl->hw, wl->vif, 0);
	if (unlikely(!beacon))
		return;

	spin_lock_irqsave(&wl->beacon_lock, flags);
	old_beacon = wl->current_beacon;
	wl->current_beacon = beacon;
	wl->beacon0_uploaded = false;
	wl->beacon1_uploaded = false;
	spin_unlock_irqrestore(&wl->beacon_lock, flags);

	ieee80211_queue_work(wl->hw, &wl->beacon_update_trigger);

	if (old_beacon)
		dev_kfree_skb_any(old_beacon);
}

void b43_beacon_set_interval(struct b43_wldev *dev, u16 beacon_int)
{
	if (!b43_beacon_mode(dev->wl) || !beacon_int)
		return;

	b43_time_lock(dev);
	if (dev->dev->core_rev >= 3) {
		b43_write32(dev, B43_MMIO_TSF_CFP_REP, (beacon_int << 16));
		b43_write32(dev, B43_MMIO_TSF_CFP_START, (beacon_int << 10));
	} else {
		b43_write16(dev, 0x606, (beacon_int >> 6));
		b43_write16(dev, 0x610, beacon_int);
	}
	b43_time_unlock(dev);
	b43dbg(dev->wl, "Set beacon interval to %u\n", beacon_int);
}

/* An AP's TBTT: the further APs' beacons. */
bool b43_beacon_tbtt(struct b43_wldev *dev)
{
	if (!b43_is_mode(dev->wl, NL80211_IFTYPE_AP))
		return false;
	b43_mbss_beacons(dev);
	return true;
}

void b43_beacon_init(struct b43_wl *wl)
{
	spin_lock_init(&wl->beacon_lock);
	INIT_WORK(&wl->beacon_update_trigger, b43_beacon_update_trigger_work);
}

/* Both templates need an upload before the first beacon. */
void b43_beacon_reset(struct b43_wl *wl)
{
	wl->beacon0_uploaded = false;
	wl->beacon1_uploaded = false;
	wl->beacon_templates_virgin = true;
}

void b43_beacon_stop(struct b43_wl *wl)
{
	cancel_work_sync(&wl->beacon_update_trigger);
}

void b43_beacon_release(struct b43_wl *wl)
{
	if (wl->current_beacon) {
		dev_kfree_skb_any(wl->current_beacon);
		wl->current_beacon = NULL;
	}
}
