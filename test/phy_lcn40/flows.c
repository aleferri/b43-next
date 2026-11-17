// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the LCN40-PHY harness, ../phy_lcn/flows.c for the HP
 * 2230's BCM43142 (chip rev 1, core rev 33, LCN40-PHY rev 3, radio 2065
 * rev 0). It compiles phy_lcn40.c into this file and runs one flow on the
 * bus of ../phy_lcn/trace.c. Nothing of b43 is modified.
 */
#include "lcn_trace.h"
#include "phy_lcn40.c"

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

/* The SROM rev 10 values phy_lcn40.c reads, from ../sprom/decode_r8 */
static void fill_sprom(void)
{
	long bf = lcn_nvram("boardflags", 0), bf2 = lcn_nvram("boardflags2", 0);

	sprom.revision = lcn_nvram("sromrev", 0);
	sprom.board_rev = lcn_nvram("boardrev", 0);
	sprom.boardflags_lo = bf & 0xffff;
	sprom.boardflags_hi = bf >> 16;
	sprom.boardflags2_lo = bf2 & 0xffff;
	sprom.boardflags2_hi = bf2 >> 16;
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
	busdev.chip_id = 43142;
	busdev.chip_rev = 1;
	busdev.core_rev = 33;
	busdev.bus_sprom = &sprom;
	fill_sprom();

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_lcn40;
	wldev.phy.type = B43_PHYTYPE_LCN40;
	wldev.phy.rev = 3;
	wldev.phy.radio_ver = 0x2065;
	wldev.phy.radio_rev = 0;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;
	lcn_dev = &wldev;

	b43_phy_lcn40_op_prepare_structs(&wldev);
	return &wldev;
}

int lcn_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (strcmp(name, "full"))
		return -1;
	/*
	 * What b43 runs of the PHY from its init to the BSS up: the analog
	 * switch of b43_wireless_core_reset() and of b43_phy_bringup(), then
	 * b43_phy_init(), and the TX power check of mac80211's first config.
	 */
	dev->phy.chandef = NULL;
	dev->phy.do_full_init = true;
	dev->phy.ops->switch_analog(dev, true);
	dev->phy.ops->switch_analog(dev, true);
	b43_phy_init(dev);
	b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
			      B43_TXPWR_IGNORE_TSSI);
	return 0;
}
