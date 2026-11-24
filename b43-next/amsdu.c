// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  A-MSDUs: joining the received ones, which the cores take apart, and the
  frames mac80211 may aggregate

  Moved out of xmit.c and main.c; the code and its copyright are theirs:

  Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (C) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (C) 2005, 2006 Michael Buesch <m@bues.ch>
  Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (C) 2026 Alessio Ferri <alessio.ferri.3012@gmail.com>
  Copyright (C) 2026 Andreas Wendleder <andreas.wendleder@proton.me>
  Copyright (C) 2026 Ville Takio <ville+git@takio.fi>

*/

#include <linux/skbuff.h>
#include <linux/unaligned.h>

#include "b43.h"
#include "amsdu.h"
#include "xmit.h"

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
 * Returns that MPDU, or NULL while it is incomplete or dropped. Any other
 * frame is returned as it is.
 *
 * The BCM43227 does the same outside A-MPDUs, but each of its pieces has
 * padding of its own, and only the last one carries the status of the
 * whole frame. There the MPDU goes on under the last piece's receive
 * header, if it is as long as its PLCP header says.
 */
struct sk_buff *b43_rx_amsdu(struct b43_wldev *dev, struct sk_buff *skb,
			     const void **rxhdr)
{
	struct b43_rxhdr_fw4 *first = (struct b43_rxhdr_fw4 *)dev->rx_amsdu_hdr;
	const struct b43_fw_hdr *fh = dev->fw.layout.hdr;
	const u32 err = B43_RX_MAC_FCSERR | B43_RX_MAC_DECERR;
	struct sk_buff *head = dev->rx_amsdu;
	unsigned int type;
	u32 macstat;
	int need;

	BUILD_BUG_ON(sizeof(*first) > sizeof(dev->rx_amsdu_hdr));
	if (!fh->rx_amsdu ||
	    (fh->rx_amsdu_ht_only && !b43_nphy_ht_enabled(dev) &&
	     dev->phy.type != B43_PHYTYPE_HT))
		return skb;
	macstat = le32_to_cpu(((const struct b43_rxhdr_fw4 *)*rxhdr)->
			      format_598.mac_status);
	if (!(macstat & B43_RX_MAC_AMSDU))
		return skb;
	type = (macstat & B43_RX_MAC_AGGTYPE) >> B43_RX_MAC_AGGTYPE_SHIFT;
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

/*
 * Only full-size MSDUs go into an A-MSDU: with a OnePlus Nord 2 two rooms
 * away, aggregating everything took its uplink from 121 to 85 Mbit/s (the
 * TCP ACKs we send it wait for a partner), aggregating MSDUs of 1000
 * bytes and more kept it (125) and its downlink (172 against 173).
 */
bool b43_op_can_aggregate_in_amsdu(struct ieee80211_hw *hw,
				   struct sk_buff *head,
				   struct sk_buff *skb)
{
	return skb->len >= 1000;
}
