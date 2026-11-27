// SPDX-License-Identifier: GPL-2.0
/*
 * The caller: it starts the real b43 and collects its ops.
 *
 * The hook is not a direct call to `b43_bcma_probe`, which is static.
 * `module_init(b43_init)` with -DMODULE creates the `init_module` alias, so:
 *
 *     init_module()  ->  b43_init()  ->  __bcmn_driver_register(&b43_bcma_driver)
 *
 * and our `__bcmn_driver_register` captures the `probe`. From there we call
 * it with a fake core. So b43 is not touched and its attach sequence is not
 * duplicated: b43 walks it itself.
 *
 * Compiled with the KERNEL's flags, because it builds real `struct
 * bcmn_device` and `struct bcmn_bus`: their fields are inputs of b43's gates
 * (`core->id.rev`, `chipinfo.id`, `chipinfo.rev`), and with invented
 * structures the gates would take the wrong branch silently.
 *
 * bcma_stub.c emits the ops through the vtable. The oracle serves the reads:
 * without B43_READ_ORACLE every read is zero and the trace is not a
 * measurement -- see trace_out.c.
 */
#include <bcma-next/bcmn.h>
#include <linux/pci.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <ssb-next/ssbn.h>
#include <linux/string.h>
#include <linux/etherdevice.h>

#include "../board_profile.h"
#include "chip_link.h"
#include "bcma-next/bcmn_private.h"

void b43_test_cc_seed(u16 off, u32 val);
void b43_test_chip_identity(unsigned int core_rev, u16 phy_ver, u16 radio,
			    u16 radio_rev);
#include "trace_out.h"

extern const struct bcmn_host_ops b43_test_bcma_ops;

/*
 * Compiled with the kernel's flags, so without stdio: the message goes to a
 * function of trace_out.c, which is user code. Including <stdio.h> here
 * would break -nostdinc, which is exactly the separation that keeps the
 * suite's two halves compilable.
 */
void b43_trace_note(const char *fmt, int arg);

/* Captured by __bcmn_driver_register in kernel_shim.c. */
extern int (*b43_test_probe)(struct bcmn_device *core);
extern void (*b43_test_remove)(struct bcmn_device *core);

int init_module(void);
void cleanup_module(void);

/*
 * The 802.11 core: corerev 42 on all the collection's boards (ucode42, AC
 * rev1, `corerev 0x2a` in every wl1_revinfo.txt). Chip, chiprev and PCI
 * deviceid change from board to board and come from the profile: they are
 * the values that decide the gates -- b43_supported_bands reads
 * `bus->host_pci->device` when the hosttype is PCI -- so they are board data
 * and not a choice made here.
 */
#define TEST_CORE_REV		42
#define TEST_CORE_ID		BCMN_CORE_80211

static struct pci_dev test_pci;
static struct bcmn_bus test_bus;
static struct bcmn_device test_core;
static struct bcmn_device test_cc_core;
static struct bcmn_device test_host_core;
/* What bcma gives a PCI host's cores for DMA, the PCI device: dma-mapping.h's
 * inlines read it before they call the stubs. */
static struct device test_dma_dev;

#define TEST_PHY_TYPE_AC	0x0b	/* B43_PHYTYPE_AC */

/* The SPROM from an NVRAM dump, for a board without one */
void bcm47xx_fill_sprom(struct ssbn_sprom *sprom, const char *prefix,
			bool fallback);

static char vchip_board[256];

static void build_core(void)
{
	const struct board_profile *board =
		board_profile_lookup(b43_test_env("B43_BOARD"));
	unsigned int core_rev = board->core_rev ?: TEST_CORE_REV;

	/*
	 * The virtual chip's board: the core revision b43 sees, the PHY
	 * revision, and the radio's identity as b43 reads it on corerev 40/42
	 * (0: revision, 1: id). PHY 0x000b (core count) reads 0x2bdb and radio
	 * 0x090b (PLL locked) 0x0100 on the D6220 and vd625 captures; the chip
	 * models neither.
	 */
	if (board->phy_type == 0x07)
		/* The radio's identity below corerev 40: rev, id low, id high */
		snprintf(vchip_board, sizeof(vchip_board),
			 "--corerev %u --phy-ver 0x%04x --radio 0=0x%x "
			 "--radio 1=0x%x --radio 2=0x%x --tx-engine 20,10,30",
			 core_rev, board->phy_type << 8 | board->phy_rev,
			 board->radio_rev, board->radio_ver & 0xff,
			 board->radio_ver >> 8);
	else
		snprintf(vchip_board, sizeof(vchip_board),
			 "--corerev %u --phy-ver 0x%04x --phy 0xb=0x2bdb "
			 "--radio-id 0x%x --radio 0=0x%x --radio 1=0x%x --radio 0x90b=0x100 "
			 "--tx-engine 20,10,30",
			 core_rev, (board->phy_type ?: TEST_PHY_TYPE_AC) << 8 |
			 board->phy_rev,
			 board->radio_ver, board->radio_rev, board->radio_ver);
	b43_chip_board(vchip_board);
	if (board->phy_type)
		b43_test_chip_identity(core_rev, board->phy_type << 8 |
				       board->phy_rev, board->radio_ver,
				       board->radio_rev);
	b43_trace_ht_tables(board->phy_type == 0x07);

	test_bus.ops = &b43_test_bcma_ops;
	test_bus.hosttype = BCMN_HOSTTYPE_PCI;
	test_pci.device = board->pci_device;
	test_bus.host_pci = &test_pci;
	test_bus.chipinfo.id = board->chip_id;
	/* B43_CHIP_REV: a chip rev no capture has (pmu_gate.sh). */
	test_bus.chipinfo.rev = b43_test_env_long("B43_CHIP_REV",
						   board->chip_rev);
	test_bus.chipinfo.pkg = 0x1;

		/*
		 * b43 reads the ChipCommon for the SPROM and the GPIOs. The core is
		 * there because a null pointer in there is not a branch b43 handles.
		 */
	test_cc_core.bus = &test_bus;
	test_cc_core.id.id = BCMN_CORE_CHIPCOMMON;
	/* Rev 43 on the 4360 family (archer-t5e EROM); bcma reads chipstatus
	 * and capabilities_ext only from rev 11 and 35. */
	test_cc_core.id.rev = 43;
	test_bus.drv_cc.core = &test_cc_core;
	b43_test_cc_seed(BCMN_CC_ID, board->chip_id |
			 ((u32)test_bus.chipinfo.rev << BCMN_CC_ID_REV_SHIFT));
	b43_test_cc_seed(BCMN_CC_CAP, board->cc_caps);
	b43_test_cc_seed(BCMN_CC_CAP_EXT, board->cc_capext);
	/* B43_CC_CHIPSTATUS: a strap no capture has; an oracle overrides it. */
	b43_test_cc_seed(BCMN_CC_CHIPSTAT,
			 b43_test_env_long("B43_CC_CHIPSTATUS",
					   board->cc_chipstatus));
	b43_test_cc_seed(BCMN_CC_PMU_CAP, board->pmu_caps);

	/*
	 * The host interface: the PCIe2 of the 4360 family, rev 1 in the
	 * archer's EROM, or the first generation's PCIe, between rev 15 and 20
	 * on the 4331 (wl's power save writes 0x7f64 to MDIO block 1's MGMT1).
	 */
	test_host_core.bus = &test_bus;
	test_host_core.id.id = board->host_core ?: BCMN_CORE_PCIE2;
	test_host_core.id.rev = board->host_core ? 15 : 1;

		/*
		 * The SROM. Without it b43 runs on a zeroed board: femctrl=0 -- which
		 * the driver reports -- rxchain=0, coremask=0, subband5gver=0 and
		 * pa5ga all zero, every board-dependent value silently wrong. The
		 * profile is shared with ../unit, which is the same board, and is
		 * chosen with B43_BOARD as there with argv.
		 *
		 * On hardware bcmn_sprom_extract_r11() fills it; here the profile
		 * stands in for it, with the same masks.
		 */
	if (board->nvram_prefix)
		bcm47xx_fill_sprom(&test_bus.sprom, board->nvram_prefix, false);
	else
		board_profile_to_sprom(board, &test_bus.sprom);

	test_core.bus = &test_bus;
	test_core.id.id = TEST_CORE_ID;
	test_core.id.rev = core_rev;
	test_core.id.manuf = BCMN_MANUF_BCM;
	test_core.core_index = 0;
	test_core.dma_dev = &test_dma_dev;
}

struct bcmn_device *bcmn_find_core_unit(struct bcmn_bus *bus, u16 coreid,
					u8 unit)
{
	if (unit)
		return NULL;
	switch (coreid) {
	case BCMN_CORE_CHIPCOMMON:
		return &test_cc_core;
	case BCMN_CORE_PCIE:
	case BCMN_CORE_PCIE2:
		return coreid == test_host_core.id.id ? &test_host_core : NULL;
	case TEST_CORE_ID:
		return &test_core;
	}
	return NULL;
}

/*
 * What bcmn_bus_register() runs before it registers the cores' drivers: the
 * ChipCommon and PMU init, then the host interface's workarounds. With the capability
 * words of the profile at zero the PMU stays out, as on a board the suite
 * has no reset values for.
 */
static void test_bcma_bus_init(void)
{
	bcmn_core_chipcommon_early_init(&test_bus.drv_cc);
	bcmn_core_chipcommon_init(&test_bus.drv_cc);
	/*
	 * Without the capability words the PMU init does not run, and leaves
	 * no PMU core for the regulator and PLL accesses b43 makes later; they
	 * go to the ChipCommon, where the PMU of these chips sits.
	 */
	if (!test_bus.drv_cc.pmu.core)
		test_bus.drv_cc.pmu.core = &test_cc_core;
	if (test_host_core.id.id == BCMN_CORE_PCIE) {
		test_bus.drv_pci[0].core = &test_host_core;
		bcmn_core_pci_init(&test_bus.drv_pci[0]);
	} else {
		test_bus.drv_pcie2.core = &test_host_core;
		bcmn_core_pcie2_init(&test_bus.drv_pcie2);
	}
}

/*
 * b43's ieee80211_ops and hw, captured by subsystem_stub.c's
 * ieee80211_alloc_hw_nm(). The channel is not injected from here: `dev` is
 * born inside the probe, out of the harness's reach, and register_hw picks
 * the channel (subsystem_stub.c).
 */
extern const struct ieee80211_ops *b43_test_hw_ops;
extern struct ieee80211_hw *b43_test_hw;

/* The kernel and the microcode, from the stubs. */
void b43_test_run_until(unsigned long until);
void b43_test_irq(void);
void b43_test_raise_irq(u32 reason);
void b43_test_raise_dma0(u32 reason);

/*
 * The noise sample ready, a bit of B43_MMIO_GEN_IRQ_REASON: it is the
 * hardware's register, the same value as b43.h's.
 */
#define TEST_IRQ_NOISESAMPLE_OK	0x00040000

static struct ieee80211_vif *test_vif;

static void test_config(u32 changed)
{
	b43_test_hw_ops->config(b43_test_hw, -1, changed);
}

static void test_bss(u64 changed)
{
	b43_test_hw_ops->bss_info_changed(b43_test_hw, test_vif,
					  &test_vif->bss_conf, changed);
}

/*
 * What mac80211 does at the ifup of an AP interface, after drv_start():
 * drv_add_interface(), ieee80211_hw_config(~0) and the AP's default WMM
 * parameters, one conf_tx per queue.
 */
static void test_ifup(void)
{
	static const struct ieee80211_tx_queue_params wmm[IEEE80211_NUM_ACS] = {
		[IEEE80211_AC_VO] = { .cw_min = 3,  .cw_max = 7,    .aifs = 1,
				      .txop = 47 },
		[IEEE80211_AC_VI] = { .cw_min = 7,  .cw_max = 15,   .aifs = 1,
				      .txop = 94 },
		[IEEE80211_AC_BE] = { .cw_min = 15, .cw_max = 63,   .aifs = 3 },
		[IEEE80211_AC_BK] = { .cw_min = 15, .cw_max = 1023, .aifs = 7 },
	};
	unsigned int ac;

	test_vif = kzalloc(sizeof(*test_vif) + b43_test_hw->vif_data_size,
			   GFP_KERNEL);
	test_vif->type = NL80211_IFTYPE_AP;
	memcpy(test_vif->addr, b43_test_hw->wiphy->perm_addr, ETH_ALEN);
	b43_test_hw_ops->add_interface(b43_test_hw, test_vif);
	test_config(~0);
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		b43_test_hw_ops->conf_tx(b43_test_hw, test_vif, 0, ac, &wmm[ac]);
}

/*
 * start_ap: hostapd gives the BSS and mac80211 passes it to the driver,
 * beacon included, in a single bss_info_changed. DTIM 3 and the basic set
 * {6, 12, 24} are the capture's.
 */
static void test_start_ap(void)
{
	struct ieee80211_bss_conf *bss = &test_vif->bss_conf;

	bss->bssid = test_vif->addr;
	bss->beacon_int = 100;
	bss->dtim_period = 3;
	bss->basic_rates = BIT(0) | BIT(2) | BIT(4);
	bss->use_short_slot = true;
	bss->enable_beacon = true;
	test_bss(BSS_CHANGED_BSSID | BSS_CHANGED_BEACON_INT |
		 BSS_CHANGED_BASIC_RATES | BSS_CHANGED_ERP_SLOT |
		 BSS_CHANGED_BEACON | BSS_CHANGED_BEACON_ENABLED);
}

/*
 * The channel availability check as mac80211 does it for a driver without
 * channel contexts: the tune with the radar on, then once the check is over
 * the release -- 20 MHz without HT on the same primary, radar off -- and
 * start_ap's tune, with the radar on again.
 */
static void test_cac_start(void)
{
	b43_test_hw->conf.radar_enabled = true;
	test_config(IEEE80211_CONF_CHANGE_CHANNEL);
}

static void test_cac_end(void)
{
	struct cfg80211_chan_def def = b43_test_hw->conf.chandef;

	b43_test_hw->conf.radar_enabled = false;
	b43_test_hw->conf.chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
	b43_test_hw->conf.chandef.center_freq1 = def.chan->center_freq;
	test_config(IEEE80211_CONF_CHANGE_CHANNEL);
	b43_test_hw->conf.chandef = def;
	b43_test_hw->conf.radar_enabled = true;
	test_config(IEEE80211_CONF_CHANGE_CHANNEL);
	test_start_ap();
}

/*
 * The environment after the bring-up, from the capture's timeline. The
 * driver's clock is the jiffies, which here advance only when the timeline
 * says so: each event's instant is carried into jiffies and before the event
 * the expired delayed works fire -- the periodic work, the radar poll. So the
 * vendor's watchdog rounds are not delivered, b43's periodic work does them;
 * they serve to scale the clock: the vendor declares one second and the
 * capture measures 1.004, and the first round falls one second after the
 * start, like b43's.
 *
 *   NOISE   the sample ready: B43_IRQ_NOISESAMPLE_OK from the hard handler
 *   IRQ     a microcode interrupt as the capture saw it: the reason in
 *           GEN_IRQ_REASON and the state of DMA channel 0, RX_DONE when
 *           there is a frame. What b43 does with it -- the ACK, the masks,
 *           the read of the receive ring -- is its own. Those that fell
 *           during the vendor's bring-up all arrive together at the start,
 *           because here the bring-up is a single call and the clock starts
 *           after it.
 *   TPL     the beacon changed: bss_info_changed(BSS_CHANGED_BEACON)
 *   BSS_UP  the check closed: the channel release and start_ap
 *   WD, POLL  the driver's timers, which fire by themselves
 */
static void test_environment(bool cac)
{
	long long t0, t1, t;
	char kind[16];
	u32 a0, a1;
	int n;

	if (!b43_test_timeline_wd(&t0, &t1, &n) || n < 2) {
		if (cac)
			test_cac_end();
		return;
	}
	while (b43_test_timeline_next(&t, kind, sizeof(kind), &a0, &a1)) {
		long long rel = t - t0;
		unsigned long j = HZ + (unsigned long)
			(rel < 0 ? 0 : rel * (n - 1) * HZ / (t1 - t0));

		b43_test_run_until(j);
		/* With the virtual chip the ucode raises the interrupts. */
		if (b43_chip_active() &&
		    (!strcmp(kind, "NOISE") || !strcmp(kind, "IRQ")))
			continue;
		if (!strcmp(kind, "NOISE")) {
			b43_test_raise_irq(TEST_IRQ_NOISESAMPLE_OK);
			b43_test_irq();
		} else if (!strcmp(kind, "IRQ")) {
			b43_test_raise_irq(a0);
			b43_test_raise_dma0(a1);
			b43_test_irq();
		} else if (!strcmp(kind, "TPL")) {
			test_bss(BSS_CHANGED_BEACON);
		} else if (!strcmp(kind, "BSS_UP")) {
			if (cac) {
				cac = false;
				test_cac_end();
			}
		}
	}
}

/*
 * B43_CYCLE2=N: a second `ifconfig up` after the first down, on channel N of
 * the same band, with the AP started again and 5 s of its work: what a
 * reconfiguration does, for what it can break (the state kept across a
 * down, the restore on another channel). The oracle has run out by then,
 * so the reads are the capture's last; it is a run for crashes and
 * warnings, not a score.
 */
static void test_second_cycle(void)
{
	long want = b43_test_env_long("B43_CYCLE2", 0);
	struct ieee80211_supported_band *sb;
	int i, up;

	if (!want)
		return;
	sb = b43_test_hw->wiphy->bands[b43_test_hw->conf.chandef.chan->band];
	for (i = 0; i < sb->n_channels; i++)
		if (sb->channels[i].hw_value == want)
			break;
	if (i == sb->n_channels) {
		b43_trace_note("B43_CYCLE2=%d: no such channel\n", (int)want);
		return;
	}
	b43_test_hw->conf.chandef.chan = &sb->channels[i];
	b43_test_hw->conf.chandef.center_freq1 = sb->channels[i].center_freq +
		(b43_test_hw->conf.chandef.width == NL80211_CHAN_WIDTH_40 ?
		 10 : 0);
	up = b43_test_hw_ops->start(b43_test_hw);
	b43_trace_note("cycle 2 start: %d\n", up);
	if (up)
		return;
	test_ifup();
	test_start_ap();
	b43_test_run_until(jiffies + 5 * HZ);
	b43_test_hw_ops->remove_interface(b43_test_hw, test_vif);
	if (b43_test_hw_ops->stop)
		b43_test_hw_ops->stop(b43_test_hw, false);
}

int main(void)
{
	int err;

	build_core();
	test_bcma_bus_init();

	err = init_module();
	if (err) {
		b43_trace_note("init_module: %d\n", err);
		return 1;
	}
	if (!b43_test_probe) {
		b43_trace_note("b43 registered no bcma probe; "
			       "built without CONFIG_B43_NEXT_BCMA?%d\n", 0);
		return 1;
	}

	err = b43_test_probe(&test_core);
	b43_trace_note("probe: %d\n", err);

		/*
		 * The bring-up. The probe does only the attach: what emits the
		 * segment's ~29k ops is the `ifconfig up` path, which in the kernel
		 * comes in from mac80211 with hw->ops->start(). Calling it here is the
		 * right hook, and does not touch b43: the stub's
		 * ieee80211_alloc_hw_nm() captures the vtable, as
		 * __bcmn_driver_register captures the probe.
		 */
	/* A registration cfg80211 would refuse leaves no channel to start on */
	if (!err && b43_test_hw && !b43_test_hw->conf.chandef.chan)
		b43_trace_note("no channel registered, no start: %d\n", -EINVAL);
	else if (!err && b43_test_hw_ops && b43_test_hw_ops->start) {
		int up = b43_test_hw_ops->start(b43_test_hw);

		b43_trace_note("start: %d\n", up);
		if (!up) {
			bool cac = b43_test_hw->conf.chandef.chan->flags &
				   IEEE80211_CHAN_RADAR;

			test_ifup();
			if (cac)
				test_cac_start();
			else
				test_start_ap();
			test_environment(cac);
			b43_test_hw_ops->remove_interface(b43_test_hw, test_vif);
			if (b43_test_hw_ops->stop)
				b43_test_hw_ops->stop(b43_test_hw, false);
			test_second_cycle();
		}
	}

	/*
	 * The return value is NOT the test's result: the result is the trace. A
	 * probe that returns 0 and emits no op is a failure, and one that
	 * returns an error after emitting the bring-up is information -- usually
	 * the point where a stub is no longer enough.
	 *
	 * The remove only if the probe succeeded: otherwise there is no drvdata
	 * to free and the remove would read a pointer nobody wrote.
	 */
	if (!err && b43_test_remove)
		b43_test_remove(&test_core);
	cleanup_module();
	return 0;
}
