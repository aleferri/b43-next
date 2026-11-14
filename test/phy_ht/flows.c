// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the HT-PHY harness, built against the kernel headers like
 * the driver. It compiles phy_ht.c into this file to reach its static
 * functions, puts a BCM4331 (HT-PHY rev 1, radio 2059) on a bus whose
 * accesses go to trace.c, and runs one flow. Nothing of b43 is modified.
 */
#include "phy_ht.c"
#include "ht_trace.h"

static struct ieee80211_channel chan = {
	.band = NL80211_BAND_2GHZ,
};
static struct ieee80211_hw hw;
static struct b43_wl wl;
static struct bcmn_host_ops agent_ops;
static struct bcmn_bus agent_bus;
static struct bcmn_device core;
static struct b43_bus_dev busdev;
static struct ssbn_sprom sprom;
static struct b43_phy_ht phy_ht;
static struct b43_wldev wldev;

static u16 bus_read16(struct b43_bus_dev *d, u16 offset)
{
	return ht_mmio_read16(offset);
}

static void bus_write16(struct b43_bus_dev *d, u16 offset, u16 value)
{
	ht_mmio_write16(offset, value);
}

static u32 bus_read32(struct b43_bus_dev *d, u16 offset)
{
	return ht_mmio_read32(offset);
}

static void bus_write32(struct b43_bus_dev *d, u16 offset, u32 value)
{
	ht_mmio_write32(offset, value);
}

static u32 agent_read32(struct bcmn_device *c, u16 offset)
{
	return ht_wrap_read32(offset);
}

static void agent_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	ht_wrap_write32(offset, value);
}

/* The core's 16-bit shared memory accessors, one op each on the bus */
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	return ht_shm_read16(routing, offset);
}

void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	ht_shm_write16(routing, offset, value);
}

static struct b43_wldev *setup(void)
{
	int c;

	chan.hw_value = ht_env("HT_CHANNEL", 6);
	chan.center_freq = 2407 + 5 * chan.hw_value;
	hw.conf.chandef.chan = &chan;
	/* HT_CENTER: the centre channel of a 40 MHz block */
	if (ht_env("HT_CENTER", 0)) {
		hw.conf.chandef.width = NL80211_CHAN_WIDTH_40;
		hw.conf.chandef.center_freq1 = 2407 + 5 * ht_env("HT_CENTER", 0);
	} else {
		hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
		hw.conf.chandef.center_freq1 = chan.center_freq;
	}
	wl.hw = &hw;

	agent_ops.aread32 = agent_read32;
	agent_ops.awrite32 = agent_write32;
	agent_bus.ops = &agent_ops;
	core.bus = &agent_bus;

	busdev.bus_type = B43_BUS_BCMA;
	busdev.bdev = &core;
	busdev.read16 = bus_read16;
	busdev.write16 = bus_write16;
	busdev.read32 = bus_read32;
	busdev.write32 = bus_write32;
	busdev.flush_writes = false;
	busdev.chip_id = 0x4331;
	busdev.core_rev = 29;
	busdev.bus_sprom = &sprom;

	sprom.revision = ht_nvram("sromrev", 0);
	sprom.boardflags_lo = ht_nvram("boardflags", 0) & 0xffff;
	sprom.boardflags_hi = ht_nvram("boardflags", 0) >> 16;
	sprom.boardflags2_lo = ht_nvram("boardflags2", 0) & 0xffff;
	sprom.boardflags2_hi = ht_nvram("boardflags2", 0) >> 16;
	for (c = 0; c < 3; c++) {
		char v[16];
		int k;

		for (k = 0; k < 3; k++) {
			snprintf(v, sizeof(v), "pa2gw%da%d", k, c);
			sprom.core_pwr_info[c].pa_2g[k] = (s16)ht_nvram(v, 0);
		}
		snprintf(v, sizeof(v), "maxp2ga%d", c);
		sprom.core_pwr_info[c].maxpwr_2g = ht_nvram(v, 0);
	}

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_ht;
	wldev.phy.ht = &phy_ht;
	wldev.phy.type = B43_PHYTYPE_HT;
	wldev.phy.rev = 1;
	wldev.phy.radio_ver = 0x2059;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;

	b43_phy_ht_op_prepare_structs(&wldev);
	/* The TX gain index a BSS before left, from its RX calibration */
	phy_ht.tx_gain_idx = ht_env("HT_TX_GAIN_IDX", phy_ht.tx_gain_idx);
	return &wldev;
}

/* The target the configuration before computed, on HT_PREV_CHANNEL */
static void prev_target(struct b43_wldev *dev)
{
	chan.hw_value = ht_env("HT_PREV_CHANNEL", chan.hw_value);
	b43_phy_ht_op_recalc_txpower(dev, true);
	chan.hw_value = ht_env("HT_CHANNEL", 6);
}

/*
 * The 1 s periodic work, once per watchdog turn of the window, and the
 * noise sample's interrupt, which comes before wl's turn ends.
 */
static void run_turns(struct b43_wldev *dev)
{
	long k, n = ht_env("HT_WD_TURNS", 0);

	for (k = 0; k < n; k++) {
		ht_turn(k);
		b43_phy_ht_op_pwork_1sec(dev);
		b43_phy_ht_op_noise_sample_done(dev);
	}
}

int ht_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (!strcmp(name, "power_fix"))
		b43_phy_ht_tx_power_fix(dev);
	else if (!strcmp(name, "pctl_off"))
		b43_phy_ht_tx_power_ctl(dev, false);
	else if (!strcmp(name, "idle_tssi"))
		b43_phy_ht_tx_power_ctl_idle_tssi(dev);
	else if (!strcmp(name, "tssi_setup"))
		b43_phy_ht_tssi_setup(dev);
	else if (!strcmp(name, "pctl_on"))
		b43_phy_ht_tx_power_ctl(dev, true);
	else if (!strcmp(name, "full")) {
		/*
		 * What b43 runs of the PHY from its init to the BSS up: the
		 * analog switch of b43_wireless_core_reset() and of
		 * b43_phy_bringup(), then b43_phy_init().
		 */
		dev->phy.chandef = NULL;
		dev->phy.do_full_init = true;
		dev->mac_suspended = 1;
		dev->phy.ops->switch_analog(dev, true);
		dev->phy.ops->switch_analog(dev, true);
		b43_phy_init(dev);
		/* The MAC enabled at the end of the core's init */
		b43_mac_enable(dev);
		/* mac80211's first config sets the power: the check, then its work */
		b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
				      B43_TXPWR_IGNORE_TSSI);
		dev->phy.ops->adjust_txpower(dev);
		run_turns(dev);
	} else if (!strcmp(name, "ap_start")) {
		/*
		 * The bss_start op when beaconing starts: the PHY init on a PHY
		 * initialised once, and what follows it. What the configuration
		 * before left, the RC calibration's results, the table 0x25 pair
		 * and the TX power target, is given from the capture.
		 */
		dev->phy.do_full_init = false;
		phy_ht.rccal[0] = ht_env("HT_RCCAL0", 0);
		phy_ht.rccal[1] = ht_env("HT_RCCAL1", 0);
		phy_ht.rccal[2] = ht_env("HT_RCCAL2", 0);
		phy_ht.t25_off = ht_env("HT_T25", 0);
		phy_ht.t25_off2 = ht_env("HT_T25B", 0);
		/* The width the up left the core at */
		if (b43_is_40mhz(dev))
			dev->phy.bw_clkbits = B43_BCMA_IOCTL_PHY_BW_40MHZ;
		phy_ht.t25_set = true;
		phy_ht.wd_ticks = ht_env("HT_WD_TICK0", 0);
		phy_ht.glitch_high = ht_env("HT_GLITCH_HIGH", 0);
		/* The configuration before was on HT_PREV_CHANNEL, if any */
		phy_ht.home_channel = ht_env("HT_PREV_CHANNEL", chan.hw_value);
		phy_ht.home_type = b43_phy_ht_tune_type(dev);
		prev_target(dev);
		/* b43_op_bss_info_changed() suspends the MAC around it */
		b43_mac_suspend(dev);
		b43_phy_ht_op_bss_start(dev);
		b43_mac_enable(dev);
		run_turns(dev);
	} else if (!strcmp(name, "pctl_init")) {
		/*
		 * The TX power control at the end of b43_phy_ht_op_init(); on
		 * a later init (HT_LATER) with the target of the configuration
		 * before (HT_PREV_CHANNEL).
		 */
		bool saved = phy_ht.tx_pwr_ctl;

		if (ht_env("HT_LATER", 0))
			prev_target(dev);
		b43_phy_ht_tx_power_fix(dev);
		b43_phy_ht_tx_power_ctl(dev, false);
		b43_phy_ht_tx_power_ctl_idle_tssi(dev);
		b43_phy_ht_tx_power_ctl_setup(dev);
		b43_phy_ht_tssi_setup(dev);
		b43_phy_ht_tx_power_ctl(dev, saved);
	} else
		return -1;
	return 0;
}

/*
 * What the files above need from the rest of the kernel and of b43. The
 * flows reach none of it; a call is logged, so that a run that does shows.
 */
#define UNREACHED(f) ht_log("unreached: " f)

void b43err(struct b43_wl *w, const char *fmt, ...) { ht_log(fmt); }
void b43err_restart(struct b43_wl *w, const char *fmt, ...) { ht_log(fmt); }
void __const_udelay(unsigned long xloops) { }
void __udelay(unsigned long usecs) { }
void msleep(unsigned int msecs) { }
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state) { }
unsigned long volatile jiffies;
unsigned long round_jiffies(unsigned long j) { return j; }
/* Allocations from a static arena, never freed: a run is short */
static u8 arena[1 << 16] __aligned(8);
static size_t arena_used;

static void *arena_alloc(size_t size)
{
	void *p;

	size = ALIGN(size, 8);
	if (arena_used + size > sizeof(arena)) {
		UNREACHED("kmalloc beyond the arena");
		return NULL;
	}
	p = arena + arena_used;
	arena_used += size;
	memset(p, 0, size);
	return p;
}

void kfree(const void *p) { }
void *__kmalloc_cache_noprof(struct kmem_cache *s, gfp_t flags, size_t size)
{
	return arena_alloc(size);
}
void *__kmalloc_noprof(size_t size, gfp_t flags)
{
	return arena_alloc(size);
}

struct kmem_cache *kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];
void mutex_lock(struct mutex *lock) { }
void mutex_unlock(struct mutex *lock) { }
void ieee80211_queue_work(struct ieee80211_hw *h, struct work_struct *w)
{
	ht_core("ieee80211_queue_work");
}

/*
 * b43's core: on hardware these make MMIO accesses of their own, which the
 * harness does not emit. The calls are counted.
 */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	ht_log(reason);
}
void b43_mac_switch_freq(struct b43_wldev *dev, u8 spurmode)
{
	ht_core("b43_mac_switch_freq");
}
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on)
{
	ht_core("b43_mac_phy_clock_set");
}
void b43_wireless_core_phy_pll_reset(struct b43_wldev *dev)
{
	ht_core("b43_wireless_core_phy_pll_reset");
}
u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	ht_core("b43_shm_read32");
	return 0;
}
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	ht_core("b43_shm_write32");
}
void bcmn_core_pll_ctl(struct bcmn_device *c, u32 req, u32 status, bool on)
{
	ht_core("bcmn_core_pll_ctl");
}
void bcmn_pmu_spuravoid_pllupdate(struct bcmn_drv_cc *cc, int spuravoid)
{
	ht_core("bcmn_pmu_spuravoid_pllupdate");
}

/*
 * The core calls of the watchdog's turn and of the MAC's suspend and enable,
 * copies of main.c's as they reach the bus. b43_power_saving_ctl_bits() is
 * main.c's as its FIXME leaves it: hardware power saving off, awake but on
 * an HT-PHY's AP that does not beacon yet.
 */
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	bool awake = (ps_flags & B43_PS_AWAKE) ||
		     !b43_is_mode(dev->wl, NL80211_IFTYPE_AP) ||
		     (dev->wl->vif && dev->wl->vif->bss_conf.enable_beacon);
	u32 old = b43_read32(dev, B43_MMIO_MACCTL);
	u32 macctl = (old & ~(B43_MACCTL_HWPS | B43_MACCTL_AWAKE)) |
		     (awake ? B43_MACCTL_AWAKE : 0);
	int i;

	if (macctl == old)
		return;
	b43_macctl_write(dev, macctl);
	b43_read32(dev, B43_MMIO_MACCTL);
	for (i = 0; awake && i < 100; i++)
		if (b43_shm_read16(dev, B43_SHM_SHARED, B43_SHM_SH_UCODESTAT) !=
		    B43_SHM_SH_UCODESTAT_SLEEP)
			break;
}

void b43_mac_suspend(struct b43_wldev *dev)
{
	int i;

	if (dev->mac_suspended++)
		return;
	b43_power_saving_ctl_bits(dev, B43_PS_AWAKE);
	b43_macctl_maskset(dev, ~B43_MACCTL_ENABLED, 0);
	b43_read32(dev, B43_MMIO_MACCTL);
	for (i = 0; i < 75; i++)
		if (b43_read32(dev, B43_MMIO_GEN_IRQ_REASON) &
		    B43_IRQ_MAC_SUSPENDED)
			return;
	ht_log("MAC suspend failed");
}

void b43_mac_enable(struct b43_wldev *dev)
{
	if (--dev->mac_suspended)
		return;
	b43_macctl_maskset(dev, ~0, B43_MACCTL_ENABLED);
	b43_write32(dev, B43_MMIO_GEN_IRQ_REASON, B43_IRQ_MAC_SUSPENDED);
	b43_read32(dev, B43_MMIO_MACCTL);
	b43_read32(dev, B43_MMIO_GEN_IRQ_REASON);
	b43_power_saving_ctl_bits(dev, 0);
}

void b43_tsf_read(struct b43_wldev *dev, u64 *tsf)
{
	u32 low = b43_read32(dev, B43_MMIO_REV3PLUS_TSF_LOW);

	*tsf = (u64)b43_read32(dev, B43_MMIO_REV3PLUS_TSF_HIGH) << 32 | low;
}

void b43_watchdog_poll(struct b43_wldev *dev)
{
	int i;

	b43_read16(dev, B43_MMIO_PSM_PC_REG1);
	b43_read32(dev, B43_MMIO_RADIO_HWENABLED_HI);
	for (i = 0; i < 6; i++)
		b43_read32(dev, B43_MMIO_DMA0_REASON + i * 8);
	ht_core("b43_dma_rx_repost");
}
