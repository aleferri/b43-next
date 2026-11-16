/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_AMPDU_H_
#define B43_AMPDU_H_

#include <linux/skbuff.h>
#include <linux/types.h>

struct b43_wl;
struct b43_wldev;
struct b43_txstatus;
struct ieee80211_hw;
struct ieee80211_vif;
struct ieee80211_sta;
struct ieee80211_txq;
struct ieee80211_ampdu_params;

/* TX A-MPDU state of one TID of a station, protected by wl->mutex. */
#define B43_AMPDU_TX_WINDOW	64
struct b43_ampdu_tx_tid {
	bool operational;
	/* Negotiated reorder window, at most B43_AMPDU_TX_WINDOW. */
	u8 buf_size;
	/* Oldest unresolved sequence and the unresolved set relative to it. */
	u16 win_start;
	u64 outstanding;
	/* Driver retransmissions, indexed by sequence modulo the window. */
	u8 retries[B43_AMPDU_TX_WINDOW];
	/* Retransmissions and frames taken from mac80211 that wait for the
	 * next A-MPDU, in sequence order. Empty unless operational. */
	struct sk_buff_head queue;
};

bool b43_ampdu_tx_host(struct b43_wldev *dev);
int b43_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     struct ieee80211_ampdu_params *params);
struct b43_ampdu_tx_tid *b43_ampdu_txq(struct ieee80211_txq *txq);
int b43_ampdu_tx(struct b43_wldev *dev, struct ieee80211_txq *txq,
		 struct b43_ampdu_tx_tid *t);
void b43_ampdu_txstatus(struct b43_wldev *dev,
			const struct b43_txstatus *status);
struct sk_buff *b43_ampdu_tx_dequeue(struct b43_ampdu_tx_tid *t);
void b43_ampdu_tx_sta_ps(struct b43_wl *wl, struct ieee80211_sta *sta,
			 bool asleep);
void b43_ampdu_tx_sta_drop(struct b43_wl *wl, struct ieee80211_sta *sta);
void b43_ampdu_tx_purge(struct b43_wl *wl);
bool b43_ampdu_tx_queued(struct b43_wl *wl);
void b43_ampdu_tx_restart(struct b43_wldev *dev);

#endif /* B43_AMPDU_H_ */
