/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_BEACON_H_
#define B43_BEACON_H_

#include <linux/types.h>

struct b43_wl;
struct b43_wldev;

/*
 * The beacons: the microcode's two beacon templates for the first interface
 * and, on an AP, the beacons of the further AP interfaces. Every call may be
 * made in any mode; the ones that only apply while beaconing (AP, mesh point,
 * ad-hoc) check that themselves.
 */

/* Probe and the mac80211 start, stop and core exit. */
void b43_beacon_init(struct b43_wl *wl);
void b43_beacon_reset(struct b43_wl *wl);
void b43_beacon_stop(struct b43_wl *wl);
void b43_beacon_release(struct b43_wl *wl);

/* A new beacon from mac80211: its content or its TIM changed. */
void b43_beacon_update(struct b43_wl *wl);
/* The beacon interval in TU; 0 leaves it as it is. */
void b43_beacon_set_interval(struct b43_wldev *dev, u16 beacon_int);

/* B43_IRQ_BEACON: a template is free for the next beacon. */
void b43_beacon_irq(struct b43_wldev *dev);
/* B43_IRQ_TBTT_INDI: true when the TBTT was an AP's and has been handled. */
bool b43_beacon_tbtt(struct b43_wldev *dev);

/* Whether the first interface sends beacons. */
bool b43_beaconing(struct b43_wl *wl);

#endif /* B43_BEACON_H_ */
