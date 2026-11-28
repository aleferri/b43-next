/* SPDX-License-Identifier: GPL-2.0-or-later */
/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <stefano.brivio@polimi.it>
                     Michael Buesch <m@bues.ch>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.


*/

#ifndef B43_MAIN_H_
#define B43_MAIN_H_

#include "b43.h"

#define P4D_BYT3S(magic, nr_bytes)	u8 __p4dding##magic[nr_bytes]
#define P4D_BYTES(line, nr_bytes)	P4D_BYT3S(line, nr_bytes)
/* Magic helper macro to pad structures. Ignore those above. It's magic. */
#define PAD_BYTES(nr_bytes)		P4D_BYTES( __LINE__ , (nr_bytes))


extern int b43_modparam_verbose;

/* Logmessage verbosity levels. Update the b43_modparam_verbose helptext, if
 * you add or remove levels. */
enum b43_verbosity {
	B43_VERBOSITY_ERROR,
	B43_VERBOSITY_WARN,
	B43_VERBOSITY_INFO,
	B43_VERBOSITY_DEBUG,
	__B43_VERBOSITY_AFTERLAST, /* keep last */

	B43_VERBOSITY_MAX = __B43_VERBOSITY_AFTERLAST - 1,
#if B43_DEBUG
	B43_VERBOSITY_DEFAULT = B43_VERBOSITY_DEBUG,
#else
	B43_VERBOSITY_DEFAULT = B43_VERBOSITY_INFO,
#endif
};

static inline int b43_is_cck_rate(int rate)
{
	return (rate == B43_CCK_RATE_1MB ||
		rate == B43_CCK_RATE_2MB ||
		rate == B43_CCK_RATE_5MB || rate == B43_CCK_RATE_11MB);
}

static inline int b43_is_ofdm_rate(int rate)
{
	return !b43_is_cck_rate(rate);
}

u8 b43_ieee80211_antenna_sanitize(struct b43_wldev *dev,
				  u8 antenna_nr);
u16 b43_antenna_to_phyctl(struct b43_wldev *dev, int antenna);

/*
 * Template RAM advances its address on every data write, and the AC cores'
 * stock driver writes each template that way: RAM_CONTROL once, then the
 * words. A sequence starts with next = U16_MAX and rewrites the control
 * only where the offsets stop being consecutive.
 */
struct b43_ram_seq {
	u16 next;
	bool swap;
};

void b43_tpl_write(struct b43_wldev *dev, struct b43_ram_seq *seq,
		   u16 offset, u32 val);

void b43_time_lock(struct b43_wldev *dev);
void b43_time_unlock(struct b43_wldev *dev);

void b43_tsf_read(struct b43_wldev *dev, u64 * tsf);
void b43_watchdog_poll(struct b43_wldev *dev);
void b43_tsf_write(struct b43_wldev *dev, u64 tsf);

static inline void b43_shm_control_word(struct b43_wldev *dev,
					u16 routing, u16 offset)
{
	u32 control;

	/* "offset" is the WORD offset. */
	control = routing;
	control <<= 16;
	control |= offset;
	b43_write32(dev, B43_MMIO_SHM_CONTROL, control);
}

u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset);
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset);
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value);
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value);
u16 b43_rate_block(struct b43_wldev *dev, u16 hw_value);
void b43_rate_dirmap_read(struct b43_wldev *dev);

u64 b43_hf_read(struct b43_wldev *dev);
void b43_mhf_maskset(struct b43_wldev *dev, unsigned int slot, u16 mask,
		     u16 set);
void b43_mhf_write(struct b43_wldev *dev, unsigned int slot);
void b43_hf_write(struct b43_wldev *dev, u64 value);

void b43_dummy_transmission(struct b43_wldev *dev, bool ofdm, bool pa_on);

void b43_wireless_core_reset(struct b43_wldev *dev, bool gmode);

void b43_controller_restart(struct b43_wldev *dev, const char *reason);

void b43_macstat_update(struct b43_wldev *dev, bool count);
void b43_generate_noise_sample(struct b43_wldev *dev);
void b43_noise_pwr_report(struct b43_wldev *dev, const u32 *pwr,
			  unsigned int cores);

#define B43_PS_ENABLED	(1 << 0)	/* Force enable hardware power saving */
#define B43_PS_DISABLED	(1 << 1)	/* Force disable hardware power saving */
#define B43_PS_AWAKE	(1 << 2)	/* Force device awake */
#define B43_PS_ASLEEP	(1 << 3)	/* Force device asleep */
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags);
void b43_ps_rx(struct b43_wldev *dev, struct sk_buff *skb);
void b43_ps_tx_status(struct b43_wldev *dev, struct sk_buff *skb, bool acked);
void b43_tx_update_hwps(struct b43_wldev *dev, struct sk_buff *skb);

void b43_wireless_core_phy_pll_reset(struct b43_wldev *dev);

void b43_mac_suspend(struct b43_wldev *dev);
void b43_mac_enable(struct b43_wldev *dev);
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on);
void b43_mac_switch_freq(struct b43_wldev *dev, u8 spurmode);

void b43_ampdu_kick_arm(struct b43_wldev *dev);

void b43_board_chains(struct b43_wldev *dev, u8 *rxchain, u8 *txchain);

#endif /* B43_MAIN_H_ */
