// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  Station power save, and the core's HWPS and AWAKE bits

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

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/math64.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>

#include "b43.h"
#include "main.h"
#include "ps.h"

void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	u32 macctl, old;
	u16 ucstat;
	bool hwps;
	bool awake;
	int i;

	B43_WARN_ON((ps_flags & B43_PS_ENABLED) &&
		    (ps_flags & B43_PS_DISABLED));
	B43_WARN_ON((ps_flags & B43_PS_AWAKE) && (ps_flags & B43_PS_ASLEEP));

	if (ps_flags & B43_PS_ENABLED) {
		hwps = true;
	} else if (ps_flags & B43_PS_DISABLED) {
		hwps = false;
	} else {
		hwps = dev->hwps;
	}
	if (ps_flags & B43_PS_AWAKE) {
		awake = true;
	} else if (ps_flags & B43_PS_ASLEEP) {
		awake = false;
	} else {
		/* The microcode sleeps between beacons unless held awake:
		 * until a beacon has checked the TBTT timer after power save
		 * started, and from a PS-Poll until its response.
		 */
		awake = !(dev->ps && hwps) || !dev->ps_tbtt || dev->ps_poll ||
			dev->mac_suspended;
	}

	/*
	 * As the HT-PHY's stock driver: on an AP that does not beacon yet
	 * it lets the ucode sleep whenever it enables the MAC (MACCONTROL
	 * 0x00160403, AWAKE clear, six times between `wl ssid` and `wl bss up`
	 * on the R6400 v1), and wakes it for the suspend after.
	 */
	if (dev->phy.type == B43_PHYTYPE_HT && !(ps_flags & B43_PS_AWAKE) &&
	    b43_is_mode(dev->wl, NL80211_IFTYPE_AP) &&
	    !(dev->wl->vif && dev->wl->vif->bss_conf.enable_beacon))
		awake = false;

	old = b43_read32(dev, B43_MMIO_MACCTL);
	macctl = old;
	if (hwps)
		macctl |= B43_MACCTL_HWPS;
	else
		macctl &= ~B43_MACCTL_HWPS;
	if (awake)
		macctl |= B43_MACCTL_AWAKE;
	else
		macctl &= ~B43_MACCTL_AWAKE;
	/* Already so: the microcode is in the state asked, nothing to wait for */
	if (macctl == old)
		return;
	b43_macctl_write(dev, macctl);
	/* Commit write */
	b43_read32(dev, B43_MMIO_MACCTL);
	if (awake && dev->dev->core_rev >= 5) {
		/* Wait for the microcode to wake up. */
		for (i = 0; i < 100; i++) {
			ucstat = b43_shm_read16(dev, B43_SHM_SHARED,
						B43_SHM_SH_UCODESTAT);
			if (ucstat != B43_SHM_SH_UCODESTAT_SLEEP)
				break;
			udelay(10);
		}
	}
}

/* The TBTT after the current TSF: where the 64-bit TSF is a multiple of the
 * beacon interval.
 */
static u32 b43_sta_next_tbtt(struct b43_wldev *dev, u16 beacon_int)
{
	u32 interval = ieee80211_tu_to_usec(beacon_int);
	u32 rem;
	u64 tsf;

	b43_tsf_read(dev, &tsf);
	div_u64_rem(tsf, interval, &rem);
	return tsf + interval - rem;
}

/* In power save the microcode wakes up for beacons at the TBTT timer. Its
 * start is compared with the low 32 bits of the TSF and advances by the
 * interval, so start it at the next TBTT.
 */
static void b43_set_sta_tbtt(struct b43_wldev *dev, u16 beacon_int)
{
	b43_time_lock(dev);
	b43_write32(dev, B43_MMIO_TSF_CFP_REP, beacon_int << 16);
	b43_write32(dev, B43_MMIO_TSF_CFP_START,
		    b43_sta_next_tbtt(dev, beacon_int));
	b43_time_unlock(dev);
}

/* The user's dynamic power save timeout in ms, mac80211's default of 100 ms
 * if unset, or 0 for static power save.
 */
static int b43_ps_timeout(struct b43_wl *wl)
{
	int timeout = ieee80211_vif_to_wdev(wl->vif)->ps_timeout;

	return timeout < 0 ? 100 : timeout;
}

/* The microcode sets the PM bit of each frame from HWPS when it transmits
 * the frame, so take HWPS from the PM bit of frames to the AP: a null frame
 * with PM announces power save, and in dynamic power save any other frame
 * wakes the station up and restarts the timeout. In static power save,
 * frames keep PM and the AP sends a buffered frame after a PS-Poll. Only
 * the N-PHY cores with HT (phy.nphy_ht) run power save.
 */
void b43_tx_update_hwps(struct b43_wldev *dev, struct sk_buff *skb)
{
	struct b43_wl *wl = dev->wl;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool hwps, ps_poll;

	/* TODO: opensource firmware */
	if (!dev->phy.nphy_ht || !b43_is_mode(wl, NL80211_IFTYPE_STATION) ||
	    info->flags & IEEE80211_TX_CTL_INJECTED ||
	    !ether_addr_equal(hdr->addr1, wl->bssid))
		return;
	if (ieee80211_has_pm(hdr->frame_control))
		hwps = true;
	else if (wl->ps_enabled && !b43_ps_timeout(wl))
		hwps = dev->hwps;
	else
		hwps = false;
	if (!hwps) {
		dev->ps = false;
		wl->ps_null_acked = false;
		wl->ps_last = jiffies;
		if (wl->ps_enabled)
			schedule_delayed_work(&wl->ps_work,
				msecs_to_jiffies(b43_ps_timeout(wl)));
	}
	ps_poll = dev->ps_poll || ieee80211_is_pspoll(hdr->frame_control);
	if (hwps == dev->hwps && ps_poll == dev->ps_poll)
		return;
	dev->hwps = hwps;
	dev->ps_poll = ps_poll;
	b43_power_saving_ctl_bits(dev, 0);
}

/* Send a null frame or a PS-Poll to the AP at the lowest rate. */
static void b43_ps_send(struct b43_wl *wl, struct sk_buff *skb, bool pm)
{
	if (!skb)
		return;
	if (pm)
		((struct ieee80211_hdr *)skb->data)->frame_control |=
			cpu_to_le16(IEEE80211_FCTL_PM);
	skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_USE_MINRATE;
	rcu_read_lock();
	if (ieee80211_tx_prepare_skb(wl->hw, wl->vif, skb,
				     b43_current_band(wl), NULL))
		b43_op_tx(wl->hw, NULL, skb);
	rcu_read_unlock();
}

static void b43_ps_send_null(struct b43_wl *wl, bool pm)
{
	b43_ps_send(wl, ieee80211_nullfunc_get(wl->hw, wl->vif, -1, true), pm);
}

/* The AP has acknowledged the null frame with PM=1: doze. */
static void b43_ps_doze(struct b43_wldev *dev)
{
	struct ieee80211_vif *vif = dev->wl->vif;

	dev->wl->ps_null_acked = false;
	dev->ps = true;
	dev->ps_tbtt = false;
	dev->ps_poll = false;
	if (vif->bss_conf.beacon_int)
		b43_set_sta_tbtt(dev, vif->bss_conf.beacon_int);
	b43_power_saving_ctl_bits(dev, 0);
}

/* mac80211 leaves power save to b43 (SUPPORTS_DYNAMIC_PS), which runs it
 * like mac80211's software power save: after the dynamic timeout without
 * frames to or from the AP, or at once in static power save, a null frame
 * with PM=1 announces power save, and the microcode dozes once the AP has
 * acknowledged it.
 */
static void b43_ps_work(struct work_struct *work)
{
	struct b43_wl *wl = container_of(work, struct b43_wl, ps_work.work);
	struct b43_wldev *dev;
	unsigned long idle;
	int timeout;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (!dev || b43_status(dev) < B43_STAT_STARTED || !wl->ps_enabled ||
	    wl->ps_scanning || dev->ps)
		goto out;
	if (wl->ps_null_acked && dev->hwps) {
		b43_ps_doze(dev);
		goto out;
	}
	timeout = b43_ps_timeout(wl);
	idle = wl->ps_last + msecs_to_jiffies(timeout);
	if (time_before(jiffies, idle)) {
		schedule_delayed_work(&wl->ps_work, idle - jiffies);
		goto out;
	}
	if (!b43_tx_empty(dev)) {
		schedule_delayed_work(&wl->ps_work,
				      msecs_to_jiffies(max(timeout, 10)));
		goto out;
	}
	b43_ps_send_null(wl, true);
	/* Its TX status reschedules this, unless the frame is dropped. */
	schedule_delayed_work(&wl->ps_work, msecs_to_jiffies(max(timeout, 10)));
out:
	mutex_unlock(&wl->mutex);
}

/* The TX status of a null frame with PM=1 to the AP, unless a frame with
 * PM=0 followed it.
 */
void b43_ps_tx_status(struct b43_wldev *dev, struct sk_buff *skb, bool acked)
{
	struct b43_wl *wl = dev->wl;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!wl->ps_enabled || wl->ps_scanning ||
	    IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_INJECTED ||
	    !ieee80211_is_any_nullfunc(hdr->frame_control) ||
	    !ieee80211_has_pm(hdr->frame_control) ||
	    !ether_addr_equal(hdr->addr1, wl->bssid))
		return;
	wl->ps_null_acked = acked && dev->hwps;
	/* Doze, or try again after the timeout. */
	mod_delayed_work(system_percpu_wq, &wl->ps_work, acked ? 0 :
			 msecs_to_jiffies(max(b43_ps_timeout(wl), 10)));
}

/* Handles the frames from the AP while power save is enabled. In power save
 * the TSF follows the AP's from the first beacon after association, which
 * may come after power save started. When the TSF jumps, the timer start
 * does not follow if it is more than 2^31 us behind the new TSF, and
 * otherwise moves to a multiple of the interval in the low 32 bits of the
 * TSF only. So check it once a beacon has updated the TSF. A beacon whose
 * TIM has the station's bit wakes the station up in dynamic power save and
 * sends a PS-Poll in static power save, as does a buffered frame with More
 * Data. A frame to this station answers a PS-Poll, and the next beacon ends
 * the wait for an answer. Unicast data restarts the dynamic timeout.
 */
void b43_ps_rx(struct b43_wldev *dev, struct sk_buff *skb)
{
	struct b43_wl *wl = dev->wl;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_vif *vif = wl->vif;
	__le16 fc = mgmt->frame_control;
	bool held = !dev->ps_tbtt || dev->ps_poll;
	bool poll = false;
	const struct element *tim;
	int len;

	if (!wl->ps_enabled || ieee80211_is_ctl(fc) ||
	    skb->len < sizeof(struct ieee80211_hdr_3addr) ||
	    !ether_addr_equal(mgmt->sa, wl->bssid))
		return;
	if (ieee80211_is_beacon(fc)) {
		len = skb->len - FCS_LEN -
		      offsetof(struct ieee80211_mgmt, u.beacon.variable);
		if (!dev->ps || len < 0)
			return;
		if (vif->bss_conf.beacon_int &&
		    b43_read32(dev, B43_MMIO_TSF_CFP_START) !=
		    b43_sta_next_tbtt(dev, vif->bss_conf.beacon_int))
			b43_set_sta_tbtt(dev, vif->bss_conf.beacon_int);
		dev->ps_tbtt = true;
		dev->ps_poll = false;
		tim = cfg80211_find_elem(WLAN_EID_TIM, mgmt->u.beacon.variable,
					 len);
		if (tim && ieee80211_check_tim((const void *)tim->data,
					       tim->datalen, vif->cfg.aid,
					       false)) {
			if (b43_ps_timeout(wl)) {
				dev->ps = false;
				b43_power_saving_ctl_bits(dev, 0);
				b43_ps_send_null(wl, false);
				return;
			}
			poll = true;
		}
	} else if (ether_addr_equal(mgmt->da, vif->addr)) {
		if (!ieee80211_has_morefrags(fc))
			dev->ps_poll = false;
		if (ieee80211_is_data(fc)) {
			wl->ps_last = jiffies;
			if (!dev->ps)
				schedule_delayed_work(&wl->ps_work,
					msecs_to_jiffies(b43_ps_timeout(wl)));
			poll = dev->ps && ieee80211_has_moredata(fc) &&
			       !b43_ps_timeout(wl);
		}
	}
	if (held != (!dev->ps_tbtt || dev->ps_poll))
		b43_power_saving_ctl_bits(dev, 0);
	if (poll)
		b43_ps_send(wl, ieee80211_pspoll_get(wl->hw, vif), true);
}

/* mac80211 allows power save while associated, with power save enabled. */
void b43_ps_update(struct b43_wldev *dev, bool enabled)
{
	struct b43_wl *wl = dev->wl;

	if (!dev->phy.nphy_ht || !b43_is_mode(wl, NL80211_IFTYPE_STATION))
		return;
	wl->ps_enabled = enabled;
	wl->ps_null_acked = false;
	if (enabled) {
		wl->ps_last = jiffies;
		schedule_delayed_work(&wl->ps_work, 0);
		return;
	}
	cancel_delayed_work(&wl->ps_work);
	dev->ps = false;
	/* The AP would keep buffering until the station next transmits. */
	if (dev->hwps && wl->vif->cfg.assoc)
		b43_ps_send_null(wl, false);
	else
		dev->hwps = false;
	b43_power_saving_ctl_bits(dev, 0);
}

void b43_ps_disable(struct b43_wl *wl)
{
	wl->ps_enabled = false;
}

/* mac80211 announces power save to the AP before leaving its
 * channel, and the station stays awake until the scan completes.
 */
void b43_ps_scan_start(struct b43_wl *wl)
{
	struct b43_wldev *dev = wl->current_dev;

	wl->ps_scanning = true;
	cancel_delayed_work(&wl->ps_work);
	if (dev && b43_status(dev) >= B43_STAT_STARTED && dev->ps) {
		dev->ps = false;
		b43_power_saving_ctl_bits(dev, 0);
	}
}

void b43_ps_scan_complete(struct b43_wl *wl)
{
	wl->ps_scanning = false;
	if (wl->ps_enabled) {
		wl->ps_last = jiffies;
		schedule_delayed_work(&wl->ps_work,
				      msecs_to_jiffies(b43_ps_timeout(wl)));
	}
}

void b43_ps_init(struct b43_wl *wl)
{
	INIT_DELAYED_WORK(&wl->ps_work, b43_ps_work);
}

void b43_ps_reset(struct b43_wldev *dev)
{
	dev->ps = false;
	dev->hwps = false;
	dev->ps_tbtt = false;
	dev->ps_poll = false;
}

void b43_ps_cancel(struct b43_wl *wl)
{
	cancel_delayed_work_sync(&wl->ps_work);
}
