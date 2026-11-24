/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_PS_H_
#define B43_PS_H_

#include <linux/types.h>

struct b43_wl;
struct b43_wldev;
struct sk_buff;

/*
 * Station power save, which b43 runs on the HT N-PHYs, and the HWPS and
 * AWAKE bits of the core. Every call may be made in any mode and on any PHY.
 * wl->mutex protects the state.
 */

/* Probe and core init. */
void b43_ps_init(struct b43_wl *wl);
void b43_ps_reset(struct b43_wldev *dev);
/* Waits for the work that enters power save, which takes wl->mutex. */
void b43_ps_cancel(struct b43_wl *wl);

/* mac80211 allows power save or not; the last interface is removed. */
void b43_ps_update(struct b43_wldev *dev, bool enabled);
void b43_ps_disable(struct b43_wl *wl);

/* The station is awake while mac80211 scans. */
void b43_ps_scan_start(struct b43_wl *wl);
void b43_ps_scan_complete(struct b43_wl *wl);

/* A frame about to go to the hardware, and its TX status. */
void b43_tx_update_hwps(struct b43_wldev *dev, struct sk_buff *skb);
void b43_ps_tx_status(struct b43_wldev *dev, struct sk_buff *skb, bool acked);
/* A frame received without an FCS error. */
void b43_ps_rx(struct b43_wldev *dev, struct sk_buff *skb);

#define B43_PS_ENABLED	(1 << 0)	/* Force enable hardware power saving */
#define B43_PS_DISABLED	(1 << 1)	/* Force disable hardware power saving */
#define B43_PS_AWAKE	(1 << 2)	/* Force device awake */
#define B43_PS_ASLEEP	(1 << 3)	/* Force device asleep */
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags);

#endif /* B43_PS_H_ */
