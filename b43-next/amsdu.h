/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_AMSDU_H_
#define B43_AMSDU_H_

#include <linux/types.h>

struct b43_wldev;
struct ieee80211_hw;
struct sk_buff;

/* Every received frame, before it is processed: returns the frame to process
 * and its receive header, or NULL when there is none yet. */
struct sk_buff *b43_rx_amsdu(struct b43_wldev *dev, struct sk_buff *skb,
			     const void **rxhdr);

/* Core exit: frees an A-MSDU that is incomplete. */
void b43_rx_amsdu_free(struct b43_wldev *dev);

/* mac80211's can_aggregate_in_amsdu. */
bool b43_op_can_aggregate_in_amsdu(struct ieee80211_hw *hw,
				   struct sk_buff *head,
				   struct sk_buff *skb);

#endif /* B43_AMSDU_H_ */
