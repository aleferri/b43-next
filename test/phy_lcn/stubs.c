// SPDX-License-Identifier: GPL-2.0
/*
 * What the PHY files the LCN harnesses compile need from the rest of the
 * kernel and of b43, built against the kernel headers like them.
 */
#include "b43.h"
#include "main.h"
#include "phy_common.h"
#include "lcn_trace.h"

struct b43_wldev *lcn_dev;

/*
 * The flows reach none of this; a call is logged, so that a run that does
 * shows.
 */
#define UNREACHED(f) lcn_log("unreached: " f)

void b43err(struct b43_wl *w, const char *fmt, ...) { lcn_log(fmt); }
void b43err_restart(struct b43_wl *w, const char *fmt, ...) { lcn_log(fmt); }
void b43dbg(struct b43_wl *w, const char *fmt, ...) { }
void b43info(struct b43_wl *w, const char *fmt, ...) { }
void __const_udelay(unsigned long xloops) { }
void __udelay(unsigned long usecs) { }
void msleep(unsigned int msecs) { }
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state) { }
unsigned long volatile jiffies;
unsigned long round_jiffies(unsigned long j) { return j; }

void kfree(const void *p) { }
void *__kmalloc_cache_noprof(struct kmem_cache *s, gfp_t flags, size_t size)
{
	UNREACHED("kmalloc");
	return NULL;
}
struct kmem_cache *kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];
void mutex_lock(struct mutex *lock) { }
void mutex_unlock(struct mutex *lock) { }
void ieee80211_queue_work(struct ieee80211_hw *h, struct work_struct *w)
{
	lcn_core("ieee80211_queue_work");
	/* The only work the flows queue: b43_phy_txpower_adjust_work() */
	if (w == &lcn_dev->wl->txpower_adjust_work)
		lcn_dev->phy.ops->adjust_txpower(lcn_dev);
}

/* cfg80211's, for b43_phy_center_channel() */
int ieee80211_freq_khz_to_channel(u32 freq)
{
	int mhz = KHZ_TO_MHZ(freq);

	if (mhz == 2484)
		return 14;
	if (mhz < 2484)
		return (mhz - 2407) / 5;
	return (mhz - 5000) / 5;
}

/*
 * b43's core: on hardware these make MMIO accesses of their own, which the
 * harness does not emit. The calls are counted.
 */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	lcn_log(reason);
}
void b43_mac_suspend(struct b43_wldev *dev) { lcn_core("b43_mac_suspend"); }
void b43_mac_enable(struct b43_wldev *dev) { lcn_core("b43_mac_enable"); }
void b43_mac_switch_freq(struct b43_wldev *dev, u8 spurmode)
{
	lcn_core("b43_mac_switch_freq");
}
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on)
{
	lcn_core("b43_mac_phy_clock_set");
}
void b43_dummy_transmission(struct b43_wldev *dev, bool ofdm, bool pa_on)
{
	lcn_core("b43_dummy_transmission");
}
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	lcn_core("b43_power_saving_ctl_bits");
}
/* The core's host flags (main.c): the three cells, read and written */
u64 b43_hf_read(struct b43_wldev *dev)
{
	u64 ret;

	ret = b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF3);
	ret <<= 16;
	ret |= b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF2);
	ret <<= 16;
	ret |= b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF1);
	return ret;
}

void b43_hf_write(struct b43_wldev *dev, u64 value)
{
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF1, (u16)value);
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF2,
			(u16)(value >> 16));
	b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_HOSTF3,
			(u16)(value >> 32));
}
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	lcn_core("b43_shm_read16");
	return routing == B43_SHM_SHARED ? lcn_shm_read16(offset) : 0;
}
u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	lcn_core("b43_shm_read32");
	return 0;
}
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	lcn_core("b43_shm_write16");
}
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	lcn_core("b43_shm_write32");
}

/* bcma-next's PMU accesses, as driver_chipcommon_pmu.c makes them */
static void pmu_maskset(u16 addr, u16 data, u32 offset, u32 mask, u32 set)
{
	lcn_cc_write32(addr, offset);
	lcn_cc_read32(addr);
	lcn_cc_write32(data, (lcn_cc_read32(data) & mask) | set);
}
void bcmn_chipco_pll_write(struct bcmn_drv_cc *cc, u32 offset, u32 value)
{
	lcn_cc_write32(BCMN_CC_PMU_PLLCTL_ADDR, offset);
	lcn_cc_read32(BCMN_CC_PMU_PLLCTL_ADDR);
	lcn_cc_write32(BCMN_CC_PMU_PLLCTL_DATA, value);
}
void bcmn_chipco_pll_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
			     u32 set)
{
	pmu_maskset(BCMN_CC_PMU_PLLCTL_ADDR, BCMN_CC_PMU_PLLCTL_DATA, offset,
		    mask, set);
}
void bcmn_chipco_chipctl_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
				 u32 set)
{
	pmu_maskset(BCMN_CC_PMU_CHIPCTL_ADDR, BCMN_CC_PMU_CHIPCTL_DATA, offset,
		    mask, set);
}
void bcmn_chipco_regctl_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
				u32 set)
{
	pmu_maskset(BCMN_CC_PMU_REGCTL_ADDR, BCMN_CC_PMU_REGCTL_DATA, offset,
		    mask, set);
}
/* The crystal: by default the BCM4313's 20 MHz, as wl's RF PLL setup shows */
u32 bcmn_chipco_get_alp_clock(struct bcmn_drv_cc *cc)
{
	return lcn_env("LCN_XTAL", 20000000);
}
void bcmn_core_pll_ctl(struct bcmn_device *c, u32 req, u32 status, bool on)
{
	lcn_core("bcmn_core_pll_ctl");
}
