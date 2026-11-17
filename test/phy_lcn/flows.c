// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the LCN-PHY harness, built against the kernel headers like
 * the driver. It compiles phy_lcn.c into this file to reach its static
 * functions, puts a BCM4313 (chip rev 1, core rev 24, LCN-PHY rev 1, radio
 * 2064 rev 1) on a bus whose accesses go to trace.c, and runs one flow.
 * Nothing of b43 is modified.
 */
#include "lcn_trace.h"
#include "phy_lcn.c"

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
static struct b43_phy_lcn phy_lcn;
static struct b43_wldev wldev;

static u16 bus_read16(struct b43_bus_dev *d, u16 offset)
{
	return lcn_mmio_read16(offset);
}

static void bus_write16(struct b43_bus_dev *d, u16 offset, u16 value)
{
	lcn_mmio_write16(offset, value);
}

static u32 bus_read32(struct b43_bus_dev *d, u16 offset)
{
	return lcn_mmio_read32(offset);
}

static void bus_write32(struct b43_bus_dev *d, u16 offset, u32 value)
{
	lcn_mmio_write32(offset, value);
}

static u32 agent_read32(struct bcmn_device *c, u16 offset)
{
	return lcn_wrap_read32(offset);
}

static void agent_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	lcn_wrap_write32(offset, value);
}

/* bcma's accesses to a core: only ChipCommon's reach the bus here */
static u32 core_read32(struct bcmn_device *c, u16 offset)
{
	if (c == &cc_core)
		return lcn_cc_read32(offset);
	lcn_log("unreached: bcma read32 of the 802.11 core");
	return 0;
}

static void core_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	if (c == &cc_core)
		lcn_cc_write32(offset, value);
	else
		lcn_log("unreached: bcma write32 of the 802.11 core");
}

/*
 * The SROM rev 8 values, from the board's nvram: on a board with a SROM,
 * ../sprom/decode_r8's output, what bcma extracts from it.
 */
static void fill_sprom(void)
{
	long bf = lcn_nvram("boardflags", 0), bf2 = lcn_nvram("boardflags2", 0);
	int k;

	sprom.revision = lcn_nvram("sromrev", 0);
	sprom.board_rev = lcn_nvram("boardrev", 0);
	sprom.boardflags_lo = bf & 0xffff;
	sprom.boardflags_hi = bf >> 16;
	sprom.boardflags2_lo = bf2 & 0xffff;
	sprom.boardflags2_hi = bf2 >> 16;
	sprom.maxpwr_bg = lcn_nvram("pa0maxpwr", 0);
	sprom.itssi_bg = lcn_nvram("pa0itssit", 0);
	sprom.pa0b0 = lcn_nvram("pa0b0", 0);
	sprom.pa0b1 = lcn_nvram("pa0b1", 0);
	sprom.pa0b2 = lcn_nvram("pa0b2", 0);
	sprom.opo = lcn_nvram("opo", 0);
	sprom.cck2gpo = lcn_nvram("cck2gpo", 0);
	sprom.ofdm2gpo = lcn_nvram("ofdm2gpo", 0);
	for (k = 0; k < 8; k++) {
		char v[16];

		snprintf(v, sizeof(v), "mcs2gpo%d", k);
		sprom.mcs2gpo[k] = lcn_nvram(v, 0);
	}
	sprom.rxpo2g = lcn_nvram("rxpo2g", 0);
	sprom.rssismf2g = lcn_nvram("rssismf2g", 0);
	sprom.rssismc2g = lcn_nvram("rssismc2g", 0);
	sprom.rssisav2g = lcn_nvram("rssisav2g", 0);
	sprom.tri2g = lcn_nvram("tri2g", 0);
	sprom.fem.ghz2.tssipos = lcn_nvram("tssipos2g", 0);
	sprom.fem.ghz2.tr_iso = lcn_nvram("triso2g", 0);
	sprom.rawtempsense = lcn_nvram("rawtempsense", 0);
	sprom.measpower = lcn_nvram("measpower", 0);
	sprom.tempsense_slope = lcn_nvram("tempsense_slope", 0);
	sprom.tempcorrx = lcn_nvram("tempcorrx", 0);
	sprom.tempsense_option = lcn_nvram("tempsense_option", 0);
	sprom.freqoffset_corr = lcn_nvram("freqoffset_corr", 0);
	sprom.iqcal_swp_dis = lcn_nvram("iqcal_swp_dis", 0);
	sprom.hw_iqcal_en = lcn_nvram("hw_iqcal_en", 0);
}

static struct b43_wldev *setup(void)
{
	chan.hw_value = lcn_env("LCN_CHANNEL", 1);
	chan.center_freq = 2407 + 5 * chan.hw_value;
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
	busdev.chip_id = 0x4313;
	busdev.chip_rev = 1;
	busdev.core_rev = 24;
	busdev.bus_sprom = &sprom;
	fill_sprom();

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_lcn;
	wldev.phy.lcn = &phy_lcn;
	wldev.phy.type = B43_PHYTYPE_LCN;
	wldev.phy.rev = 1;
	wldev.phy.radio_ver = 0x2064;
	wldev.phy.radio_rev = 1;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;
	lcn_dev = &wldev;

	b43_phy_lcn_op_prepare_structs(&wldev);
	return &wldev;
}

int lcn_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (!strcmp(name, "full")) {
		/*
		 * What b43 runs of the PHY from its init to the BSS up: the
		 * analog switch of b43_wireless_core_reset(), then
		 * b43_phy_bringup()'s analog switch, b43_phy_init() and RX
		 * antenna.
		 */
		dev->phy.chandef = NULL;
		dev->phy.do_full_init = true;
		dev->phy.ops->switch_analog(dev, true);
		dev->phy.ops->switch_analog(dev, true);
		b43_phy_init(dev);
		dev->phy.ops->set_rx_antenna(dev, B43_ANTENNA_DEFAULT);
		/* mac80211's first config sets the power: the check */
		b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
				      B43_TXPWR_IGNORE_TSSI);
	} else if (!strcmp(name, "switch")) {
		/*
		 * A channel switch after the init, with the init's
		 * calibration results the capture holds (calres.py)
		 */
		struct b43_phy_lcn *lcn = dev->phy.lcn;
		int i;

		lcn->txiqcc[0] = lcn_env("LCN_TXIQCC_A", 0);
		lcn->txiqcc[1] = lcn_env("LCN_TXIQCC_B", 0);
		lcn->txdidq = lcn_env("LCN_TXDIDQ", 0);
		for (i = 0; i < 4; i++) {
			char v[16];

			snprintf(v, sizeof(v), "LCN_LOFT%d", i);
			lcn->loft[i] = lcn_env(v, 0);
		}
		lcn->rxiqcomp[0] = lcn_env("LCN_RXIQCOMP_A", 0);
		lcn->rxiqcomp[1] = lcn_env("LCN_RXIQCOMP_B", 0);
		lcn->cal_done = true;
		b43_switch_channel(dev, chan.hw_value);
		/* b43_op_config() closes the switch with it */
		dev->phy.ops->channel_calibrate(dev, false);
	} else if (!strcmp(name, "watchdog")) {
		/* A turn of the periodic work, with wl's frame count before it */
		dev->phy.lcn->tssi_tx_cnt = lcn_env("LCN_TSSI_TX_CNT", 0);
		dev->phy.ops->pwork_1sec(dev);
	} else
		return -1;
	return 0;
}
