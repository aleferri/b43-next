// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the N-PHY harness, built against the kernel headers like
 * the driver. It compiles phy_n.c into this file to reach its static
 * functions, puts a BCM43217 (N-PHY rev 17, radio 2057 rev 14) on a bus
 * whose accesses go to trace.c, and runs one flow. Nothing of b43 is
 * modified.
 */
#include "n_trace.h"
/* The calibration report reads without disturbing the stream: peek mode */
#define B43_NPHY_REPORT_BEGIN(dev)	n_peek(1)
#define B43_NPHY_REPORT_END(dev)	n_peek(0)
#include "phy_n.c"

static struct ieee80211_channel chan = {
	.band = NL80211_BAND_2GHZ,
};
static struct ieee80211_hw hw;
static struct b43_wl wl;
static struct bcmn_host_ops agent_ops;
static struct bcmn_bus agent_bus;
static struct bcmn_device core, cc_core;
static struct b43_bus_dev busdev;
static struct ssbn_sprom sprom;
static struct b43_phy_n phy_n;
static struct b43_wldev wldev;

static u16 bus_read16(struct b43_bus_dev *d, u16 offset)
{
	return n_mmio_read16(offset);
}

static void bus_write16(struct b43_bus_dev *d, u16 offset, u16 value)
{
	n_mmio_write16(offset, value);
}

static u32 bus_read32(struct b43_bus_dev *d, u16 offset)
{
	return n_mmio_read32(offset);
}

static void bus_write32(struct b43_bus_dev *d, u16 offset, u32 value)
{
	n_mmio_write32(offset, value);
}

static u32 agent_read32(struct bcmn_device *c, u16 offset)
{
	return n_wrap_read32(offset);
}

static void agent_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	n_wrap_write32(offset, value);
}

/* The SROM rev 8 values b43's N-PHY reads, from the board's nvram */
static void fill_sprom(void)
{
	long bf = n_nvram("boardflags", 0), bf2 = n_nvram("boardflags2", 0);
	int c, k;

	sprom.revision = n_nvram("sromrev", 0);
	sprom.boardflags = bf;
	sprom.boardflags2 = bf2;
	sprom.boardflags_lo = bf & 0xffff;
	sprom.boardflags_hi = bf >> 16;
	sprom.boardflags2_lo = bf2 & 0xffff;
	sprom.boardflags2_hi = bf2 >> 16;
	sprom.fem.ghz2.tssipos = n_nvram("tssipos2g", 0);
	sprom.fem.ghz2.extpa_gain = n_nvram("extpagain2g", 0);
	sprom.fem.ghz2.pdet_range = n_nvram("pdetrange2g", 0);
	sprom.fem.ghz2.tr_iso = n_nvram("triso2g", 0);
	sprom.fem.ghz2.antswlut = n_nvram("antswctl2g", 0);
	sprom.elna2g = n_nvram("elna2g", 0);
	sprom.antenna_gain.a0 = n_nvram("ag0", 0);
	sprom.antenna_gain.a1 = n_nvram("ag1", 0);
	sprom.cck2gpo = n_nvram("cck2gpo", 0);
	sprom.ofdm2gpo = n_nvram("ofdm2gpo", 0);
	for (k = 0; k < 8; k++) {
		char v[16];

		snprintf(v, sizeof(v), "mcs2gpo%d", k);
		sprom.mcs2gpo[k] = n_nvram(v, 0);
	}
	for (c = 0; c < 2; c++) {
		char v[16];

		for (k = 0; k < 3; k++) {
			snprintf(v, sizeof(v), "pa2gw%da%d", k, c);
			sprom.core_pwr_info[c].pa_2g[k] = (s16)n_nvram(v, 0);
		}
		snprintf(v, sizeof(v), "maxp2ga%d", c);
		sprom.core_pwr_info[c].maxpwr_2g = n_nvram(v, 0);
		snprintf(v, sizeof(v), "itt2ga%d", c);
		sprom.core_pwr_info[c].itssi_2g = n_nvram(v, 0);
	}
}

/* bcma's accesses to a core: only ChipCommon's reach the bus here */
static u32 core_read32(struct bcmn_device *c, u16 offset)
{
	if (c == &cc_core)
		return n_cc_read32(offset);
	n_log("unreached: bcma read32 of the 802.11 core");
	return 0;
}

static void core_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	if (c == &cc_core)
		n_cc_write32(offset, value);
	else
		n_log("unreached: bcma write32 of the 802.11 core");
}

int b43_modparam_calreport;

void b43info(struct b43_wl *w, const char *fmt, ...)
{
	char line[512];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	n_report(line);
}

static struct b43_wldev *setup(void)
{
	b43_modparam_calreport = n_env("N_CALREPORT", 0);
	chan.hw_value = n_env("N_CHANNEL", 6);
	chan.center_freq = 2407 + 5 * chan.hw_value;
	/* The regulatory limit: the US one at 2.4 GHz, 30 dBm, by default */
	chan.max_power = n_env("N_MAXPWR", 30);
	hw.conf.chandef.chan = &chan;
	hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
	hw.conf.chandef.center_freq1 = chan.center_freq;
	wl.hw = &hw;

	agent_ops.aread32 = agent_read32;
	agent_ops.awrite32 = agent_write32;
	agent_ops.read32 = core_read32;
	agent_ops.write32 = core_write32;
	agent_bus.ops = &agent_ops;
	agent_bus.drv_cc.core = &cc_core;
	core.bus = &agent_bus;
	cc_core.bus = &agent_bus;

	busdev.bus_type = B43_BUS_BCMA;
	busdev.bdev = &core;
	busdev.read16 = bus_read16;
	busdev.write16 = bus_write16;
	busdev.read32 = bus_read32;
	busdev.write32 = bus_write32;
	busdev.flush_writes = false;
	busdev.chip_id = 43217;
	busdev.chip_rev = 0;
	busdev.chip_pkg = 8;
	busdev.core_rev = 30;
	busdev.board_vendor = n_nvram("venid", 0);
	busdev.bus_sprom = &sprom;
	fill_sprom();

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_n;
	wldev.phy.n = &phy_n;
	wldev.phy.type = B43_PHYTYPE_N;
	wldev.phy.rev = 17;
	wldev.phy.radio_ver = 0x2057;
	wldev.phy.radio_rev = 14;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;
	/* b43's hwpctl module parameter, off by default; wl runs it on */
	wldev.phy.hardware_power_control = n_env("N_HWPCTL", 0);

	b43_nphy_op_prepare_structs(&wldev);
	/*
	 * N_PERICAL overrides the periodic calibration mode prepare_structs
	 * sets (2, under which the init runs neither the TX I/Q-LO nor the
	 * RX I/Q calibration): an experiment, not b43's flow.
	 */
	phy_n.perical = n_env("N_PERICAL", phy_n.perical);
	return &wldev;
}

int n_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (!strcmp(name, "full")) {
		/*
		 * What b43 runs of the PHY from its init to the BSS up: the
		 * analog switch of b43_wireless_core_reset() and of
		 * b43_phy_bringup(), then b43_phy_init().
		 */
		dev->phy.chandef = NULL;
		dev->phy.do_full_init = true;
		dev->phy.ops->switch_analog(dev, true);
		dev->phy.ops->switch_analog(dev, true);
		b43_phy_init(dev);
		/* mac80211's first config sets the power: the check */
		b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
				      B43_TXPWR_IGNORE_TSSI);
	} else
		return -1;
	return 0;
}

/*
 * What the files above need from the rest of the kernel and of b43. The
 * flows reach none of it; a call is logged, so that a run that does shows.
 */
#define UNREACHED(f) n_log("unreached: " f)

void b43err(struct b43_wl *w, const char *fmt, ...) { n_log(fmt); }
void b43err_restart(struct b43_wl *w, const char *fmt, ...) { n_log(fmt); }
void __const_udelay(unsigned long xloops) { }
void __udelay(unsigned long usecs) { }
void msleep(unsigned int msecs) { }
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state) { }
unsigned long volatile jiffies;
unsigned long round_jiffies(unsigned long j) { return j; }
/* Allocations from a static arena, never freed: a run is short */
static u8 arena[1 << 18] __aligned(8);
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
	n_core("ieee80211_queue_work");
}

/*
 * b43's core: on hardware these make MMIO accesses of their own, which the
 * harness does not emit. The calls are counted.
 */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	n_log(reason);
}
void b43_mac_suspend(struct b43_wldev *dev) { n_core("b43_mac_suspend"); }
void b43_mac_enable(struct b43_wldev *dev) { n_core("b43_mac_enable"); }
void b43_mac_switch_freq(struct b43_wldev *dev, u8 spurmode)
{
	n_core("b43_mac_switch_freq");
}
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on)
{
	n_core("b43_mac_phy_clock_set");
}
void b43_wireless_core_phy_pll_reset(struct b43_wldev *dev)
{
	n_core("b43_wireless_core_phy_pll_reset");
}
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	n_core("b43_power_saving_ctl_bits");
}
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	n_core("b43_shm_read16");
	return 0;
}
u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	n_core("b43_shm_read32");
	return 0;
}
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	n_core("b43_shm_write16");
}
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	n_core("b43_shm_write32");
}
/* bcma-next's PMU chip control access, as driver_chipcommon_pmu.c makes it */
void bcmn_chipco_chipctl_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
				 u32 set)
{
	n_cc_write32(BCMN_CC_PMU_CHIPCTL_ADDR, offset);
	n_cc_read32(BCMN_CC_PMU_CHIPCTL_ADDR);
	n_cc_write32(BCMN_CC_PMU_CHIPCTL_DATA,
		     (n_cc_read32(BCMN_CC_PMU_CHIPCTL_DATA) & mask) | set);
}
void bcmn_core_pll_ctl(struct bcmn_device *c, u32 req, u32 status, bool on)
{
	n_core("bcmn_core_pll_ctl");
}
void bcmn_pmu_spuravoid_pllupdate(struct bcmn_drv_cc *cc, int spuravoid)
{
	n_core("bcmn_pmu_spuravoid_pllupdate");
}
/* The hooks of -finstrument-functions (Makefile, N_FN=1) */
void __cyg_profile_func_enter(void *fn, void *site)
	__attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site)
	__attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *site) { n_marker(fn, 0); }
void __cyg_profile_func_exit(void *fn, void *site) { n_marker(fn, 1); }

/* b43's debug messages are not errors: they are not counted */
void b43dbg(struct b43_wl *w, const char *fmt, ...) { }
unsigned int __sw_hweight32(unsigned int w) { return __builtin_popcount(w); }
u64 b43_hf_read(struct b43_wldev *dev)
{
	n_core("b43_hf_read");
	return 0;
}
void b43_hf_write(struct b43_wldev *dev, u64 value)
{
	n_core("b43_hf_write");
}
/* ChipCommon's GPIO control, through bcma: outside the 802.11 core */
u32 bcmn_chipco_gpio_control(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	n_core("bcmn_chipco_gpio_control");
	return 0;
}
