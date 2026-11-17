// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the LP-PHY harness, built against the kernel headers like
 * the driver. It compiles phy_lp.c into this file to reach its static
 * functions, puts a BCM4312 (LP-PHY rev 1, radio 2062 rev 2) on an SSB bus
 * whose accesses go to trace.c, and runs one flow. Nothing of b43 is
 * modified.
 */
#include "lp_trace.h"
/* The statistics' readback does not disturb the stream: peek mode */
#define B43_CAL_PEEK(dev, on)	lp_peek(on)
#include "phy_lp.c"
/* ssb-next/pci.c's SROM rev 8 extractor, cut out by the Makefile */
#include "sprom_r8.c"

static struct ieee80211_channel chan = {
	.band = NL80211_BAND_2GHZ,
};
static struct ieee80211_hw hw;
static struct b43_wl wl;
static struct ssbn_bus ssb;
static struct ssbn_device core, cc_core;
static struct ssbn_bus_ops cc_ops;
static struct b43_bus_dev busdev;
static struct ssbn_sprom sprom;
static struct b43_phy_lp phy_lp;
static struct b43_wldev wldev;

static u16 bus_read16(struct b43_bus_dev *d, u16 offset)
{
	return lp_mmio_read16(offset);
}

static void bus_write16(struct b43_bus_dev *d, u16 offset, u16 value)
{
	lp_mmio_write16(offset, value);
}

static u32 bus_read32(struct b43_bus_dev *d, u16 offset)
{
	return lp_mmio_read32(offset);
}

static void bus_write32(struct b43_bus_dev *d, u16 offset, u32 value)
{
	lp_mmio_write32(offset, value);
}

/* ssb-next's ChipCommon accesses, through its fixed window of the BAR */
static u32 cc_read32(struct ssbn_device *d, u16 offset)
{
	return lp_cc_read32(offset);
}

static void cc_write32(struct ssbn_device *d, u16 offset, u32 value)
{
	lp_cc_write32(offset, value);
}

static void fill_sprom(void)
{
	u16 words[SSBN_SPROMSIZE_WORDS_R4];
	int n = lp_srom(words, ARRAY_SIZE(words));

	if (n != ARRAY_SIZE(words)) {
		lp_log("no SROM rev 8 dump: LP_SROM");
		return;
	}
	sprom.revision = words[n - 1] & 0xff;
	sprom_extract_r8(&sprom, words);
	/* The PCI subsystem IDs, which the SROM supplies */
	ssb.boardinfo.vendor = words[SPOFF(SSBN_SPROM1_SVID)];
	ssb.boardinfo.type = words[SPOFF(SSBN_SPROM1_SPID)];
}

/*
 * The calibration statistics of a build with CONFIG_B43_NEXT_CALSTAT, on with
 * LP_CALSTAT=1: calstat.c keeps them as in the driver, and they are printed
 * at the end of the flow.
 */
static struct b43_calstat calstat;
static struct b43_cal_ring cal_rings[ARRAY_SIZE(lpphy_cal_entries)];
static u8 cal_mem[64 * 1024];

static void calstat_print(void)
{
	unsigned int entry, age;
	char line[1024];
	u32 seq = 0;

	while (b43_cal_next(&calstat, &seq, &entry, &age)) {
		b43_cal_format(&calstat, entry, age, 0, line, sizeof(line) - 1);
		strcat(line, "\n");
		lp_report(line);
	}
}

void b43info(struct b43_wl *w, const char *fmt, ...)
{
	char line[512];
	va_list ap;

	va_start(ap, fmt);
	vscnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	lp_report(line);
}

static struct b43_wldev *setup(void)
{
	chan.hw_value = lp_env("LP_CHANNEL", 1);
	chan.center_freq = chan.hw_value == 14 ? 2484 : 2407 + 5 * chan.hw_value;
	/* cfg80211's limit, dBm EIRP; 20 binds nothing on the HP 550 */
	chan.max_power = lp_env("LP_MAX_POWER", 20);
	hw.conf.chandef.chan = &chan;
	hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
	hw.conf.chandef.center_freq1 = chan.center_freq;
	wl.hw = &hw;

	ssb.bustype = SSBN_BUSTYPE_PCI;
	ssb.chip_id = 0x4312;
	ssb.chip_rev = 1;
	ssb.chip_package = 0;
	cc_ops.read32 = cc_read32;
	cc_ops.write32 = cc_write32;
	cc_core.ops = &cc_ops;
	cc_core.bus = &ssb;
	ssb.chipco.dev = &cc_core;
	ssb.chipco.capabilities = SSBN_CHIPCO_CAP_PMU;
	/* What ssbn_pmu1_pllinit_r0() leaves on the 4312 */
	ssb.chipco.pmu.crystalfreq = 20000;
	core.bus = &ssb;

	busdev.bus_type = B43_BUS_SSB;
	busdev.sdev = &core;
	busdev.read16 = bus_read16;
	busdev.write16 = bus_write16;
	busdev.read32 = bus_read32;
	busdev.write32 = bus_write32;
	busdev.flush_writes = false;
	busdev.chip_id = ssb.chip_id;
	busdev.chip_rev = ssb.chip_rev;
	busdev.chip_pkg = ssb.chip_package;
	busdev.core_rev = 15;
	busdev.bus_sprom = &sprom;
	fill_sprom();
	busdev.board_vendor = ssb.boardinfo.vendor;
	busdev.board_type = ssb.boardinfo.type;
	busdev.board_rev = sprom.board_rev;

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_lp;
	wldev.phy.lp = &phy_lp;
	wldev.phy.type = B43_PHYTYPE_LP;
	wldev.phy.rev = 1;
	wldev.phy.radio_manuf = 0x17f;
	wldev.phy.radio_ver = 0x2062;
	wldev.phy.radio_rev = 2;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;

	b43_lpphy_op_prepare_structs(&wldev);

	if (lp_env("LP_CALSTAT", 0)) {
		b43_calstat_setup(&calstat, &wldev, lpphy_cal_entries,
				  ARRAY_SIZE(lpphy_cal_entries), cal_rings);
		if (calstat.size > sizeof(cal_mem))
			lp_log("calstat: cal_mem too small");
		else
			b43_calstat_start(&calstat, cal_mem);
		wldev.calstat = &calstat;
	}
	return &wldev;
}

/*
 * What b43 runs of the PHY from its init to the MAC enable: the analog
 * switch of b43_wireless_core_reset() and of b43_phy_bringup(), then
 * b43_phy_init().
 */
static void lp_init(struct b43_wldev *dev)
{
	dev->phy.chandef = NULL;
	dev->phy.do_full_init = true;
	dev->phy.ops->switch_analog(dev, true);
	dev->phy.ops->switch_analog(dev, true);
	b43_phy_init(dev);
}

int lp_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (!strcmp(name, "full")) {
		lp_init(dev);
	} else if (!strcmp(name, "station")) {
		unsigned int ch[256];
		int i, n = lp_channels(ch, ARRAY_SIZE(ch));

		/*
		 * The init, then mac80211's channel switches with the MAC up,
		 * as b43_op_config() runs them: the MAC suspended, the
		 * channel set, b43_switch_channel(), the MAC enabled. A
		 * channel 0 is the interface going down and up again.
		 */
		lp_init(dev);
		for (i = 0; i < n; i++) {
			lp_mark(ch[i] ? "SWITCH" : "UP");
			/* An interface down and up: b43_wireless_core_init() */
			if (!ch[i]) {
				dev->phy.ops->prepare_structs(dev);
				lp_init(dev);
				continue;
			}
			b43_mac_suspend(dev);
			chan.hw_value = ch[i];
			chan.center_freq = ch[i] == 14 ? 2484 : 2407 + 5 * ch[i];
			hw.conf.chandef.center_freq1 = chan.center_freq;
			dev->phy.chandef = &hw.conf.chandef;
			dev->phy.channel = ch[i];
			b43_switch_channel(dev, ch[i]);
			b43_mac_enable(dev);
		}
	} else
		return -1;
	calstat_print();
	return 0;
}

/*
 * What the files above need from the rest of the kernel and of b43. The
 * flows reach none of it; a call is logged, so that a run that does shows.
 */
#define UNREACHED(f) lp_log("unreached: " f)

void b43err(struct b43_wl *w, const char *fmt, ...) { lp_log(fmt); }
void b43err_restart(struct b43_wl *w, const char *fmt, ...) { lp_log(fmt); }
void b43warn(struct b43_wl *w, const char *fmt, ...) { lp_log(fmt); }
/* b43's debug messages are not errors: they are not counted */
void b43dbg(struct b43_wl *w, const char *fmt, ...) { }
void __const_udelay(unsigned long xloops) { }
void __udelay(unsigned long usecs) { }
void msleep(unsigned int msecs) { }
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state) { }
unsigned long volatile jiffies;
/* read_poll_timeout() */
ktime_t ktime_get(void) { return 0; }
int __cond_resched(void) { return 0; }
long __static_call_return0(void) { return 0; }
DEFINE_STATIC_CALL_RET0(might_resched, __cond_resched);
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
	lp_core("ieee80211_queue_work");
}

/*
 * b43's core: on hardware these make MMIO accesses of their own, which the
 * harness does not emit. The calls are counted.
 */
void b43_controller_restart(struct b43_wldev *dev, const char *reason)
{
	lp_log(reason);
}
void b43_mac_suspend(struct b43_wldev *dev) { lp_core("b43_mac_suspend"); }
void b43_mac_enable(struct b43_wldev *dev) { lp_core("b43_mac_enable"); }
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	lp_core("b43_power_saving_ctl_bits");
}
/* Shared memory: OBJ ops, reads served from wl's */
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	return lp_shm_read16(routing, offset);
}
u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	lp_core("b43_shm_read32");
	return 0;
}
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	lp_shm_write16(routing, offset, value);
}
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	lp_core("b43_shm_write32");
}
void b43_dummy_transmission(struct b43_wldev *dev, bool ofdm, bool pa_on)
{
	lp_core("b43_dummy_transmission");
}
u64 b43_hf_read(struct b43_wldev *dev)
{
	lp_core("b43_hf_read");
	return 0;
}
void b43_hf_write(struct b43_wldev *dev, u64 value)
{
	lp_core("b43_hf_write");
}

/* The hooks of -finstrument-functions (Makefile, LP_FN=1) */
void __cyg_profile_func_enter(void *fn, void *site)
	__attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site)
	__attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *site) { lp_marker(fn, 0); }
void __cyg_profile_func_exit(void *fn, void *site) { lp_marker(fn, 1); }

/* cfg80211's, for b43_phy_center_channel() */
int ieee80211_freq_khz_to_channel(u32 freq)
{
	int mhz = KHZ_TO_MHZ(freq);

	if (mhz == 2484)
		return 14;
	if (mhz < 2484)
		return (mhz - 2407) / 5;
	if (mhz >= 4910 && mhz <= 4980)
		return (mhz - 4000) / 5;
	return (mhz - 5000) / 5;
}

/* What driver_chipcommon_pmu.c prints, outside the LDO functions */
void _dev_err(const struct device *dev, const char *fmt, ...) { lp_log(fmt); }
void _dev_emerg(const struct device *dev, const char *fmt, ...) { lp_log(fmt); }
void _dev_info(const struct device *dev, const char *fmt, ...) { UNREACHED("_dev_info"); }
void __dynamic_dev_dbg(struct _ddebug *descriptor, const struct device *dev,
		       const char *fmt, ...) { }
void dump_stack(void) { lp_log("WARN"); }
