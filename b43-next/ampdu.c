// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Block ack sessions, and the TX A-MPDUs b43 builds itself.
 *
 * On the AC the microcode builds the A-MPDU from MPDUs posted one by one
 * with the session's window in their header (b43_txhdr_ac_ampdu()), so a
 * session there is the window kept here. On the pre-AC cores that send
 * A-MPDUs b43 builds them, as their stock drivers do: an A-MPDU takes the
 * frames of one station and TID from mac80211's TX queue (b43_tx_pull()),
 * goes to the DMA ring at once (b43_dma_tx_ampdu()) and is completed from
 * one TX status with the block ack bitmap. The frames not acknowledged are
 * sent again from t->queue, in sequence order and inside the window.
 */
#include "b43.h"
#include "ampdu.h"
#include "dma.h"
#include "main.h"
#include "xmit.h"

/* Driver retransmissions of an unacknowledged A-MPDU member; after
 * B43_AMPDU_TX_RR_LIMIT of them the member uses its fallback rate. */
#define B43_AMPDU_TX_RETRY_LIMIT	6
#define B43_AMPDU_TX_RR_LIMIT		2

/*
 * The cores whose A-MPDUs b43 builds: the pre-AC ones with HT, as their
 * stock drivers do, the BCM43227's and wl 7.14 on the BCM4331 (mmio
 * txh-ch11).
 */
bool b43_ampdu_tx_host(struct b43_wldev *dev)
{
	return b43_ht_enabled(dev);
}

/* Null delimiters after an MPDU so that the next one starts at least
 * spacing bytes after it. At most 63: with more, the hardware reports a TX
 * PHY error. At 40 MHz a short MPDU can need more. */
static u8 b43_ampdu_tx_ndelim(unsigned int mpdu, unsigned int spacing)
{
	unsigned int start = 4 + ALIGN(mpdu, 4);

	return spacing > start ? min(DIV_ROUND_UP(spacing - start, 4), 63U) : 0;
}

/* Whether a frame of an aggregating TX queue may join an A-MPDU. */
static bool b43_ampdu_tx_member(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	return (info->flags & IEEE80211_TX_CTL_AMPDU) &&
	       !(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	       ieee80211_is_data_qos(hdr->frame_control) &&
	       !ieee80211_has_morefrags(hdr->frame_control);
}

/* Returns the receiver of a frame we sent, on whichever of our interfaces
 * sent it, or NULL once it is gone. Must be called under rcu_read_lock(). */
static struct ieee80211_sta *b43_ampdu_tx_sta(struct b43_wl *wl,
					      const struct ieee80211_hdr *hdr)
{
	return ieee80211_find_sta_by_ifaddr(wl->hw, hdr->addr1, hdr->addr2);
}

/* Returns the A-MPDU state of a TX queue whose frames go out in A-MPDUs,
 * or NULL. Must be called under rcu_read_lock() or with the bottom halves
 * off. */
struct b43_ampdu_tx_tid *b43_ampdu_txq(struct ieee80211_txq *txq)
{
	struct b43_ampdu_tx_tid *t;

	if (!txq->sta)
		return NULL;
	t = &((struct b43_sta *)txq->sta->drv_priv)->ampdu_tx[txq->tid];
	return t->operational ? t : NULL;
}

static void b43_ampdu_tx_resolve(struct b43_ampdu_tx_tid *t, u16 seq)
{
	u16 off = ieee80211_sn_sub(seq, t->win_start);
	unsigned int shift;

	t->retries[seq % B43_AMPDU_TX_WINDOW] = 0;
	if (off >= B43_AMPDU_TX_WINDOW)
		return;
	t->outstanding &= ~BIT_ULL(off);
	if (!t->outstanding)
		return;
	shift = __ffs64(t->outstanding);
	t->outstanding >>= shift;
	t->win_start = ieee80211_sn_add(t->win_start, shift);
}

static u16 b43_ampdu_tx_seq(struct sk_buff *skb)
{
	return ieee80211_get_sn((struct ieee80211_hdr *)skb->data);
}

static bool b43_ampdu_tx_key_valid(struct b43_wldev *dev,
				   struct ieee80211_tx_info *info)
{
	int i;

	if (!info->control.hw_key)
		return true;
	for (i = 0; i < ARRAY_SIZE(dev->key); i++) {
		if (dev->key[i].keyconf == info->control.hw_key)
			return true;
	}
	return false;
}

/* Requeue a retransmission ahead of the later sequences, so A-MPDUs stay
 * in sequence order when completions of overlapping A-MPDUs return
 * members. */
static void b43_ampdu_tx_requeue(struct b43_ampdu_tx_tid *t,
				 struct sk_buff *skb)
{
	u16 seq = b43_ampdu_tx_seq(skb);
	struct sk_buff *pos;

	skb_queue_walk(&t->queue, pos) {
		if (ieee80211_sn_less_eq(seq, b43_ampdu_tx_seq(pos)))
			break;
	}
	/* At the end of the walk pos is the list head: append. */
	__skb_queue_before(&t->queue, pos, skb);
}

/* Takes a frame out of t->queue for a PS-Poll or U-APSD response. It goes
 * out singly, outside the Block Ack window. Called under wl->mutex. */
struct sk_buff *b43_ampdu_tx_dequeue(struct b43_ampdu_tx_tid *t)
{
	struct sk_buff *skb;

	if (!t->operational)
		return NULL;
	skb = __skb_dequeue(&t->queue);
	if (skb)
		b43_ampdu_tx_resolve(t, b43_ampdu_tx_seq(skb));
	return skb;
}

/* The session ends: the frames in t->queue go out singly, or are dropped
 * with drop. Called under wl->mutex. */
static void b43_ampdu_tx_stop(struct b43_wl *wl, struct b43_ampdu_tx_tid *t,
			      bool drop)
{
	struct sk_buff *skb;

	if (!t->operational)
		return;
	t->operational = false;
	if (drop) {
		ieee80211_purge_tx_queue(wl->hw, &t->queue);
		return;
	}
	if (skb_queue_empty(&t->queue))
		return;
	while ((skb = __skb_dequeue(&t->queue)))
		skb_queue_tail(&wl->tx_queue[skb_get_queue_mapping(skb)], skb);
	ieee80211_queue_work(wl->hw, &wl->tx_work);
}

/* mac80211 keeps the new frames of a dozing station; the frames in its
 * t->queue set its TIM bit and wait until it wakes up or asks for them.
 */
void b43_ampdu_tx_sta_ps(struct b43_wl *wl, struct ieee80211_sta *sta,
			 bool asleep)
{
	struct b43_sta *bsta = (struct b43_sta *)sta->drv_priv;
	int tid;

	WRITE_ONCE(bsta->asleep, asleep);
	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++) {
		struct b43_ampdu_tx_tid *t = &bsta->ampdu_tx[tid];

		if (!t->operational)
			continue;
		if (!asleep)
			ieee80211_schedule_txq(wl->hw, sta->txq[tid]);
		else if (!skb_queue_empty(&t->queue))
			ieee80211_sta_set_buffered(sta, tid, true);
	}
	if (!asleep)
		ieee80211_queue_work(wl->hw, &wl->tx_work);
}

/* The station is going away: drop the frames that wait for it. */
void b43_ampdu_tx_sta_drop(struct b43_wl *wl, struct ieee80211_sta *sta)
{
	struct b43_sta *bsta = (struct b43_sta *)sta->drv_priv;
	int tid;

	mutex_lock(&wl->mutex);
	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++)
		b43_ampdu_tx_stop(wl, &bsta->ampdu_tx[tid], true);
	mutex_unlock(&wl->mutex);
}

static void b43_ampdu_tx_sta_purge(void *data, struct ieee80211_sta *sta)
{
	struct b43_sta *bsta = (struct b43_sta *)sta->drv_priv;
	struct b43_wl *wl = data;
	int tid;

	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++) {
		if (bsta->ampdu_tx[tid].operational)
			ieee80211_purge_tx_queue(wl->hw,
						 &bsta->ampdu_tx[tid].queue);
	}
}

/* Drop the frames that wait in t->queue of every station. */
void b43_ampdu_tx_purge(struct b43_wl *wl)
{
	ieee80211_iterate_stations_atomic(wl->hw, b43_ampdu_tx_sta_purge, wl);
}

static void b43_ampdu_tx_sta_queued(void *data, struct ieee80211_sta *sta)
{
	struct b43_sta *bsta = (struct b43_sta *)sta->drv_priv;
	bool *queued = data;
	int tid;

	if (READ_ONCE(bsta->asleep))
		return;
	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++) {
		if (bsta->ampdu_tx[tid].operational &&
		    !skb_queue_empty(&bsta->ampdu_tx[tid].queue))
			*queued = true;
	}
}

/* Whether frames wait in t->queue of a station that is awake. */
bool b43_ampdu_tx_queued(struct b43_wl *wl)
{
	bool queued = false;

	ieee80211_iterate_stations_atomic(wl->hw, b43_ampdu_tx_sta_queued,
					  &queued);
	return queued;
}

static void b43_ampdu_tx_restart_sta(void *data, struct ieee80211_sta *sta)
{
	struct b43_wldev *dev = data;
	struct b43_sta *bsta = (struct b43_sta *)sta->drv_priv;
	int i;

	for (i = 0; i < IEEE80211_NUM_TIDS; i++) {
		struct b43_ampdu_tx_tid *t = &bsta->ampdu_tx[i];

		if (dev->__using_pio_transfers)
			t->operational = false;
		t->outstanding = 0;
		memset(t->retries, 0, sizeof(t->retries));
	}
}

/* The core starts again: nothing is in flight. */
void b43_ampdu_tx_restart(struct b43_wldev *dev)
{
	ieee80211_iterate_stations_atomic(dev->wl->hw,
					  b43_ampdu_tx_restart_sta, dev);
}

/* The next frame for an A-MPDU: the first one in t->queue, or else a new
 * one from mac80211. */
static struct sk_buff *b43_ampdu_tx_peek(struct b43_wl *wl,
					 struct ieee80211_txq *txq,
					 struct b43_ampdu_tx_tid *t)
{
	struct sk_buff *skb = skb_peek(&t->queue);

	if (!skb) {
		skb = ieee80211_tx_dequeue(wl->hw, txq);
		if (skb)
			__skb_queue_tail(&t->queue, skb);
	}
	return skb;
}

/* Set the rates of the A-MPDU that skb starts: a probe keeps its own,
 * other frames take the station's current ones, and its fallback rate
 * once B43_AMPDU_TX_RR_LIMIT retransmissions of skb failed. */
static void b43_ampdu_tx_rates(struct b43_wl *wl, struct ieee80211_txq *txq,
			       struct b43_ampdu_tx_tid *t, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = info->control.rates;
	int i;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		return;
	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		rates[i].idx = -1;
		rates[i].count = 0;
	}
	ieee80211_get_tx_rates(txq->vif, txq->sta, skb, rates,
			       wl->hw->max_rates);
	if (t->retries[b43_ampdu_tx_seq(skb) % B43_AMPDU_TX_WINDOW] >
	    B43_AMPDU_TX_RR_LIMIT && rates[1].idx >= 0 &&
	    (rates[1].flags & IEEE80211_TX_RC_MCS))
		rates[0] = rates[1];
}

/*
 * Send one A-MPDU from an aggregating TX queue, or the first frame alone
 * if it cannot start one, when b43_dma_tx_room() allows it. Returns the
 * number of frames sent. Called from b43_tx_pull(), with the bottom halves
 * off.
 */
int b43_ampdu_tx(struct b43_wldev *dev, struct ieee80211_txq *txq,
		 struct b43_ampdu_tx_tid *t)
{
	struct b43_wl *wl = dev->wl;
	struct b43_sta *bsta = (struct b43_sta *)txq->sta->drv_priv;
	struct sk_buff *skbs[B43_AMPDU_TX_MAX_MPDUS];
	struct b43_txhdr_ampdu ampdu[B43_AMPDU_TX_MAX_MPDUS];
	struct ieee80211_tx_info *info, *first;
	unsigned int max_len = bsta->ampdu_tx_max_len;
	unsigned int n = 0, len = 0, pad = 0, max_n, spacing, i, failed;
	u16 start = t->win_start;
	bool have_start = t->outstanding;
	struct sk_buff *skb;

	skb = b43_ampdu_tx_peek(wl, txq, t);
	if (!skb)
		return 0;
	b43_tx_update_hwps(dev, skb);
	b43_ampdu_tx_rates(wl, txq, t, skb);
	first = IEEE80211_SKB_CB(skb);
	if (!b43_ampdu_tx_member(skb) ||
	    !(first->control.rates[0].flags & IEEE80211_TX_RC_MCS)) {
		/* It goes out singly, outside the Block Ack window. */
		__skb_unlink(skb, &t->queue);
		b43_ampdu_tx_resolve(t, b43_ampdu_tx_seq(skb));
		if (b43_dma_tx(dev, skb))
			ieee80211_free_txskb(wl->hw, skb);
		return 1;
	}
	max_n = min_t(unsigned int, b43_dma_tx_ampdu_room(dev, txq->ac),
		      B43_AMPDU_TX_MAX_MPDUS);
	/* A rate-control probe goes alone. */
	if (first->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		max_n = 1;
	spacing = b43_ampdu_tx_spacing(dev, first, bsta->ampdu_tx_density);

	while (n < max_n && (skb = b43_ampdu_tx_peek(wl, txq, t))) {
		unsigned int mpdu, next_len;
		u16 seq = b43_ampdu_tx_seq(skb);

		info = IEEE80211_SKB_CB(skb);
		if (n && (!b43_ampdu_tx_member(skb) ||
			  (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)))
			break;
		if (!have_start) {
			start = seq;
			have_start = true;
		}
		if (ieee80211_sn_sub(seq, start) >= t->buf_size) {
			if (n || b43_dma_tx_ampdu_pending(dev, txq->ac))
				break;
			/* Nothing is in flight: the window state is stale. */
			t->outstanding = 0;
			start = seq;
		}
		mpdu = b43_tx_psdu_len(skb);
		/* Each MPDU follows a 4-byte delimiter and pads to 4 bytes;
		 * null delimiters after a short one keep the peer's spacing. */
		next_len = n ? len + pad + 4 + mpdu : 4 + mpdu;
		if (n && (next_len > max_len ||
			  !b43_ampdu_tx_fits(dev, first, next_len)))
			break;
		ampdu[n].ndelim = b43_ampdu_tx_ndelim(mpdu, spacing);
		pad = ALIGN(mpdu, 4) - mpdu + 4 * ampdu[n].ndelim;
		len = next_len;
		skbs[n++] = __skb_dequeue(&t->queue);
	}
	if (!n)
		return 0;

	/* Every member goes at the first member's rates. */
	failed = n;
	for (i = 0; i < n; i++) {
		info = IEEE80211_SKB_CB(skbs[i]);
		if (i)
			memcpy(info->control.rates, first->control.rates,
			       sizeof(info->control.rates));
		if (failed == n && !b43_ampdu_tx_key_valid(dev, info))
			failed = i;
		ampdu[i].first = !i;
		ampdu[i].last = i == n - 1;
		ampdu[i].len = len;
	}
	ampdu[n - 1].ndelim = 0;
	if (failed == n)
		failed = b43_dma_tx_ampdu(dev, txq->ac, skbs, ampdu, n);
	if (unlikely(failed < n)) {
		/* The hardware got none of them; drop the failed frame and
		 * retry the rest. */
		for (i = n; i-- > 0;) {
			if (i != failed)
				__skb_queue_head(&t->queue, skbs[i]);
		}
		b43_ampdu_tx_resolve(t, b43_ampdu_tx_seq(skbs[failed]));
		ieee80211_free_txskb(wl->hw, skbs[failed]);
		return 1;
	}

	if (!t->outstanding)
		t->win_start = start;
	for (i = 0; i < n; i++) {
		u16 seq = b43_ampdu_tx_seq(skbs[i]);

		t->outstanding |= BIT_ULL(ieee80211_sn_sub(seq, t->win_start));
	}
	return n;
}

/*
 * Complete one A-MPDU from its single status: the firmware reports the
 * first member's cookie and, after a received Block Ack, its bitmap. The
 * completed members are reported like other frames, from wl->txstatus_queue
 * (b43_rx_deliver()); the others are sent again until
 * B43_AMPDU_TX_RETRY_LIMIT.
 */
void b43_ampdu_txstatus(struct b43_wldev *dev,
			const struct b43_txstatus *status)
{
	struct b43_wl *wl = dev->wl;
	struct ieee80211_sta *sta = NULL;
	struct b43_sta *bsta = NULL;
	struct b43_ampdu_tx_tid *t = NULL;
	struct sk_buff_head mpdus, done, retry;
	struct sk_buff *skb, *bar = NULL;
	struct ieee80211_tx_info *info, rc = {};
	struct ieee80211_tx_rate rate = { .idx = -1 };
	unsigned int n = 0, acked = 0;
	u8 tid = 0;

	__skb_queue_head_init(&mpdus);
	if (!b43_dma_ampdu_complete(dev, status, &mpdus))
		return;

	__skb_queue_head_init(&done);
	__skb_queue_head_init(&retry);
	rcu_read_lock();
	while ((skb = __skb_dequeue(&mpdus))) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		u16 seq = b43_ampdu_tx_seq(skb);

		info = IEEE80211_SKB_CB(skb);
		if (!n) {
			/* Without the receiver, complete without retries. */
			sta = b43_ampdu_tx_sta(wl, hdr);
			bsta = sta ? (struct b43_sta *)sta->drv_priv : NULL;
			tid = ieee80211_get_tid(hdr);
			t = bsta ? &bsta->ampdu_tx[tid] : NULL;
			rate = info->control.rates[0];
			rc.band = info->band;
		}
		/* b43_dma_ampdu_complete() gave it the first interface; a
		 * retry goes out on the station's own. */
		if (bsta && bsta->vif)
			info->control.vif = bsta->vif;
		n++;
		if (b43_txstatus_mpdu_acked(status, skb)) {
			acked++;
			if (t)
				b43_ampdu_tx_resolve(t, seq);
			ieee80211_tx_info_clear_status(info);
			info->flags |= IEEE80211_TX_STAT_ACK;
			__skb_queue_tail(&done, skb);
		} else if (t && t->operational &&
			   t->retries[seq % B43_AMPDU_TX_WINDOW] <
			   B43_AMPDU_TX_RETRY_LIMIT) {
			/* A probe tries its sample rate once, then goes
			 * on as an ordinary frame. */
			info->flags &= ~IEEE80211_TX_CTL_RATE_CTRL_PROBE;
			t->retries[seq % B43_AMPDU_TX_WINDOW]++;
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_RETRY);
			__skb_queue_tail(&retry, skb);
		} else {
			if (t)
				b43_ampdu_tx_resolve(t, seq);
			ieee80211_tx_info_clear_status(info);
			__skb_queue_tail(&done, skb);
			/* mac80211 sends a BAR past the last dropped member. */
			if (t && t->operational)
				bar = skb;
		}
	}

	if (t) {
		/* One rate-control sample per transmitted A-MPDU at its rate,
		 * carried by a completed member if there is one; minstrel
		 * ignores the other members. */
		skb = skb_peek(&done);
		info = skb ? IEEE80211_SKB_CB(skb) : &rc;
		info->flags |= IEEE80211_TX_STAT_AMPDU;
		info->status.ampdu_len = n;
		info->status.ampdu_ack_len = acked;
		info->status.rates[0].idx = rate.idx;
		info->status.rates[0].flags = rate.flags;
		info->status.rates[0].count = 1;
		info->status.rates[1].idx = -1;
		if (!skb)
			ieee80211_tx_rate_update(wl->hw, sta, &rc);
	}
	if (!skb_queue_empty(&retry)) {
		while ((skb = __skb_dequeue(&retry)))
			b43_ampdu_tx_requeue(t, skb);
		/* A dozing station gets them when it wakes up or asks. */
		if (READ_ONCE(bsta->asleep))
			ieee80211_sta_set_buffered(sta, tid, true);
		else
			ieee80211_schedule_txq(wl->hw, sta->txq[tid]);
	}
	rcu_read_unlock();
	if (bar)
		IEEE80211_SKB_CB(bar)->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
	/* Reported by b43_rx_deliver(), outside wl->mutex. */
	while ((skb = __skb_dequeue(&done)))
		skb_queue_tail(&wl->txstatus_queue, skb);
}

/*
 * mac80211's block ack sessions. On RX the microcode answers an A-MPDU or
 * a block ack request with a block ack of its own making, from no table a
 * driver fills (the AC: frame code 0x25, at 0x076C on 784), the BCM43227's
 * and the BCM4331's from the transmitter address in their template RAM
 * (b43_write_mac_bssid_templates()); mac80211 reorders. A further AP of
 * the BCM4331 (b43_mbss_match()) answers with its own address, so every
 * AP takes sessions: a client that is refused one may go on asking and
 * stop being acknowledged (an MT7981 does). On TX the cores of
 * b43_ampdu_tx_host() send A-MPDUs on BE (TIDs 0 and 3) and VI (TIDs 4
 * and 5), whose TX FIFOs b43_txfifo_init_ht() sizes.
 */
int b43_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_sta *bsta = (struct b43_sta *)params->sta->drv_priv;
	struct b43_ampdu_tx_tid *t = &bsta->ampdu_tx[params->tid];
	struct ieee80211_sta_ht_cap *ht_cap = &params->sta->deflink.ht_cap;
	struct b43_wldev *dev;
	bool ok, rx;

	switch (params->action) {
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_RX_START:
		mutex_lock(&wl->mutex);
		dev = wl->current_dev;
		rx = params->action == IEEE80211_AMPDU_RX_START;
		if (dev && (rx ? b43_ht_enabled(dev) :
				 b43_ampdu_tx_host(dev)))
			ok = b43_status(dev) >= B43_STAT_STARTED &&
			     (vif->type == NL80211_IFTYPE_STATION ||
			      vif->type == NL80211_IFTYPE_AP ||
			      vif->type == NL80211_IFTYPE_ADHOC) &&
			     (rx || (!b43_using_pio_transfers(dev) &&
				     (params->tid == 0 || params->tid == 3 ||
				      params->tid == 4 || params->tid == 5)));
		else
			ok = dev && dev->fw.layout.ampdu &&
			     !b43_using_pio_transfers(dev);
		if (ok && !rx && b43_ampdu_tx_host(dev))
			memset(t, 0, sizeof(*t));
		mutex_unlock(&wl->mutex);
		if (!ok)
			return -EOPNOTSUPP;
		if (rx)
			return 0;
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		WRITE_ONCE(bsta->ampdu_win[params->tid], params->buf_size);
		mutex_lock(&wl->mutex);
		dev = wl->current_dev;
		if (dev && b43_ampdu_tx_host(dev)) {
			memset(t, 0, sizeof(*t));
			skb_queue_head_init(&t->queue);
			t->buf_size = min_t(u16, params->buf_size,
					    B43_AMPDU_TX_WINDOW);
			t->operational = t->buf_size;
			bsta->ampdu_tx_max_len =
				(1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				       ht_cap->ampdu_factor)) - 1;
			bsta->ampdu_tx_density = ht_cap->ampdu_density;
		}
		mutex_unlock(&wl->mutex);
		return 0;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		WRITE_ONCE(bsta->ampdu_win[params->tid], 0);
		/* Queued and retried members continue as ordinary MPDUs. */
		mutex_lock(&wl->mutex);
		b43_ampdu_tx_stop(wl, t, false);
		mutex_unlock(&wl->mutex);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, params->sta->addr,
						params->tid);
		return 0;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		WRITE_ONCE(bsta->ampdu_win[params->tid], 0);
		mutex_lock(&wl->mutex);
		b43_ampdu_tx_stop(wl, t, params->action ==
				  IEEE80211_AMPDU_TX_STOP_FLUSH);
		mutex_unlock(&wl->mutex);
		return 0;
	case IEEE80211_AMPDU_RX_STOP:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
