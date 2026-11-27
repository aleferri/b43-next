/* SPDX-License-Identifier: GPL-2.0
 * Test driver: build a minimal mock b43_wldev matching the D6220 board
 * (BCM4352 2x2, r2069 rev 4, coremask=0x3) and invoke one of the
 * scratch entry points. Emitted trace on stdout should match the
 * corresponding wl-diag capture after normalisation.
 *
 * Usage:
 *   ./ac_trace [flow] [board]
 *     flow  = full (default) | up | down | switch_channel | periodic |
 *             op_init | rfkill | crsmin | rxiq_est_debug | rxiq_comp
 *     board = d6220 (default) | agcombo | dsl | tg789 | archer | macbook |
 *             r7000-2g | r7000-5g
 *
 * `full` is the broadest flow, ~28k HW ops on d6220 ch36: the analog preamble,
 * rfkill, op_init, then everything run_switch_channel() drives -- the channel
 * setup of b43_phy_ac_op_switch_channel(), the core's BSS configuration, the
 * second half of the setup, the post-channel calibrations and the bss-up
 * burst. `switch_channel` alone models a runtime channel change and the others
 * exercise narrower slices. The AC-PHY files of the driver link with the
 * harness's RX-IQ estimator, rxiqcal_phy_ac.c; see the Makefile SRCS list.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "b43.h"
#include "phy_common.h"
#include "phy_ac.h"
#include "radio_2069.h"
#include "calstat.h"
#include "rxiqcal_phy_ac.h"
#include "test_harness.h"
#include "readplan_0270.h"

extern enum nl80211_band b43_test_band;

#include "../board_profile.h"
#include "leds.h"

/*
 * The calibration statistics, with AC_CALSTAT set: calstat.c keeps them as in
 * the driver, and they go to stderr at the exit, a slot a line in the order
 * they were reported.
 */
static struct b43_calstat g_calstat;

static void calstat_print(void)
{
	unsigned int entry, age;
	char line[1024];
	u32 seq = 0;

	while (b43_cal_next(&g_calstat, &seq, &entry, &age)) {
		b43_cal_format(&g_calstat, entry, age, 0, line, sizeof(line));
		fprintf(stderr, "calstat: %s\n", line);
	}
}

static struct b43_phy_ac       g_ac;
/* The beacon template reloads: how many done, and how many at start_ap. */
static unsigned int g_beacon_reloads;
static unsigned int g_beacon_reload_pre;
/* The timeline has a reload inside the watchdog turn being delivered. */
static bool g_wd_reload;
/* The pending check, decided before g_wldev is built. */
static bool g_cac_pending;
static struct b43_wl           g_wl;
static struct ieee80211_hw     g_hw;
static struct ieee80211_channel g_chan;
static struct ssbn_sprom        g_sprom;
static struct bcmn_bus         g_bcma_bus;
static struct bcmn_device      g_bcma_dev;
static struct b43_bus_dev      g_bus_dev;
static struct b43_wldev        g_wldev;

/* The header geometries are in xmit.c, outside the link. */
const struct b43_fw_hdr b43_hdr_351, b43_hdr_410, b43_hdr_598,
			b43_ac_hdr_784;

/*
 * Bit 0 of PHY 0x0270 is the RX-IQ measurement's start flag and the hardware
 * clears it on completion: the driver rereads the register while it stays
 * high, so the value read decides how many ops it emits. Without
 * AC_READ_ORACLE that value has to come from somewhere, and it comes from the
 * reads the vendor tracer recorded on the board in question.
 *
 * The choice is by board and phase, the same two axes the counts depend on:
 * the d6220 captures show 44/14/42/43 polls in the attach's v2 blocks against
 * 29/27/22/41 of the down->up. Where only one capture with the RETVALs exists
 * it serves both phases, a declared approximation: for agcombo there is only
 * a rescan (later phase) and for the DSL only down->ups, the cold attach
 * cannot be captured.
 */
static void plan_rxiq_poll(const char *board, bool first_init)
{
	const u16 *plan;
	int n;

	if (!strcmp(board, "agcombo")) {
		plan = readplan_0270_agcombo;
		n = (int)ARRAY_SIZE(readplan_0270_agcombo);
	} else if (!strcmp(board, "dsl")) {
		plan = readplan_0270_dsl;
		n = (int)ARRAY_SIZE(readplan_0270_dsl);
	} else if (first_init) {
		plan = readplan_0270_d6220_first;
		n = (int)ARRAY_SIZE(readplan_0270_d6220_first);
	} else {
		plan = readplan_0270_d6220_next;
		n = (int)ARRAY_SIZE(readplan_0270_d6220_next);
	}
	b43_test_plan_phy_reads(0x0270, plan, n);
}

/*
 * Chain @c's noise ring full of @idx, and the CRS block programmed from it:
 * the ladder index in force on the chain is @idx and the hardware carries it.
 */
static void seed_crs_chain(unsigned int c, u8 idx)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(g_ac.crs_ring[0]); i++)
		g_ac.crs_ring[c][i] = idx;
	g_ac.crs_prog[c] = idx;
}

/* The same on every chain, so the 0x0910 bank carries no offset. */
static void seed_crs_rings(u8 idx)
{
	unsigned int c;

	for (c = 0; c < B43_PHY_AC_MAX_CORES; c++)
		seed_crs_chain(c, idx);
	g_ac.crs_ring_head = 0;
	g_ac.crs_ring_len = ARRAY_SIZE(g_ac.crs_ring[0]);
}

static int timeline_has(const char *kind);

/* The mounted profile, for the few points that model the core. */

static const struct board_profile *g_profile;


/*
 * On which of the four conf_tx passes the stack above republishes the
 * beacon. Not structural: on cold01 it happens only on the first, on cold05
 * on all four, and the same tree emits the four passes in both cases. It is
 * hostapd pushing a beacon while the tracer runs, so it goes like
 * AC_PROBE_TICKS and AC_BEACON_RELOADS -- out of the capture and in from the
 * caller.
 *
 * The passes are in the order of edcf_queues[]: best effort, background,
 * video, voice, the blocks that end up at 0x027e, 0x025e, 0x029e and 0x02be.
 * Bit n says there is a reload after pass n.
 */
static unsigned int g_edcf_reload_mask = 0x1;

/*
 * Length of the capture's SSID, in bytes. A single input: cell 0x001e, the
 * BTL cells and the PLCPs of the eight rates depend on it. The recaptured
 * sweep uses `test-ap5`, eight characters; a knob because the next capture
 * may use another.
 */
static unsigned int g_ssid_len = 8;


static void mount_board(const struct board_profile *p)
{
	g_profile = p;
	memset(&g_ac, 0, sizeof(g_ac));
	g_ac.num_cores  = p->num_cores;
	g_ac.coremask   = p->coremask;
	/*
	 * dacbuf_cap: the real d6220 chip has RCCAL_G=0x0009 → dacbuf_cap=0.
	 * See the comment in analog_on_reset's dacbuf loop.
	 */
	/*
	 * dacbuf_cap: rccal computes it in op_init as (RCCAL_G & 0x03e0)>>5
	 * from the post-apply read. switch_channel does not re-run rccal, so
	 * replicate it here from the per-board rccal_g with the same formula.
	 */
	g_ac.dacbuf_cap = (u8)((p->rccal_g & 0x03e0) >> 5);
	/*
	 * lpf_cap0/1: in the real driver rccal computes it in op_init as
	 * cap = ((F-E)*193)>>8 from the R2069_RCCAL_E/F reads. switch_channel
	 * does not re-run rccal, so replicate that precondition here from the
	 * per-board E/F, using the same formula, instead of forcing a value.
	 */
	{
		u8 cap = (u8)(((p->rccal_f - p->rccal_e) * 193) >> 8);
		g_ac.lpf_cap0 = cap;
		g_ac.lpf_cap1 = cap;
	}

	board_profile_to_sprom(p, &g_sprom);
	/*
	 * phycal_tempdelta as the instance ran with, when it is not the NVRAM's:
	 * `wl phycal_tempdelta` after the insmod. It decides whether a full
	 * calibration opens with a temperature reading; see
	 * b43_phy_ac_cal_tempsense().
	 */
	{
		const char *e = getenv("AC_TEMPDELTA");

		if (e && *e)
			g_sprom.phycal_tempdelta = (u8)strtoul(e, NULL, 0);
	}

	g_bcma_dev.bus = &g_bcma_bus;
	g_bus_dev.bus_type = B43_BUS_BCMA;
	g_bus_dev.chip_id  = p->chip_id;
	g_bus_dev.bus_sprom = &g_sprom;
	g_bus_dev.bdev     = &g_bcma_dev;

	/*
	 * ch36 unless AC_CHANNEL says otherwise. Driving another channel only
	 * makes sense against a capture of that channel supplied through
	 * AC_READ_ORACLE. Channels 1 to 14 are 2.4 GHz.
	 */
	g_chan.band = NL80211_BAND_5GHZ;
	g_chan.hw_value = 36;
	{
		const char *e = getenv("AC_CHANNEL");

		if (e) {
			unsigned long v = strtoul(e, NULL, 10);

			if (v < 1 || v > 200) {
				fprintf(stderr, "AC_CHANNEL=%s out of range\n", e);
				exit(1);
			}
			g_chan.hw_value = (u16)v;
		}
	}
	g_chan.center_freq = 5000 + 5 * g_chan.hw_value;
	if (g_chan.hw_value <= 14) {
		g_chan.band = NL80211_BAND_2GHZ;
		g_chan.center_freq = g_chan.hw_value == 14 ?
			2484 : 2407 + 5 * g_chan.hw_value;
		b43_test_band = NL80211_BAND_2GHZ;
	}

	/*
	 * Radar-detection requirement, as cfg80211 would set it from the
	 * regulatory domain: U-NII-1 (5150-5250) and U-NII-3 (5725-5850) carry
	 * no radar duty, U-NII-2A (5250-5350) and U-NII-2C do.
	 *
	 * Where U-NII-2C ends is the one edge the spec and this device do not
	 * agree on, and here the captures arbitrate instead of the spec. The
	 * stock driver polls the radar detector on a channel that carries the
	 * duty and not on one that does not, so the POLL events of
	 * reverse-tools/timeline.py read the answer off each segment; run over
	 * all forty-three of the cold sweep they draw the boundary at ch140, not
	 * at 5725:
	 *
	 *   ch36-48       no poll        ch52-140      poll
	 *   ch144-165     no poll
	 *
	 * and the same on the 40 and 80 MHz blocks, which follow their low
	 * channel: 140/40 spans 5690-5730 and polls, 149/40 does not. So 5720
	 * -- ch144, inside U-NII-2C by the spec -- carries no duty on this
	 * board. Marking it would suppress the calibrations the vendor runs
	 * there, which is most of a 29k-operation attach.
	 *
	 * This is the regulatory domain the d6220 captures were taken under,
	 * not a universal truth: the tg789vac's domain does put ch144 under
	 * the duty, and its ch144 segment polls the detector 166 times. So
	 * where a timeline is given, its POLL events are the answer and the
	 * frequency rule is the fallback for the flows that run without one;
	 * on hardware it is cfg80211 that says so.
	 */
	{
		int polls = timeline_has("POLL");

		if (polls > 0 ||
		    (polls < 0 &&
		     ((g_chan.center_freq > 5250 && g_chan.center_freq <= 5350) ||
		      (g_chan.center_freq > 5470 && g_chan.center_freq <= 5700))))
			g_chan.flags |= IEEE80211_CHAN_RADAR;
	}

	/*
	 * Whether a channel availability check is outstanding. On hardware it
	 * is mac80211 asking for radar detection when it tunes the channel --
	 * hw->conf.radar_enabled -- and hostapd asks for it on every AP start
	 * on a channel with the duty. The vendor does the same on every `wl up`:
	 * the hot up on ch52 arms the detector, polls it 414 times and brings
	 * the BSS up 60 s later exactly like the cold attach does. So the
	 * default is the duty itself, cold or hot.
	 *
	 * AC_DFS_CAC_DONE overrides it, for a channel whose check some earlier
	 * owner had already passed and cfg80211 still holds as available.
	 */
	{
		const char *e = getenv("AC_DFS_CAC_DONE");

		g_cac_pending = e ? (strtoul(e, NULL, 0) == 0)
				  : !!(g_chan.flags & IEEE80211_CHAN_RADAR);
	}

	/*
	 * Regulatory ceiling for the channel, in dBm, as cfg80211 would supply
	 * it. The captures were taken on a system whose ceiling does not bind
	 * -- ch100 receives 86 quarter-dBm, above the 84 a 21 dBm limit would
	 * allow -- so the default here is permissive and AC_MAX_POWER lowers it
	 * to exercise the clamp.
	 */
	{
		const char *e = getenv("AC_MAX_POWER");
		int dflt = e ? (int)strtol(e, NULL, 10) : 30;

		g_chan.max_power = dflt;
		b43_test_reg_init(dflt, getenv("AC_MAX_POWER_MAP"));
	}

	/*
	 * CRS minimum-power state carried in from the previous cycle. The sweep
	 * is a continuous chain of channel changes, so running one segment on
	 * its own needs the state the chain would have left: AC_CRS_INDEX and
	 * AC_CRS_SUBBAND are the ladder entry, seeded into every chain's ring,
	 * and the sub-band in force. Without AC_CRS_INDEX the rings start
	 * empty, and the default sub-band of 0xff forces the reset to the
	 * floor: both are what a cold start does.
	 */
	{
		const char *e = getenv("AC_CRS_INDEX");

		if (e)
			seed_crs_rings((u8)strtoul(e, NULL, 0));
		e = getenv("AC_CRS_SUBBAND");
		g_ac.crs_subband = e ? (u8)strtoul(e, NULL, 0) : 0xff;
	}
	g_hw.conf.chandef.chan  = &g_chan;

	/*
	 * AC_BW selects the operating width: 20, 40 or 80. The centre of a
	 * bonded channel sits above the primary one by half the bonding span,
	 * so +10 MHz at 40 and +30 at 80, which is what the sweep's segment
	 * names imply -- ch36-bw40 is the 36/40 pair centred on 5190, and
	 * ch36-bw80 is 36 to 48 centred on 5210.
	 */
	{
		const char *e = getenv("AC_BW");
		unsigned long bw = e ? strtoul(e, NULL, 10) : 20;

		switch (bw) {
		case 20:
			g_hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
			g_hw.conf.chandef.center_freq1 = g_chan.center_freq;
			break;
		case 40:
			g_hw.conf.chandef.width = NL80211_CHAN_WIDTH_40;
			g_hw.conf.chandef.center_freq1 = g_chan.center_freq + 10;
			break;
		case 80:
			g_hw.conf.chandef.width = NL80211_CHAN_WIDTH_80;
			g_hw.conf.chandef.center_freq1 = g_chan.center_freq + 30;
			break;
		default:
			fprintf(stderr, "AC_BW=%s: expected 20, 40 or 80\n", e);
			exit(1);
		}
		/*
		 * AC_CENTER, in MHz, puts the block elsewhere around the
		 * primary, for a primary other than the lowest channel.
		 */
		e = getenv("AC_CENTER");
		if (e)
			g_hw.conf.chandef.center_freq1 =
				(u32)strtoul(e, NULL, 10);
	}
	g_hw.wiphy = NULL;
	g_wl.hw = &g_hw;

	memset(&g_wldev, 0, sizeof(g_wldev));
	g_wldev.dev = &g_bus_dev;
	g_wldev.wl  = &g_wl;
	{
		const char *e = getenv("AC_CALSTAT");

		if (e && *e && atoi(e)) {
			b43_calstat_setup(&g_calstat, &g_wldev,
					  b43_phyops_ac.cal_entries,
					  b43_phyops_ac.nr_cal_entries,
					  calloc(b43_phyops_ac.nr_cal_entries,
						 sizeof(struct b43_cal_ring)));
			b43_calstat_start(&g_calstat, calloc(1, g_calstat.size));
			g_wldev.calstat = &g_calstat;
			atexit(calstat_print);
		}
	}
	g_wldev.phy.type       = B43_PHYTYPE_AC;
	g_wldev.phy.rev        = p->phy_rev;
	g_wldev.fw.layout      = *b43_fw_layout_get(
					b43_fw_family_find(42, B43_PHYTYPE_AC),
					p->ucode ? p->ucode : 928);
	g_wldev.phy.radio_ver  = p->radio_ver;
	g_wldev.phy.radio_rev  = p->radio_rev;
	g_wldev.phy.dacbuf_cap = g_ac.dacbuf_cap;
	g_wldev.phy.lpf_cap    = g_ac.lpf_cap0;
	g_wldev.phy.ac         = &g_ac;
	/* As b43_setup_chains() sets them: the board's chains, its coremask. */
	g_wldev.rxchain        = g_ac.coremask;
	g_wldev.txchain        = g_ac.coremask;
	g_wldev.cac_pending    = g_cac_pending;
		/*
		 * Like b43_phy_init(): the PHY looks at phy.chandef, not inside
		 * hw->conf. AFTER the memset of g_wldev above, or it is lost.
		 */
	g_wldev.phy.chandef    = &g_hw.conf.chandef;
	/*
	 * The width the MAC was last told about, the width field of the last
	 * chanspec, which the chanspec write compares against: AC_MAC_WIDTH
	 * seeds it (gates.sh passes 0 on a segment that carries MAC.BW, the
	 * segment's own width otherwise), so a segment run on its own does not
	 * repeat a write the chain made.
	 */
	{
		const char *mw = getenv("AC_MAC_WIDTH");
		enum nl80211_chan_width w = mw ? (enum nl80211_chan_width)
					    strtoul(mw, NULL, 0)
					  : g_hw.conf.chandef.width;

		if (w == NL80211_CHAN_WIDTH_80)
			g_ac.chanspec = B43_PHY_AC_CHANSPEC_BW80;
		else if (w == NL80211_CHAN_WIDTH_40)
			g_ac.chanspec = B43_PHY_AC_CHANSPEC_BW40;
		else if (w)
			g_ac.chanspec = B43_PHY_AC_CHANSPEC_BW20;
	}
		/*
		 * First bring-up by default: the phase the attach captures witness.
		 * AC_FIRST_INIT=0 selects the later bring-up, to compare with the
		 * down->up captures.
		 */
	{
		const char *e = getenv("AC_FIRST_INIT");
		g_wldev.phy.do_full_init = !(e && !strcmp(e, "0"));
	}

	/*
	 * Channel of the previous calibration, which in a live driver survives
	 * from one cycle to the next but which a single-flow run has to be
	 * told. It selects the irregular first probe group; see
	 * b43_phy_ac_cal_restore.
	 *
	 * The default matches the flow: a first bring-up has no previous
	 * calibration, while a later cycle is by default a down-and-up on the
	 * channel it was already on, which is what the down->up captures are.
	 * AC_LAST_CAL_CHANNEL overrides it, and is what a warm cycle following
	 * a channel change needs -- the odd-numbered sweep segments.
	 */
	{
		const char *e = getenv("AC_LAST_CAL_CHANNEL");

		if (e)
			g_ac.last_cal_channel = (u16)strtoul(e, NULL, 10);
		else if (!g_wldev.phy.do_full_init)
			g_ac.last_cal_channel = g_chan.hw_value;
	}

		/*
		 * The beacon template reloads the vendor puts inside the bring-up's
		 * tail, before the first watchdog round: they are the stack's and the
		 * count comes from the capture (reverse-tools/beacon_reloads.py, the
		 * field before the colon). The core emits them at start_ap, see
		 * run_switch_channel(); the later ones are timeline events.
		 */
	{
		const char *e = getenv("AC_BEACON_RELOADS");

		if (e)
			g_beacon_reload_pre = (unsigned int)strtoul(e, NULL, 10);
	}

		/*
		 * The watchdog's state on entry: the counter of the ten-round period,
		 * which in the driver runs from the bring-up and which a hot segment
		 * catches halfway. reverse-tools/timeline.py reads it from the
		 * capture's first tempsense. Without it, zero: the tempsense on the
		 * ninth round, which is the cold attach.
		 */
	{
		const char *e = getenv("AC_WD_PHASE");

		if (e && *e)
			g_ac.wd_turns = (u16)strtoul(e, NULL, 10);
	}

		/*
		 * The shape of the first round after the bring-up: full instead of the
		 * sweep alone. The capture tells it by the order of the four head
		 * cells, and reverse-tools/timeline.py passes it. See @wd_entry_turn.
		 */
	{
		const char *e = getenv("AC_WD_ENTRY_TURN");

		if (e && *e)
			g_ac.wd_entry_turn = strtoul(e, NULL, 10) != 0;
	}
	{
		const char *e = getenv("AC_SSID_LEN");

		if (e && *e)
			g_ssid_len = (unsigned int)strtoul(e, NULL, 0);
	}
	{
		const char *e = getenv("AC_EDCF_RELOADS");

		if (e)
			g_edcf_reload_mask = (unsigned int)strtoul(e, NULL, 0);
	}

	/* Preconditions the rxiqcal REQUIRE gates want to see. */
	g_ac.status_mask = B43_PHY_AC_STATE_RX_WAITED |
			   B43_PHY_AC_STATE_CLIP_ALL_DIS;
}

/*
 * Table-access lock probe. Every b43_actab_* prologue reads 0x019e to
 * observe the "table write in progress" bit before touching 0x000d..
 * 0x000f. In real HW the bit is momentarily set by the previous access
 * and reads back clear once the internal state machine has advanced.
 * We script a single "already clear" read; any subsequent poll after a
 * mid-sequence write is served the same value.
 */
static const u16 tblacc_0x019e[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static void register_rxiq_read_plans(void)
{
	b43_test_plan_phy_reads(0x019e, tblacc_0x019e,
				(int)(sizeof(tblacc_0x019e) / sizeof(u16)));
}

/*
 * Board-specific read seeds. The wl-diag capture logs reads as val=UNDEFINED,
 * so read-return values come from each board's reset defaults / NVRAM, not the
 * trace. Most radio POR defaults seeded in main() are r2069-rev4 properties
 * (identical on 0x4352 and 0x4360), but a few reads differ per chip and must be
 * overridden here so an agcombo run is not served d6220 read values. Called
 * after the shared seeds, so these win. Add agcombo values here as they are
 * derived from the agcombo dumps (RCCAL E/F 0x0414/0x0415 still fall back to
 * the shared d6220 seed — unverified for agcombo).
 */
static void register_board_read_plans(const struct board_profile *p)
{
	if (!strcmp(p->name, "agcombo")) {
		/* CLASSCTL 0x0140: BCM4360 powers this register with bit 0x0800
		 * set at reset (0x4352 leaves it clear). channel_switch_prep
		 * preserves the peeked bit: (cur & 0x0800) | 0x05f4 -> 0x0df4 on
		 * agcombo, 0x05f4 on d6220. Both agcombo captures confirm 0x0df4. */
		b43_test_mirror_phy_set(0x0140, 0x0800);
		/* Radio 0x0433 (core-2 0x0033 shadow): agcombo reads 0x0161 like
		 * cores 0/1, not the d6220 core-2 default 0x0160 seeded in main(). */
		b43_test_mirror_radio_set(0x0433, 0x0161);
		/* Core-2 shadows of seeds already present for cores 0/1 in
		 * main(): 0x041a pre-state bit 0x0004 (agcombo #60570 folds it
		 * into WR 0x0014) and 0x0521 AFE_CAL_CLK bit 0x2000 (agcombo
		 * #63946 RMW yields 0x2000/0x3000). */
		b43_test_mirror_radio_set(0x041a, 0x0004);
		b43_test_mirror_radio_set(0x0521, 0x2000);
	} else if (!strcmp(p->name, "r7000-2g") ||
		   !strcmp(p->name, "r7000-5g")) {
		/* CLASSCTL reads 0x0df7 on both radios (cold01 of cold-2g20 and
		 * of cold-5g20). The radio seeds are the other BCM4360 3x3
		 * boards': in these captures 0x041a and 0x0521 are modified
		 * before any read shows them. */
		b43_test_mirror_phy_set(0x0140, 0x0800);
		b43_test_mirror_radio_set(0x041a, 0x0004);
		b43_test_mirror_radio_set(0x0521, 0x2000);
	} else if (!strcmp(p->name, "tg789")) {
			/* First reads of tg789vac-v2 cold01-ch36-bw20: CLASSCTL 0x0df7,
			 * radio 0x041a 0x0004, radio 0x0521 0x2000. The agcombo's 0x0433
			 * does not hold here: it reads 0x6060 and then 0x4161. */
		b43_test_mirror_phy_set(0x0140, 0x0800);
		b43_test_mirror_radio_set(0x041a, 0x0004);
		b43_test_mirror_radio_set(0x0521, 0x2000);
	} else if (!strcmp(p->name, "dsl")) {
		/*
		 * DSL-3580L initial register state, read back from the wl6.30
		 * capture's RETVALs (router-data/dsl3580l). These replace the
		 * d6220-derived read-plan values so the port is exercised
		 * against what the DSL hardware really returns, not values
		 * tuned to make the d6220 trace match.
		 *
		 * 0x0140 CLASSCTL: the 4352 here comes up with bit 0x0800 SET
		 * (real read 0x0df4/0x0df7), unlike the D6220 -- so the "0x4352
		 * leaves it clear" assumption is a d6220 artifact, not a chip
		 * rule.
		 */
		b43_test_mirror_phy_set(0x0140, 0x0800);
		/* Radio 0x0033/0233/0433 background: real 0x6061/0x6061/0x6060
		 * (D6220 path yields the 0x40xx family). */
		b43_test_mirror_radio_set(0x0033, 0x6061);
		b43_test_mirror_radio_set(0x0233, 0x6061);
		b43_test_mirror_radio_set(0x0433, 0x6060);
		/* Radio 0x0045/0245/0445 background: real 0x703f/0x703f/0x7000. */
		b43_test_mirror_radio_set(0x0045, 0x703f);
		b43_test_mirror_radio_set(0x0245, 0x703f);
		b43_test_mirror_radio_set(0x0445, 0x7000);
	}
}

/*
 * Runnable segments. Each registers its own read plans and then calls its
 * op: plan_add resets the cursor when an address is registered again, so
 * registering per segment right before running it is also the right order
 * when the segments are chained.
 */
static void run_op_init(void)
{
	/*
	 * On the vendor attach-to-bss trace, num_cores is already
	 * cached from a prior fresh attach, so probe_cores hits its
	 * early-return path and emits no PHY.RD 0x000b. We keep the
	 * mount_board() defaults (num_cores=3, coremask=3) to mirror
	 * that state; a fresh-attach flow would clear them here to
	 * exercise probe_cores' PHY read.
	 */
	g_ac.status_mask = 0;	/* op_init has no REQUIRE gates */
	/*
	 * pre_init_frontend RAD 0x0X33 (#51692/51697/51702): nibble
	 * 0xf000 -> 0x4000 over HW background 0x0060 gives 0x4060.
	 */
	{
		static const u16 r33[] = { 0x4060 };
		u16 co;
		for (co = 0; co <= 0x400; co += 0x200)
			b43_test_plan_radio_reads(0x0033 + co, r33,
						  ARRAY_SIZE(r33));
	}
	int r = b43_phyops_ac.init(&g_wldev);
	fprintf(stderr, "test: op_init returned %d\n", r);
}

static void run_rfkill(void)
{
	/*
	 * software_rfkill(false): radio bring-up (2069 init/pwron/rccal),
	 * afe_lpf_stage, GPIO frontend, PA bias and the radio-ON front-end
	 * switch. Runs before op_init in the real driver; here in isolation.
	 */
	g_ac.status_mask = 0;

	/*
	 * rccal done-bit poll (R2069_RCCAL_STAT 0x0413, bit 4). Three
	 * passes; vendor ch36 polls 2, 6, 6 times before done (ep
	 * 32611-12, 32640-45, 32708-13). Done value = 0x0010 on the last
	 * read of each run.
	 */
	{
		static const u16 rccal_stat[] = {
			0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
			0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
			0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
		};
		b43_test_plan_radio_reads(0x0413, rccal_stat,
					  ARRAY_SIZE(rccal_stat));
	}

	/*
	 * 0x0407 bit 7 (0x0080) reads back clear after the prefregs write
	 * of 0x8382, so the set-bit1 RMW writes 0x8302 (vendor #51319-21).
	 * The write-mirror can't model the self-clear.
	 */
	{
		static const u16 rad_0407[] = { 0x8302 };
		b43_test_plan_radio_reads(0x0407, rad_0407,
					  ARRAY_SIZE(rad_0407));
	}
	/*
	 * 0x040c reads back 0x0200 (bit 9 set) at the epilogue bit-4
	 * toggle (vendor: mask ->0x0200, set ->0x0210); the bit is a HW
	 * default the write-mirror doesn't hold.
	 */
	{
		static const u16 rad_040c[] = { 0x0200, 0x0201 };
		b43_test_plan_radio_reads(0x040c, rad_040c,
					  ARRAY_SIZE(rad_040c));
	}
	/*
	 * EN2 / power-kick / RCCAL cfg+kick registers hold silicon
	 * background bits that the write-mirror (starting at 0)
	 * cannot reproduce. Seed each read with the vendor's post-RMW
	 * value: for a maskset read==WR yields WR, since the vendor WR
	 * already carries the set bits under the mask. Ordered per
	 * read; vendor down-to-bss-up #51262-.
	 */
	{
		static const u16 rad_08ed[] = { 0x4124, 0x4124, 0x4524 };
		static const u16 rad_040b[] = {
			0x0000, 0x0001,		/* kick clear, set */
			0x0168, 0x0168,		/* pon0/pon1 readback */
			0x0168,			/* final mask reads risen reg */
		};
		static const u16 rad_0410[] = {
			0x1f80, 0x1f80, 0x1f80, 0x1f81, 0x1f80,
			0x0f80, 0x0f90, 0x0f90, 0x0f91, 0x0f90,
			0x0f90, 0x0f88, 0x0f88, 0x0f89,
		};
		static const u16 rad_0411[] = {
			0x1c54, 0x1c55, 0x1c54, 0x7054, 0x7055,
			0x7054, 0x4054, 0x4055, 0x4054,
		};
		/* measured RC code read by apply_code (pass 1) then the
		 * dacbuf read (pass 2); low 5 bits = 0x09 (vendor #51517). */
		static const u16 rad_0416[] = { 0x0009, 0x0009 };
		b43_test_plan_radio_reads(0x08ed, rad_08ed,
					  ARRAY_SIZE(rad_08ed));
		b43_test_plan_radio_reads(0x040b, rad_040b,
					  ARRAY_SIZE(rad_040b));
		b43_test_plan_radio_reads(0x0410, rad_0410,
					  ARRAY_SIZE(rad_0410));
		b43_test_plan_radio_reads(0x0411, rad_0411,
					  ARRAY_SIZE(rad_0411));
		b43_test_plan_radio_reads(0x0416, rad_0416,
					  ARRAY_SIZE(rad_0416));
		/*
		 * afe_lpf_stage per-core (after rccal, #51609-): 0x0045
		 * set-0x0080 over HW background 0x3000; 0x0049 six clears
		 * over background 0x0030 (no clr49 touches bits 4:5, so the
		 * mirror carries it after the first seeded read).
		 */
		{
			static const u16 r45[] = { 0x3080 };
			static const u16 r49[] = { 0x0030 };
			u16 co;
			for (co = 0; co <= 0x400; co += 0x200) {
				b43_test_plan_radio_reads(0x0045 + co,
						r45, ARRAY_SIZE(r45));
				b43_test_plan_radio_reads(0x0049 + co,
						r49, ARRAY_SIZE(r49));
			}
		}
	}

	b43_phyops_ac.software_rfkill(&g_wldev, false);
	fprintf(stderr, "test: software_rfkill(false) done\n");
}

static void emit_core_bss_config(void);
static void emit_core_bss_config1(void);
static void emit_core_conf_tx_passes(void);
static void emit_core_cac_gate(bool open);
static void emit_core_beacon_reload(unsigned int which);

/*
 * Replay of the environment's events after the bring-up's tail, in the order
 * of their instants in the capture: reverse-tools/timeline.py extracts them
 * and says what they are. Each line is a callback that on hardware comes from
 * outside the PHY:
 *
 *   WD      the one-second periodic tick -> the PHY's pwork_1sec
 *   POLL    the radar's 150 ms work      -> b43_phy_ac_radar_poll()
 *   TPL     the core's bss_info_changed  -> the template reload, which is
 *                                           the core's and the harness emits;
 *                                           one following a WD falls between
 *                                           the two counter passes
 *   BSS_UP  the beacon starts after CAC  -> the core closes the check,
 *                                           reopens the AMT row and calls
 *                                           channel_calibrate with cac_done
 *   NOISE   the noise sample is ready    -> b43_phy_ac_noise_sample_done(),
 *                                           which on hardware comes from the
 *                                           core's tasklet
 *
 * How many there are and when they fall is not the driver's: it is how long
 * the capture lasted and what the stack did meanwhile. The driver only
 * decides what to do at each callback.
 */
/*
 * Whether the timeline carries an event of @kind: 1 or 0, and -1 when there
 * is no timeline to ask.
 */
static int timeline_has(const char *kind)
{
	const char *path = getenv("AC_TIMELINE");
	FILE *f;
	char line[128];
	int found = 0;

	if (!path || !*path)
		return -1;
	f = fopen(path, "r");
	if (!f)
		return -1;
	while (!found && fgets(line, sizeof(line), f)) {
		char k[16];

		if (sscanf(line, "%*f %*d %15s", k) == 1 && !strcmp(k, kind))
			found = 1;
	}
	fclose(f);
	return found;
}

static void run_timeline(void)
{
	const char *path = getenv("AC_TIMELINE");
	FILE *f;
	char line[128];

	if (!path || !*path)
		return;
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "test: cannot open timeline %s\n", path);
		exit(1);
	}

	while (fgets(line, sizeof(line), f)) {
		char kind[16];

		if (sscanf(line, "%*f %*d %15s", kind) != 1)
			continue;
		if (!strcmp(kind, "WD")) {
			long at = ftell(f);
			char next[128], nkind[16];

			/*
			 * A reload that follows the turn in the timeline falls
			 * between its two counter passes: the watchdog's site
			 * delivers it, see b43_phy_ac_core_site().
			 */
			g_wd_reload = fgets(next, sizeof(next), f) &&
				sscanf(next, "%*f %*d %15s", nkind) == 1 &&
				!strcmp(nkind, "TPL");
			if (!g_wd_reload)
				fseek(f, at, SEEK_SET);
			b43_phyops_ac.pwork_1sec(&g_wldev);
			if (g_wd_reload) {
				g_wd_reload = false;
				emit_core_beacon_reload(g_beacon_reloads++);
			}
		} else if (!strcmp(kind, "POLL"))
			b43_phy_ac_radar_poll(&g_wldev);
		else if (!strcmp(kind, "TPL"))
			emit_core_beacon_reload(g_beacon_reloads++);
		else if (!strcmp(kind, "BSS_UP")) {
			if (g_wldev.cac_pending) {
				g_wldev.cac_pending = false;
				emit_core_cac_gate(true);
				b43_mac_suspend(&g_wldev);
				b43_phyops_ac.channel_calibrate(&g_wldev, true);
			}
		} else if (!strcmp(kind, "NOISE"))
			b43_phy_ac_noise_sample_done(&g_wldev);
		else {
			fprintf(stderr, "test: unknown timeline event %s\n", kind);
			exit(1);
		}
	}
	fclose(f);
}

/*
 * Duplicates of b43_upload_card_macaddress(), b43_macfilter_set() and
 * b43_security_init() of b43/main.c: the rows at the top of the address match
 * table -- 0x3e the BSSID, 0x3f the station -- and the key rows.
 *
 * b43 writes them in the tail of b43_wireless_core_init(), after the first
 * switch_channel; the vendor inside the channel setup, at three points, and
 * the first pair before the BSSID is there. They are emitted at the PHY's
 * sites, see b43_phy_ac_core_site().
 *
 * The key rows are 56, 0x00-0x37, the ones the vendor clears on all segments;
 * b43_clear_keys() clears B43_NR_PAIRWISE_KEYS, 50, of them, and the
 * difference is the row count in docs/retrace-todo.md, "Key-table clearing".
 */
static void emit_core_top_row(bool self, u16 flags)
{
	b43_test_emit_addrm(self ? 0xffffffffu : 0xfffffffeu);
	b43_test_emit_amt(self ? 0x3f : 0x3e, flags);
}

#define B43_TEST_KEY_ROWS	0x38

/*
 * Corerev 42's key table as the vendor clears it before the rows: the KTP
 * material (0x087a, 0x10f4), 480 words, and the index block, 0x05e0-0x0666,
 * 68 words. b43_security_init() does it in the core init, for its entries.
 */
#define B43_TEST_KEY_MATERIAL	0x10f4
#define B43_TEST_KEY_MATERIAL_WORDS	480
#define B43_TEST_KEYIDX_BLOCK	0x05e0
#define B43_TEST_KEYIDX_WORDS	68

/*
 * The core's duplicates on the cold bring-up: the MACCONTROL of
 * b43_wireless_core_reset() (IHR | AWAKE, four resets in the capture), the
 * PSM's jump to 0 and its start in b43_upload_microcode(), and at the end
 * the GPOUT clear with the empty GPIO.CTL of b43_gpio_init(). The vendor
 * interleaves them with the host flags of the PHY's preamble; b43 does them
 * from main.c before b43_phy_init(), so the PHY does not emit them and they
 * are here.
 */
static void emit_core_reset_mctrl(void)
{
	b43_test_emit_mctrl(0, B43_MACCTL_IHR_ENABLED |
			   B43_MACCTL_AWAKE);
}

static void emit_core_ucode_load(void)
{
	b43_test_emit_mctrl(0, B43_MACCTL_IHR_ENABLED |
			   B43_MACCTL_AWAKE | B43_MACCTL_PSM_JMP0);
}

static void emit_core_ucode_start(void)
{
	b43_test_emit_mctrl(0, B43_MACCTL_IHR_ENABLED |
			   B43_MACCTL_AWAKE | B43_MACCTL_INFRA |
			   B43_MACCTL_PSM_RUN);
	b43_test_emit_mctrl((u32)~B43_MACCTL_GPOUTSMSK, 0);
	bcmn_chipco_gpio_control(&g_wldev.dev->bdev->bus->drv_cc, 0, 0);
}

/*
 * The MAC toggles the vendor does inside its own down. In b43 the PHY's down
 * runs with the MAC held suspended by b43_wireless_core_stop() and by
 * b43_software_rfkill()'s bracket, at count 2: the enable/suspend pairs there
 * do not touch the register, so they are emitted here raw, outside the
 * counter.
 */
static void emit_core_mac_toggle(bool on)
{
	b43_test_emit_mctrl(~(u32)B43_MACCTL_ENABLED,
			   on ? B43_MACCTL_ENABLED : 0);
}

/*
 * The tail of the vendor's down: MAC enable and suspend (wlc_bmac_down) and
 * the core reset's MACCONTROL that precedes the analog power-down. Between
 * the enable and the suspend the vendor turns off the two LEDs
 * (wlc_bmac_led, the core's), which stay in the comparison's perimeter.
 */
static void emit_core_down(void)
{
	emit_core_mac_toggle(true);
	emit_core_mac_toggle(false);
	emit_core_reset_mctrl();
}

/*
 * The core's operating mode, which the vendor rewrites inside the PHY's
 * phases: the beacon promiscuity (bit 20), the KEEP_* filters and, at down,
 * INFRA/DISCPMQ and AP. In b43 it is b43_adjust_opmode(); here the bit the
 * vendor touches at that point is emitted.
 */
static void emit_core_opmode(u32 mask, u32 set)
{
	b43_test_emit_mctrl(mask, set);
}

/*
 * The core's BSS configuration block before the TX power adjust: TBTT hold
 * raised and lowered (b43_time_lock/unlock around the beacon interval), AP
 * and INFRA (b43_adjust_opmode), PRETBTT at 2 (b43_set_pretbtt) and the
 * beacon promiscuity between an enable and a suspend. DISCPMQ goes down as
 * the vendor does; b43 keeps it up, the power management queue is out of
 * scope (docs/retrace-todo.md).
 */
static void emit_core_bss_mode(void)
{
	b43_test_emit_mctrl(~0x10000000u, 0x10000000);
	b43_test_emit_mctrl(~0x10000000u, 0);
	b43_test_emit_mctrl(~(u32)B43_MACCTL_AP, B43_MACCTL_AP);
	b43_test_emit_mctrl(~0x48020000u, B43_MACCTL_INFRA);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, B43_SHM_SH_PRETBTT, 2);
	b43_mac_enable(&g_wldev);
	b43_test_emit_mctrl(~0x00100000u, 0x00100000);
	b43_mac_suspend(&g_wldev);
}

void b43_phy_ac_core_site(struct b43_wldev *dev, enum b43_phy_ac_core_site site)
{
	u16 i;

	switch (site) {
	case B43_AC_SITE_CORE_DOWN:
		emit_core_down();
		break;
	case B43_AC_SITE_OPMODE_FILTERS:
		emit_core_opmode(~0x00100000u, 0);
		emit_core_opmode(~0x01c00000u, 0);
		break;
	case B43_AC_SITE_DOWN_OPMODE:
		emit_core_mac_toggle(true);
		emit_core_opmode(~0x48020000u, 0x40000000);
		emit_core_mac_toggle(false);
		emit_core_opmode(~(u32)B43_MACCTL_AP, 0);
		emit_core_mac_toggle(true);
		break;
	case B43_AC_SITE_DOWN_OPMODE_END:
		emit_core_mac_toggle(false);
		break;
	case B43_AC_SITE_CORE_RESET:
		emit_core_reset_mctrl();
		break;
	case B43_AC_SITE_UCODE_LOAD:
		emit_core_ucode_load();
		break;
	case B43_AC_SITE_UCODE_START:
		emit_core_ucode_start();
		break;
	case B43_AC_SITE_KEYS_CLEAR:
		for (i = 0; i < B43_TEST_KEY_MATERIAL_WORDS; i++)
			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_TEST_KEY_MATERIAL + 2 * i, 0);
		for (i = 0; i < B43_TEST_KEYIDX_WORDS; i++)
			b43_shm_write16(dev, B43_SHM_SHARED,
					B43_TEST_KEYIDX_BLOCK + 2 * i, 0);
		for (i = 0; i < B43_TEST_KEY_ROWS; i++) {
			b43_test_emit_addrm(i);
			b43_test_emit_amt(i, 0);
		}
		break;
	case B43_AC_SITE_MACFILTER_FIRST:
		emit_core_top_row(true, 0x8008);
		emit_core_top_row(false, 0);
		break;
	case B43_AC_SITE_MACFILTER:
		emit_core_top_row(false, 0x8002);
		emit_core_top_row(true, 0x8008);
		break;
	case B43_AC_SITE_CAC_CLOSE:
		if (dev->cac_pending)
			emit_core_cac_gate(false);
		break;
	case B43_AC_SITE_BEACON_START:
		for (i = 0; i < g_beacon_reload_pre; i++)
			emit_core_beacon_reload(g_beacon_reloads++);
		break;
	case B43_AC_SITE_BEACON_WD:
		if (g_wd_reload) {
			g_wd_reload = false;
			emit_core_beacon_reload(g_beacon_reloads++);
		}
		break;
	}
}

/*
 * The head of the vendor's down before it touches the PHY: the read of a
 * 32-bit counter (hi/lo/hi on 0x077c), the beacon promiscuity down and the
 * MAC suspend. In b43 it is b43_wireless_core_stop(), which suspends the MAC
 * before b43_phy_exit(); the counter and the mode bit are its own, not the
 * PHY's.
 */
static void emit_core_stop(void)
{
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077e);
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077c);
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077e);
	emit_core_opmode(~0x00100000u, 0);
	b43_mac_suspend(&g_wldev);
}

static void run_switch_channel(void)
{

		/*
		 * State on entry to switch_channel (post-op_init): MAC suspended,
		 * classifier/clip-detector state untouched (RX_ANY = 0,
		 * CLIP_ALL_DIS = 0). switch_channel sets RX_WAITED and
		 * CLIP_ALL_DIS via classifier()/clip_det() before the subroutines
		 * that REQUIRE that state.
		 *
		 * op_init is not rerun here: its trace is validated by the separate
		 * flow; running it back-to-back double-counts the register writes.
		 *
		 * Only the bits switch_channel expects clear on entry are cleared,
		 * not the whole status_mask, which in `full` already carries the
		 * phase latch set by op_init (B43_PHY_AC_STATE_FIRST_BRINGUP) and the
		 * PMU bracket's state.
		 */
	g_ac.status_mask &= B43_PHY_AC_STATE_FIRST_BRINGUP |
			    B43_PHY_AC_STATE_PMU_REQ;

		/*
		 * No read plan for 0x019e (table-write gate) in this flow: the mirror
		 * follows the gate's state. tbl_write_lock writes 0x0002, the sub-locks
		 * read 0x0002 back, and at the final tbl_write_unlock the saved value
		 * holds the previous lock's state (0 at the first lock, 0x0002 for the
		 * nested ones). A fixed 0x0000 plan would break this sequence.
		 */

		/*
		 * Table-7 cell pre-state for analog_on_reset's RMW. The
		 * b43_actab_read_bulk calls emit phy_read(0x000f) after writing
		 * TABLE_ID/TABLE_OFFSET; without a plan the mirror returns the last
		 * generic write to 0x000f, which means nothing for the addressed cell.
		 *
		 * Approximation: the pre-state is the VALUE THE VENDOR WRITES
		 * (assuming the chip does an identity-like RMW with the pre-state
		 * already set by the init). If the scratch formula were the identity
		 * with these inputs it would match. In practice it is not, but the
		 * flow advances. The real lo_read/hi_read/cur values need an ad hoc
		 * instrumented read on the real chip.
		 *
		 * Order in which the plan is consumed (channel_setup call site):
		 *  - TX-LPF: core 0 stage 0..8 (18 reads: lo,hi x 9), then core 1
		 *    (another 18) = 36 reads.
		 *  - Dacbuf: core 0 stage 0..8 (9 reads), core 1 (9) = 18 reads.
		 *  - RX-LPF: core 0 stage 0..2 (6 reads: lo,hi x 3), core 1 (6)
		 *    = 12 reads.
		 * Total: 66 reads.
		 */
	{
		static const u16 t7_pre_seed[] = {
			/* TX-LPF core 0: stage 0..8, (lo, hi) */
			0x50db, 0x0151,  0x50db, 0x0151,  0x50db, 0x0151,
			0x5123, 0x0151,  0x5123, 0x0151,  0x5123, 0x0151,
			0x516b, 0x0151,  0x516b, 0x0151,  0x50db, 0x0151,
			/* TX-LPF core 1 */
			0x50db, 0x0151,  0x50db, 0x0151,  0x50db, 0x0151,
			0x5123, 0x0151,  0x5123, 0x0151,  0x5123, 0x0151,
			0x516b, 0x0151,  0x516b, 0x0151,  0x50db, 0x0151,
				/* Dacbuf core 0: stage 0..8 (add={b,b,c,c,e,e,f,f,a}).
				 * Stages 0-1 read/write the same cell 0x3fb; the pre-state
				 * given here is the chip's BEFORE the first stage RMW. */
			0x0b2e, 0x0b2e, 0x0b2e, 0x0b2e, 0x0b2e,
			0x0b2e, 0x0b2e, 0x0b2e, 0x002e,
			/* Dacbuf core 1 */
			0x0b2e, 0x0b2e, 0x0b2e, 0x0b2e, 0x0b2e,
			0x0b2e, 0x0b2e, 0x0b2e, 0x002e,
			/* RX-LPF, ordine vendor stage-then-core.
			 * stage 0: core 0 lo/hi, core 1 lo/hi
			 * stage 1: core 0 lo/hi, core 1 lo/hi
			 * stage 2: core 0 lo/hi, core 1 lo/hi */
			0x2440, 0x0150,  0x2440, 0x0150,
			0x2349, 0x0150,  0x2349, 0x0150,
			0x2352, 0x0150,  0x2352, 0x0150,
		};
		b43_test_plan_phy_reads(0x000f, t7_pre_seed,
			(int)(sizeof(t7_pre_seed) / sizeof(u16)));
	}

		/*
		 * run_rfseq_cmd reads 0x0403 and stops when bit 0 has *dropped*: the
		 * invocations are `0x0101, 0x0000` or `0x0000` alone. In the ch36
		 * slice there are 18, with pattern [2,1,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,1]
		 * -- the plan reproduces it whole.
		 *
		 * A plan tuned on the opposite read -- waiting for the bit to rise --
		 * runs out and from the third invocation falls on the mirror, which
		 * returns 0. The gate does NOT catch it: the ch36 capture has the read
		 * values at UNDEFINED and the comparison ignores them, so a wrong plan
		 * here does not show in the score.
		 */
	{
		static const u16 rfseq_done_poll[] = {
			0x0101, 0x0000, 0x0000, 0x0101, 0x0000, 0x0101,
			0x0000, 0x0101, 0x0000, 0x0101, 0x0000, 0x0101,
			0x0000, 0x0101, 0x0000, 0x0101, 0x0000, 0x0101,
			0x0000, 0x0000, 0x0101, 0x0000, 0x0101, 0x0000,
			0x0101, 0x0000, 0x0101, 0x0000, 0x0101, 0x0000,
			0x0101, 0x0000, 0x0000,
		};
		b43_test_plan_phy_reads(0x0403, rfseq_done_poll,
			(int)(sizeof(rfseq_done_poll) / sizeof(u16)));
	}

		/*
		 * Idle-TSSI measurement: idle_tssi_meas derives the base index from
		 * 0x0012 >> 2, and in the ch36 slice the bases written are core0
		 * 0x206/0x207/0x206 and core1 0x200 -- so the reads are base << 2, in
		 * the order core0, core1 for each of the three iterations. The ch36
		 * capture does not have the values (UNDEFINED), but the writes imply
		 * them.
		 */
	{
		static const u16 idle_tssi_meas_vals[] = {
			0x0818, 0x0800,   /* iter 1: core 0, core 1 */
			0x081c, 0x0800,   /* iter 2 */
			0x0818, 0x0800,   /* iter 3 */
		};
		b43_test_plan_phy_reads(0x0012, idle_tssi_meas_vals,
			(int)(sizeof(idle_tssi_meas_vals) / sizeof(u16)));
	}

		/*
		 * RXIQ accumulators. The solve sums the two most recent rounds, so the
		 * pair must give the coefficients the ch36 capture writes: core 0
		 * `0x03f2/0x004c`, core 1 `0x03db/0x0037`. The values are the same on
		 * every round, so any pair produces the same result.
		 *
		 * They are not measured values -- ch36 does not record the reads -- but
		 * an input *consistent with the observed output*. The gate stays
		 * meaningful: the coefficients are what it checks, and if the solve
		 * regressed the output would change. Validating the real measurement
		 * takes AC_READ_ORACLE, see test/unit/README.md, "Read plans and the
		 * oracle".
		 */
	{
		static const u16 acc_06c3[6] = {
			0x0262, 0x0262, 0x0262, 0x0262, 0x0262, 0x0262,
		};
		static const u16 acc_06c2[6] = {
			0x5a00, 0x5a00, 0x5a00, 0x5a00, 0x5a00, 0x5a00,
		};
		static const u16 acc_06c5[6] = {
			0x02c0, 0x02c0, 0x02c0, 0x02c0, 0x02c0, 0x02c0,
		};
		static const u16 acc_06c4[6] = {
			0x5564, 0x5564, 0x5564, 0x5564, 0x5564, 0x5564,
		};
		static const u16 acc_06c1[6] = {
			0x0008, 0x0008, 0x0008, 0x0008, 0x0008, 0x0008,
		};
		static const u16 acc_06c0[6] = {
			0x489b, 0x489b, 0x489b, 0x489b, 0x489b, 0x489b,
		};
		static const u16 acc_08c3[6] = {
			0x0262, 0x0262, 0x0262, 0x0262, 0x0262, 0x0262,
		};
		static const u16 acc_08c2[6] = {
			0x5a00, 0x5a00, 0x5a00, 0x5a00, 0x5a00, 0x5a00,
		};
		static const u16 acc_08c5[6] = {
			0x02a6, 0x02a6, 0x02a6, 0x02a6, 0x02a6, 0x02a6,
		};
		static const u16 acc_08c4[6] = {
			0x60dc, 0x60dc, 0x60dc, 0x60dc, 0x60dc, 0x60dc,
		};
		static const u16 acc_08c1[6] = {
			0x0015, 0x0015, 0x0015, 0x0015, 0x0015, 0x0015,
		};
		static const u16 acc_08c0[6] = {
			0xfe20, 0xfe20, 0xfe20, 0xfe20, 0xfe20, 0xfe20,
		};

		b43_test_plan_phy_reads(0x06c3, acc_06c3, 6);
		b43_test_plan_phy_reads(0x06c2, acc_06c2, 6);
		b43_test_plan_phy_reads(0x06c5, acc_06c5, 6);
		b43_test_plan_phy_reads(0x06c4, acc_06c4, 6);
		b43_test_plan_phy_reads(0x06c1, acc_06c1, 6);
		b43_test_plan_phy_reads(0x06c0, acc_06c0, 6);
		b43_test_plan_phy_reads(0x08c3, acc_08c3, 6);
		b43_test_plan_phy_reads(0x08c2, acc_08c2, 6);
		b43_test_plan_phy_reads(0x08c5, acc_08c5, 6);
		b43_test_plan_phy_reads(0x08c4, acc_08c4, 6);
		b43_test_plan_phy_reads(0x08c1, acc_08c1, 6);
		b43_test_plan_phy_reads(0x08c0, acc_08c0, 6);
	}

		/*
		 * Results of the TX IQ/LO cal: txiqlo_cmd writes back the value it
		 * read, so the plan carries what the ch36 capture writes back. Keyed
		 * on the read offset, not on the position in 0x000f's queue.
		 */
	{
		static const u16 afe_80[] = {
			0x0060, 0x0000, 0x0064, 0x000e,
			0x0060, 0x0000, 0x0062, 0xfffd,
		};
		static const u16 afe_83[] = {
			0xff01, 0xfe02,
		};
		static const u16 afe_84[] = {
			0x0102,
		};
		static const u16 afe_85[] = {
			0x0301,
		};
		static const u16 afe_87[] = {
			0x0020, 0x0000, 0x0022, 0x0004,
			0x0020, 0x0000, 0x0023, 0x0003,
		};
		static const u16 afe_8a[] = {
			0x0100, 0x0100,
		};
		static const u16 afe_8b[] = {
			0x0000,
		};
		static const u16 afe_8c[] = {
			0x0001,
		};
		static const u16 afe_8e[] = {
			0xfee0, 0x0080, 0xfed6, 0x00b6,
			0x00c0, 0xff40, 0x00dd, 0xff57,
		};
		static const u16 afe_91[] = {
			0x0d6e, 0x0764,
		};
		static const u16 afe_92[] = {
			0x05fe,
		};
		static const u16 afe_93[] = {
			0xfb03,
		};

		b43_test_plan_table_cell(0x000c, 0x0080, afe_80,
					 ARRAY_SIZE(afe_80));
		b43_test_plan_table_cell(0x000c, 0x0083, afe_83,
					 ARRAY_SIZE(afe_83));
		b43_test_plan_table_cell(0x000c, 0x0084, afe_84,
					 ARRAY_SIZE(afe_84));
		b43_test_plan_table_cell(0x000c, 0x0085, afe_85,
					 ARRAY_SIZE(afe_85));
		b43_test_plan_table_cell(0x000c, 0x0087, afe_87,
					 ARRAY_SIZE(afe_87));
		b43_test_plan_table_cell(0x000c, 0x008a, afe_8a,
					 ARRAY_SIZE(afe_8a));
		b43_test_plan_table_cell(0x000c, 0x008b, afe_8b,
					 ARRAY_SIZE(afe_8b));
		b43_test_plan_table_cell(0x000c, 0x008c, afe_8c,
					 ARRAY_SIZE(afe_8c));
		b43_test_plan_table_cell(0x000c, 0x008e, afe_8e,
					 ARRAY_SIZE(afe_8e));
		b43_test_plan_table_cell(0x000c, 0x0091, afe_91,
					 ARRAY_SIZE(afe_91));
		b43_test_plan_table_cell(0x000c, 0x0092, afe_92,
					 ARRAY_SIZE(afe_92));
		b43_test_plan_table_cell(0x000c, 0x0093, afe_93,
					 ARRAY_SIZE(afe_93));
	}

		/*
		 * PHY pre-seed: at boot 0x0401 (RF_SEQ_MODE) has bits 0-2 = coremask
		 * and bits 12-14 = coremask<<12. The vendor reads these bits as
		 * saved_401 inside rxcore_setstate and restores them at the end
		 * (vendor #34553: MOD val=0x0003 mask=0x0007; #34554: MOD
		 * val=0x7000 mask=0x7000). The test framework starts from mirror=0,
		 * so the restore would fail with val=0. Pre-seed = 0x7003 for the
		 * D6220 (coremask=0x03).
		 */
	b43_test_mirror_phy_set(0x0401, 0x7003);

		/*
		 * Radio-side state already present at the first maskset of
		 * b43_radio_2069_channel_setup. On all the vendor captures (d6220 and
		 * agcombo) the registers have bits already set before the channel
		 * switch:
		 *   0x0645: bit 7 (0x0080) -- bias/enable set by an earlier init
		 *           (mode_init/pwron).
		 *   0x08c9: bits 1-2 (0x0006) -- PLL configuration persistent across
		 *           channel switches.
		 * The read plan returns these values so radio_maskset's peek produces
		 * the same RAD.WR val=... as the vendor.
		 */
	{
		static const u16 rad_0645[] = { 0x0080 };
		static const u16 rad_08c9[] = { 0x0006 };
		static const u16 rad_090b[] = { 0x0100 };	/* immediate PLL lock */
			/*
			 * The per-core radio bias registers (0x0045/0x0245/0x0445)
			 * have bits set before afe_lpf_stage loop2's radio_set(0x0080).
			 * Vendor #33037/33058/... writes val=0x70bf -> cur = 0x70bf &
			 * ~0x0080 = 0x703f. Present in all the d6220 and agcombo
			 * captures.
			 *
			 * For 0x0445 (core 2, inactive on the d6220, coremask=0x03) the
			 * second read (during post_noise_shaping_core_transition at
			 * #38058) returns 0x7080 in the vendor -- bits 0-6 clear except
			 * bit 7 -- so the maskset ~0x0300, 0x0300 produces 0x7380
			 * (instead of 0x73bf for the active cores). How the blob brings
			 * the register to 0x7080 for the inactive core does not show in
			 * the trace (no intermediate WR); a second value in the read plan
			 * stands for it.
			 */
		static const u16 rad_0045[] = { 0x703f };
		static const u16 rad_0245[] = { 0x703f };
		static const u16 rad_0445[] = { 0x703f, 0x7080 };
		b43_test_plan_radio_reads(0x0645, rad_0645, 1);
		b43_test_plan_radio_reads(0x08c9, rad_08c9, 1);
		b43_test_plan_radio_reads(0x090b, rad_090b, 1);
		b43_test_plan_radio_reads(0x0045, rad_0045, 1);
		b43_test_plan_radio_reads(0x0245, rad_0245, 1);
		b43_test_plan_radio_reads(0x0445, rad_0445, 2);

			/*
			 * 0x0017 / 0x0217: bit 4 is moved by the hardware, not by the
			 * driver, so the read plan has to follow it slot by slot.
			 *
			 *   [0] [1]  bit 4 low, after a WR of 0
			 *   [2]      tempsense_radio_setup's peek. It ends up in
			 *            tempsense_radio_saved[] and the restore writes it
			 *            back: the ch36 capture restores 0x0011, so the peek
			 *            has to read that
			 *   [3] [4]  inner RDs of the masksets that follow
			 *   [5]      between the cleanup and iter 2 the hardware bounces
			 *            to 0x0002 (bit 4 low, bit 1 high) without any traced
			 *            WR: probably a cal-done flag
			 *   [6]      guard at 0 for iter 3, which reads the stable mirror
			 *   [7]      baseline of cal_restore's loop, peek only
			 *   [8] [9]  inner RDs of the masksets, with bit 4 raised again by
			 *            the hardware
			 */
		static const u16 rad_0017[] = {
			0, 0, 0x0011, 0x0010, 0x0011, 0x0002, 0,
			0, 0x0010, 0x0011,
		};
		static const u16 rad_0217[] = {
			0, 0, 0x0011, 0x0010, 0x0011, 0x0002, 0,
			0, 0x0010, 0x0011,
		};

		/* Setup peek: the cleanup restores 0x0001. */
		static const u16 rad_000e[] = { 0x0001 };
		static const u16 rad_020e[] = { 0x0001 };

		b43_test_plan_radio_reads(0x000e, rad_000e,
					  ARRAY_SIZE(rad_000e));
		b43_test_plan_radio_reads(0x020e, rad_020e,
					  ARRAY_SIZE(rad_020e));
		b43_test_plan_radio_reads(0x0017, rad_0017,
					  ARRAY_SIZE(rad_0017));
		b43_test_plan_radio_reads(0x0217, rad_0217,
					  ARRAY_SIZE(rad_0217));

		/*
		 * 0x0024 / 0x0224 (radio bits 0-1 HW-sticky). The vendor at
		 * #52770/#52803 emits WR val=0x0303 (MOD mask=0x0700 val=0x0300 ->
		 * RD_val=0x0003). The last WR on the register was val=0x0000
		 * (#50117), but the HW keeps bits 0-1 set.
		 * Slots 0-3: match the mirror sequence of the 4 pre-setup reads.
		 * Slot 4: baseline setup peek (not used in computations).
		 * Slot 5: inner RD of the MOD -> 0x0003 for WR=0x0303.
		 */
		static const u16 rad_0024[] = {
			0x0003, 0x0003, 0x0003, 0x0000, 0x0000, 0x0003,
		};
		static const u16 rad_0224[] = {
			0x0003, 0x0003, 0x0003, 0x0000, 0x0000, 0x0003,
		};
		b43_test_plan_radio_reads(0x0024, rad_0024,
					  ARRAY_SIZE(rad_0024));
		b43_test_plan_radio_reads(0x0224, rad_0224,
					  ARRAY_SIZE(rad_0224));
	}

		/*
		 * B5 RX AFE calibration polls (vendor #41909+): register 0x0380 is
		 * armed with a command (bit 15 set) and then polled until bit 15
		 * clears. Poll counts for iter 1..24 hardcoded from the d6220 ch36
		 * trace.
		 */
	{
		#define B(n) 0x8000
		#define D 0x0000
		#define B5 B(0),B(0),B(0),B(0),B(0)
		#define B10 B5,B5
		#define B20 B10,B10
		#define B38 B20,B10,B5,B(0),B(0),B(0)
		#define B41 B38,B(0),B(0),B(0)
		#define B68 B38,B20,B10
		static const u16 poll_0x0380[] = {
			/* Gruppo core 0 (iter 1-6): 39 42 42 69 11 39 */
			B38, D, B41, D, B41, D, B68, D, B10, D, B38, D,
			/* Gruppo core 1 (iter 7-12): 41 40 13 47 61 43 */
			B38, B(0), B(0), D,
			B38, B(0), D,
			B10, B(0), B(0), D,
			B38, B5, B(0), B(0), B(0), D,
			B38, B20, B(0), B(0), D,
			B41, B(0), D,
			/* Gruppo core 2 (iter 13-18): 44 31 5 10 59 42 */
			B41, B(0), B(0), D,
			B20, B10, D,
			B(0), B(0), B(0), B(0), D,
			B5, B(0), B(0), B(0), B(0), D,
			B38, B20, D,
			B41, D,
			/* Gruppo 4 RXIQ measurement (iter 19-24): 43 60 35 12 8 58 */
			B38, B(0), B(0), B(0), B(0), D,
			B38, B20, B(0), D,
			B20, B10, B(0), B(0), B(0), B(0), D,
			B10, B(0), D,
			B5, B(0), B(0), D,
			B38, B10, B(0), B(0), B(0), B(0),
			B(0), B(0), B(0), B(0), B(0), D,
		};
		#undef B
		#undef D
		#undef B5
		#undef B10
		#undef B20
		#undef B38
		#undef B41
		#undef B68

		b43_test_plan_phy_reads(0x0380, poll_0x0380,
			(int)(sizeof(poll_0x0380) / sizeof(u16)));
	}

	int r = b43_phyops_ac.switch_channel(&g_wldev, 36);

		/*
		 * b43_op_config()'s order: switch_channel, the core's BSS
		 * configuration, the TX power adjust b43_phy_txpower_check() queues,
		 * the stack's conf_tx passes, and in place of the final mac_enable
		 * the channel_calibrate phyop, which re-enables the MAC between the
		 * gain control sweep and the calibrations: where the vendor emits it.
		 */
	emit_core_bss_config();
	if (b43_phyops_ac.recalc_txpower(&g_wldev, true) ==
	    B43_TXPWR_RES_NEED_ADJUST) {
		emit_core_bss_mode();
		b43_phyops_ac.adjust_txpower(&g_wldev);
	}
	emit_core_conf_tx_passes();

	if (r == 0)
		b43_phyops_ac.channel_calibrate(&g_wldev, false);
	else
		b43_mac_enable(&g_wldev);

		/*
		 * From here on the driver no longer decides the flow: it reacts to
		 * what happens to it, and what makes it happen is the environment --
		 * the kernel with its timers, mac80211 with the templates and the
		 * bss-up. Here the environment is the capture's timeline. Then the
		 * down as b43 does it: b43_wireless_core_stop() suspends the MAC,
		 * then b43_phy_exit() goes through b43_software_rfkill(), which
		 * suspends and re-enables around the PHY's op.
		 */
	if (r == 0) {
		run_timeline();
		emit_core_stop();
		b43_mac_suspend(&g_wldev);
		b43_phyops_ac.software_rfkill(&g_wldev, true);
		b43_mac_enable(&g_wldev);
	}

	fprintf(stderr, "test: switch_channel returned %d\n", r);
}

/*
 * Reads of the core init whose meaning we do NOT know, transcribed to close
 * the window of the positional comparison. Each one is a TODO.
 *
 *   0x0092  read once before the MAC address write (cold01 #659) and again
 *           at #12194. It is 0xacc on d6220 and agcombo, so it is not
 *           session state but something the ucode publishes. b43.h does not
 *           name it.
 *   0x077c  read as 32 bits in the high/low/high form (#664-#668), the way
 *           the stock driver reads its 32-bit counters. It is zero here: the
 *           first sampling, before anything counts.
 *
 * They are reads, so they do not change the hardware's state: transcribing
 * them costs only that the port makes two accesses it cannot explain. The
 * oracle provides the value, it is not hardwired here.
 */
static void emit_core_shm_unexplained(void)
{
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x0092);
}

static void emit_core_counters_first(void)
{
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077e);
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077c);
	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x077e);
}

/*
 * AMT row 0x3f written to zero a second time, and only where the channel
 * carries the radar guard.
 *
 * Counted on the row's rewrites on all 43 cold segments: three where there
 * is no guard, six where there is. The three weather-radar ones have five,
 * and the missing one is one of the bss-up's, which those segments do not
 * have. The boundary is ch140, the same timeline.py's POLLs give: ch144 has
 * three like ch36.
 *
 * Of the three extra ones this is the only one in the preamble, right after
 * the first counter sampling. The other two fall in the wait of the check --
 * on cold05 at 6.7 s and 69.6 s from the start, the two edges of the sixty
 * seconds -- and this harness does not model that transition: cac_pending is
 * a boolean for the whole run. See docs/retrace-todo.md.
 *
 * No b43 site emits it. The core init of b43/main.c writes the row, and the
 * caller here is the match suspension during the channel availability
 * check, which b43 does not have: that is why it is among the duplicates
 * and not in the PHY.
 */
static void emit_core_amt_cac_suspend(void)
{
	if (!(g_chan.flags & IEEE80211_CHAN_RADAR))
		return;
	b43_test_emit_amt(0x3f, 0);
}

/*
 * Host flags. b43 writes them with b43_hf_write(), which covers words 1..3;
 * the port's core adds 4 and 5. The vendor puts them here, right before the
 * chanspec (cold01 #686-#690), while b43 writes them much earlier, between
 * WLCOREREV and MACHW: the order is an open reconciliation point, and here
 * the vendor is followed because that is what the comparison measures.
 *
 * b43_hf_write() is preceded by b43_hf_read(), three reads the vendor does
 * not do at this point: they are not emitted, they would be three ops more
 * than any capture has.
 *
 * Words 4 and 5 go out with the PARTIAL value, 0x0040 and 0x0080, which is
 * what the vendor writes here: the extra bits (-> 0x0060 and 0x8088) come
 * later, in a block the harness does not model. Writing the final value
 * right away would skip a state the ucode can read meanwhile, and would not
 * match anyway. TODO: find the two points where the bits are added; they are
 * at #12243, #13526 and #13531 on cold01.
 */
static void emit_core_hostflags(void)
{
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x005e, 0x0100);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0060, 0x0000);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0062, 0x0000);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0078, 0x0040);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00d4, 0x0080);
}

/*
 * The GPIO pins b43 registers a LED on, from the mounted SPROM: the rule of
 * b43_led_get_sprominfo() in b43/leds.c, and b43_map_led(),
 * which registers nothing for OFF, ON and INACTIVE. compare.py uses them to
 * tell the vendor's LED ops from the other GPIO ones; they match the
 * mask the stock driver writes to chipcommon 0x8c in the LED block of the
 * attach, on every board.
 */
static u16 led_pins(const struct ssbn_sprom *s)
{
	static const u8 defaults[4] = {
		B43_LED_ACTIVITY, B43_LED_RADIO_B, B43_LED_RADIO_A, B43_LED_OFF,
	};
	u8 bh[4 + ARRAY_SIZE(s->gpio_ext)] = { s->gpio0, s->gpio1, s->gpio2, s->gpio3 };
	bool none = (bh[0] & bh[1] & bh[2] & bh[3]) == 0xff;
	u16 pins = 0;
	unsigned int i;

	memcpy(&bh[4], s->gpio_ext, sizeof(s->gpio_ext));
	for (i = 0; i < ARRAY_SIZE(bh); i++) {
		u8 b;

		if (i < 4 && none)
			b = defaults[i];
		else if (bh[i] == 0xff || (i >= 4 && !bh[i]))
			b = B43_LED_OFF;
		else
			b = bh[i] & B43_LED_BEHAVIOUR;
		if (b != B43_LED_OFF && b != B43_LED_ON && b != B43_LED_INACTIVE)
			pins |= 1u << i;
	}
	return pins;
}

/*
 * Duplicate of b43_shm_macaddr_set(), which lives in b43/main.c and the
 * harness does not compile.
 *
 * It is here and not in the PHY because it is NOT PHY code: there it would
 * be a misattribution. Shape and values are the core's -- three words at
 * 0x078c, low byte of each pair first -- so if the core changes this becomes
 * wrong and the comparison says so.
 *
 * The POINT where it is called is wl's (right after the retransmission
 * limits in core init), not b43's, which calls b43_upload_card_macaddress()
 * later. It is the same licence the harness takes elsewhere in modelling the
 * core's order, and the reconciliation of the two orders is in
 * docs/retrace-todo.md: here it lets the comparison move past these three
 * ops, it does not prove b43 emits them at the right place.
 */
static void emit_core_shm_macaddr(const struct board_profile *p)
{
	unsigned int i;

	for (i = 0; i < 6; i += 2)
		b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(0x078c + i),
				(u16)(p->macaddr[i] | (p->macaddr[i + 1] << 8)));
}

/*
 * Duplicate of the cells the port adds to the core init of b43/main.c.
 *
 * Split in two because the trace has them at two distinct points: ANTSWAP
 * and BTSFOFF in the chip init block before the chanspec (cold01 #650,
 * #656), the rest in the MAC config block after the radio init. The order
 * matters: putting one of these after the chanspec when the vendor does it
 * before is enough to lose the pairing.
 *
 * The cells of the second group are not mirrored: they fall between #12197
 * and #14172, and emitting them elsewhere would put them in the wrong place
 * -- worse than not emitting them. The core writes them anyway, because
 * what matters to the hardware is the final state; here the order matters,
 * and we cannot reproduce it.
 */
static void emit_core_shm_chipinit(const struct board_profile *p)
{
	/* b43/main.c, core init */
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0080, 8);      /* MAXBFRAMES */
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x005c, 0x000a); /* ANTSWAP */

	/*
	 * The core's operating mode: INFRA and DISCPMQ, AP cleared. b43 does it
	 * in b43_adjust_opmode(); the vendor emits it here, between ANTSWAP and
	 * WLCOREREV, not with the GPIO.CTL.
	 */
	b43_test_emit_mctrl((u32)~0x40060000u, 0x40020000);

	/*
	 * These b43 already writes; they are missing only because the harness
	 * compiles the PHY and not main.c. In the vendor's order, which is NOT
	 * b43's: b43 puts WLCOREREV, then the host flags, then MACHW; the vendor
	 * puts MACHW right after WLCOREREV and the host flags much later, right
	 * before the chanspec. See docs/retrace-todo.md.
	 */
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0016, p->core_rev);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00c0,
			(u16)(p->mac_hw_cap & 0xffff));
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00c2,
			(u16)((p->mac_hw_cap >> 16) & 0xffff));
	/*
	 * The retransmission limits, cold01 #655: scratch words 6 and 7, which
	 * wl-diag prints at 0x0018 and 0x001c. They are not BTL0 and BTSFOFF,
	 * the shared memory cells at the same addresses, which the vendor
	 * writes later in the BSS config. b43 writes them in
	 * b43_set_retry_limits(); the long one is 6, where mac80211 gives b43 4.
	 */
	b43_shm_write16(&g_wldev, B43_SHM_SCRATCH, B43_SHM_SC_SRLIMIT, 7);
	b43_shm_write16(&g_wldev, B43_SHM_SCRATCH, B43_SHM_SC_LRLIMIT, 6);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0044, 3);      /* SFFBLIM */
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0046, 2);      /* LFFBLIM */
}

/*
 * The probe response template is NOT loaded: b43 does not have the firmware
 * answer probes -- PRMAXTIME=1 in b43_wireless_core_init() -- and
 * consistently writes none of the three template cells. The vendor's ops on
 * PRTLEN, PRSSID, PRSSIDLEN, on the timings 0x0180-0x0186 and on the
 * template RAM word 0x0700 are in compare.py's VENDOR_ONLY.
 *
 * A choice of the WIP, not a limit: see "Probe-response offload" in
 * docs/retrace-todo.md. When the offload is implemented the VENDOR_ONLY
 * entry goes, and the load is written in the kernel's main.c -- not here.
 */

/*
 * Length of the beacon template. It grows by one byte per width step --
 * 0x012a at 20 MHz, 0x012b at 40, 0x012c at 80 on all segments -- because the
 * VHT operation element changes. The template RAM write that precedes the
 * length is the whole template, in 32-bit words: TPL.RAMW carries its byte
 * count, the length rounded up to 4.
 */
static u16 beacon_tpl_len(void)
{
	enum nl80211_chan_width w = g_wldev.phy.chandef->width;

	/* Fixed part plus the SSID: 0x012a with the seven characters of test-ap. */
	return (u16)(0x0123 + g_ssid_len +
		     (w == NL80211_CHAN_WIDTH_80 ? 2 :
		      w == NL80211_CHAN_WIDTH_40 ? 1 : 0));
}

/*
 * Duplicate of the core's b43_ac_cac_match_gate(): the BSS's AMT row at the
 * check's two boundaries, closed by the config that tunes to the radar
 * channel and reopened by the beacon. The shape is the capture's -- the
 * logical record and the row's traffic -- and it is the same as
 * emit_core_amt_cac_suspend(), which covers the third occurrence, in the
 * preamble.
 */
static void emit_core_cac_gate(bool open)
{
		/*
		 * The five TX FIFOs in the vendor's order: 1, 3, 0, 2, 4. When closing
		 * each is preceded by a MACCONTROL read, when opening it is not.
		 */
	static const u16 txctl[] = { 0x0240, 0x02c0, 0x0200, 0x0280, 0x0300 };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(txctl); i++) {
		u32 v;

		if (!open)
			b43_read32(&g_wldev, B43_MMIO_MACCTL);
		v = b43_read32(&g_wldev, txctl[i]);
		b43_write32(&g_wldev, txctl[i],
			    open ? v & ~B43_DMA64_TXSUSPEND : v | B43_DMA64_TXSUSPEND);
	}
	b43_test_emit_amt(0x3f, open ? 0x8008 : 0);
}

/*
 * Duplicate of the core's b43_update_templates(), which mac80211 calls when
 * the beacon changes. @which alternates beacon0 and beacon1 and the series
 * starts from beacon0.
 */
static void emit_core_beacon_reload(unsigned int which)
{
	struct b43_wldev *dev = &g_wldev;
	static bool late_head_done;
	u16 btl = (which & 1) ? 0x001a : 0x0018;

	/*
	 * Head of the first late reload and of that one only: four cells at
	 * 0x2637, once in the whole segment. The condition is "the first late
	 * one" and not "index zero" because the index also counts the reloads of
	 * the BSS block and of the conf_tx passes, which do not have the head:
	 * on cold05 the first late one is the segment's sixth.
	 */
	if (!late_head_done) {
		u16 off;

		for (off = 0x0300; off <= 0x0306; off += 2)
			b43_shm_write16(dev, B43_SHM_SHARED, off, 0x2637);
	}

	b43_shm_write16(dev, B43_SHM_SHARED, 0x00cc,
			b43_shm_read16(dev, B43_SHM_SHARED, 0x00cc));
	/* Fixed part plus the SSID, as in emit_core_bss_ssid(). */
	b43_shm_write16(dev, B43_SHM_SHARED, 0x001e, (u16)(0x003c + g_ssid_len));
	b43_test_tplram_write16(btl == 0x0018 ? 0x0200 : 0x0480,
				(beacon_tpl_len() + 3) & ~3);
	b43_shm_write16(dev, B43_SHM_SHARED, btl, beacon_tpl_len());

	b43_mac_suspend(dev);
	b43_phy_ac_prb_rsp_plcp_pass(dev, g_ssid_len);
	b43_mac_enable(dev);

	/*
	 * Tail of the first reload and of that one only, like the head: three
	 * cells at zero, once in the whole segment.
	 */
	if (!late_head_done) {
		late_head_done = true;
		b43_shm_write16(dev, B43_SHM_SHARED, 0x00a4, 0x0000);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x00b4, 0x0000);
		b43_shm_write16(dev, B43_SHM_SHARED, 0x00d6, 0x0000);
	}
}

/*
 * The tail shared by the two passes that load a beacon template: TIMBPOS,
 * the template in template RAM and its length (@btl, 0x0018 for the first
 * and 0x001a for the second). b43 does the beacon templates:
 * b43_upload_beacon0() and b43_upload_beacon1() go through
 * b43_write_template_common().
 */
static void emit_core_bss_ssid(u16 btl)
{
	/* Fixed part plus the SSID: 0x0043 with the seven characters of test-ap. */
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x001e,
			(u16)(0x003c + g_ssid_len));
	/*
	 * The template's offset follows the length cell: BTL0 loads beacon0 at
	 * 0x0200, BTL1 loads beacon1 at 0x0480. They are not b43.h's
	 * BT_BASE0/BT_BASE1 constants, 0x0068 and 0x0468: the AC's template RAM
	 * layout is not the v4 firmware's.
	 */
	b43_test_tplram_write16(btl == 0x0018 ? 0x0200 : 0x0480,
				(beacon_tpl_len() + 3) & ~3);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, btl, beacon_tpl_len());
}

/*
 * The core's BSS configuration, cold01 #13593-#13624.
 *
 * It sits between the two halves of the channel setup: in b43 the core does
 * it inside b43_op_config(), between b43_switch_channel() returning and the
 * call to b43_phy_txpower_check(). The position constraint is real, not a
 * trace alignment: the ucode reads the template lengths when it transmits,
 * so they must be there before the MAC restarts -- and the MAC is still
 * suspended here, the enable comes later.
 *
 * The probe response cells (PRTLEN, PRSSID, PRSSIDLEN) are not written: see
 * the note on the probe response template above.
 */
static void emit_core_bss_config(void)
{
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0020, 0x0000);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0022, 0x0000);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x0012, 0x0003);

	b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x00cc);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00cc,
			b43_phy_ac_bss_cc(&g_wldev));
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00cc,
			b43_phy_ac_bss_cc(&g_wldev) | 0x0001);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00ce,
			b43_phy_ac_bss_beacon_phyctl1(&g_wldev));
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00d0, 0x0000);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x001c, 0x003a);
	emit_core_bss_ssid(0x0018);
	g_beacon_reloads++;
	/*
	 * The PLCPs close every template load, this one included: cold01 has them
	 * at #13625, right after PRSSIDLEN, and again at the end of each of the
	 * four conf_tx passes.
	 */
	b43_phy_ac_prb_rsp_plcp_pass(&g_wldev, g_ssid_len);
}

/*
 * Second pass of the BSS config, cold01 #14103-#14127: the second beacon
 * template. It differs from the first in the length -- 0x001a (BTL1) instead
 * of 0x0018 (BTL0), like b43_upload_beacon1() against b43_upload_beacon0()
 * -- and in 0x00cc getting a single write instead of two.
 */
static void emit_core_bss_config1(void)
{
	u16 cc = b43_shm_read16(&g_wldev, B43_SHM_SHARED, 0x00cc);

	b43_shm_write16(&g_wldev, B43_SHM_SHARED, 0x00cc, cc);
	emit_core_bss_ssid((g_beacon_reloads & 1) ? 0x001a : 0x0018);
	g_beacon_reloads++;
}

/*
 * EDCF parameters of an access queue, cold01 #14085-#14102 and the three
 * twin blocks at #14170, #14249 and #14328.
 *
 * Eight words per queue plus eight zeros up to +0x1e, preceded by the read
 * of the status cell at +0x0e. The layout is b43_qos_params_upload()'s:
 * TXOP, CWMIN, CWMAX, CWCUR, AIFS, BSLOTS, REGGAP, STATUS.
 *
 * The values are the EDCA defaults -- TXOP 3008 and 1504 us for video and
 * voice and zero for best effort and background, CWMIN 15/15/7/3, AIFS
 * 3/7/1/1 -- and REGGAP is AIFS + BSLOTS on all four. BSLOTS is the backoff
 * drawn at random and so is not derived: these are the capture's, and on
 * another capture they will differ.
 *
 * The order of the queues is the one the vendor emits, not 0..3.
 */
struct edcf_queue {
	u16 base;
	u16 txop;
	u16 cwmin;
	u16 cwmax;
	u16 aifs;
	u16 bslots;
};

static const struct edcf_queue edcf_queues[] = {
	{ 0x0260, 0x0000, 15,   63, 3, 9 },	/* best effort */
	{ 0x0240, 0x0000, 15, 1023, 7, 0 },	/* background */
	{ 0x0280, 0x0bc0,  7,   15, 1, 2 },	/* video */
	{ 0x02a0, 0x05e0,  3,    7, 1, 0 },	/* voice */
};

static void emit_core_edcf_queue(const struct edcf_queue *q)
{
	u16 off;

	b43_shm_read16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x0e));
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x00), q->txop);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x02), q->cwmin);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x04), q->cwmax);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x06), q->cwmin);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x08), q->aifs);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x0a), q->bslots);
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x0c),
			(u16)(q->aifs + q->bslots));
	b43_shm_write16(&g_wldev, B43_SHM_SHARED, (u16)(q->base + 0x0e), 0x0100);
	for (off = 0x10; off <= 0x1e; off += 2)
		b43_shm_write16(&g_wldev, B43_SHM_SHARED,
				(u16)(q->base + off), 0x0000);
}

/*
 * A conf_tx pass, cold01 #14083-#14167 and its three twins.
 *
 * It sits between the two halves of the channel setup's tail, inside its
 * enable/suspend bracket: the queue's parameters, the template, and the
 * PLCPs of the eight rates the PHY computes from the probe response length.
 * The first pass also loads the second beacon template; the other three
 * only repeat the probe response.
 */
static void emit_core_conf_tx_pass(unsigned int n)
{
	/*
	 * The enable and the suspend are a pulse and come before the load, not
	 * around it: cold01 #14083-#14084 and then #14085, which opens the queue
	 * parameters. (Wrapping the load stops the positional gate there, at
	 * @11822, with the vendor on the suspend and the port on the parameter
	 * read.)
	 */
	b43_mac_enable(&g_wldev);
	b43_mac_suspend(&g_wldev);
	emit_core_edcf_queue(&edcf_queues[n]);
	if (g_edcf_reload_mask & (1u << n))
		emit_core_bss_config1();
	b43_phy_ac_prb_rsp_plcp_pass(&g_wldev, g_ssid_len);
}

static void emit_core_conf_tx_passes(void)
{
	unsigned int q;

	for (q = 0; q < ARRAY_SIZE(edcf_queues); q++)
		emit_core_conf_tx_pass(q);

	/*
	 * A fifth pulse, this one without a load, before the setup's tail
	 * resumes: cold01 has it right before the peek of PHY 0x019e of the
	 * second half.
	 *
	 * The pulse's suspend is the one of the temperature read with which
	 * channel_calibrate opens. Without the read (phycal_tempdelta 0, the
	 * four segments of the TG789vac's second boot) the vendor emits only
	 * the enable and calibrates with the MAC on: here the MAC stays
	 * suspended and channel_calibrate emits that enable.
	 */
	if (g_sprom.phycal_tempdelta) {
		b43_mac_enable(&g_wldev);
		b43_mac_suspend(&g_wldev);
	}
}

/*
 * Duplicate of b43_amt_write(), which lives in b43/main.c, over all the rows,
 * as the core init clears them.
 *
 * b43_test_emit_amt() emits the logical record and the row's traffic: the
 * tracer's hook on wlc_bmac_write_amt gives `AMT.WR idx=`, and below it come
 * the read and the rewrite of the row's 8 bytes on the RCMTA routing, which
 * `OBJ.BULKR`/`OBJ.BULKW` cover. So the row is compared whole.
 *
 * The two rows at the top -- BSSID and station address -- are not emitted:
 * the capture clears at #307-#622 and writes the rows with the flags at
 * #13302 and #13943, inside the bss-up and through `ADDRM.SET`, which is
 * the entry the core calls and this harness does not model. Emitting them
 * here would be six ops at the wrong point, worse than not emitting them.
 */
static void emit_core_amt(const struct board_profile *p)
{
	u16 i;

	for (i = 0; i < 64; i++)
		b43_test_emit_amt(i, 0);
	(void)p;
}

/*
 * Continuous bring-up, in b43_phy_init()'s order: switch_analog(true) ->
 * software_rfkill(false) -> ops->init -> switch_channel.
 */
static void run_full(void)
{
	/*
	 * The three entries b43 makes before b43_phy_init(): the attach's reset
	 * (main.c:1455 from 5650), the attach's tail (5664) and the core-init
	 * reset (1455 from 4956). The attach's reset carries the part of the
	 * preamble the vendor does in its own attach -- the AFE_ON bank twice
	 * (#557, #576), the seven clears, the PMU request -- and the entry of
	 * b43_phy_init() carries the rest; the other two emit nothing. The AFE_ON
	 * bank comes back only in the bss-up (#36534) and the AFE_DOWN bank
	 * appears only once, at #1262, which is mode_init. If one of the three
	 * starts emitting, the comparison says so right away.
	 *
	 * The chanspec write to shared memory (OBJ.WR 0x00a0), which the `down`
	 * flow does, is missing here. It is not a line to add: the capture has
	 * it at #691, after the attach's two AFE units and the core's MAC config,
	 * and putting it at the head places it wrong. It goes with the ordering
	 * of the attach's head, the double analog programming of
	 * docs/retrace-todo.md.
	 */
	{
		const struct cfg80211_chan_def *saved = g_wldev.phy.chandef;

		/*
		 * Without a channel, as b43 sees them: b43_phy_init() assigns
		 * phy->chandef (phy_common.c:93) on the line before its
		 * switch_analog, and nobody earlier. The preamble keys on exactly
		 * that to recognize the right entry, so with it assigned here the
		 * preamble would land on the first entry instead of the fourth.
		 */
		g_wldev.phy.chandef = NULL;
		b43_phyops_ac.switch_analog(&g_wldev, true);
		b43_phyops_ac.switch_analog(&g_wldev, false);
		b43_phyops_ac.switch_analog(&g_wldev, true);
		g_wldev.phy.chandef = saved;
	}

	/* The entry of b43_phy_init(), the one that carries the preamble. */
	b43_phyops_ac.switch_analog(&g_wldev, true);
	/*
	 * The order is the trace's, and it matters more than the values: a
	 * single inversion drops the op from the comparison. On cold01: AMT
	 * #443, chip init block #649-#658, MAC in shared memory #661-#663, host
	 * flags #686-#690, chanspec #691 (that one the driver writes, in
	 * op_software_rfkill).
	 */
	emit_core_amt(g_profile);
	emit_core_shm_chipinit(g_profile);
	emit_core_shm_unexplained();
	emit_core_shm_macaddr(g_profile);
	emit_core_counters_first();
	emit_core_amt_cac_suspend();
	emit_core_hostflags();
	run_rfkill();
	run_op_init();
	/*
	 * b43_phy_init clears phy->do_full_init between ops->init and
	 * switch_channel, so switch_channel's code never sees the flag raised
	 * -- gating on do_full_init there has no effect in the real driver.
	 */
	g_wldev.phy.do_full_init = false;
	run_switch_channel();
}

int main(int argc, char **argv)
{
	const char *flow  = (argc > 1) ? argv[1] : "full";
	const char *board = (argc > 2) ? argv[2] : "d6220";

	const struct board_profile *p = board_profile_lookup(board);

	fprintf(stderr, "test: board=%s flow=%s\n", p->name, flow);
	mount_board(p);
	if (!strcmp(flow, "led_pins")) {
		printf("0x%04x\n", led_pins(&g_sprom));
		return 0;
	}
	b43_test_plans_reset();
	plan_rxiq_poll(p->name, g_wldev.phy.do_full_init);
	g_wldev.mac_suspended = 1;
	b43_test_trace_to(stdout);

	/*
	 * PLL state the PMU brings up before the driver runs, per chip:
	 * PLLCTL2=0x0c31 on both, PLLCTL3=0x00133333 on 4352 / 0x100e on 4360.
	 * op_init verifies PLLCTL3 on the 4352, so seed it to the real value.
	 */
	b43_test_pll_set(2, 0x0c31);
	b43_test_pll_set(3, p->chip_id == 0x4360 ? 0x100e : 0x00133333);

	/*
	 * Pre-populate radio-mirror slots set by earlier init flows we don't
	 * re-execute here. The RCCAL comparators (R2069_RCCAL_E/F) are
	 * written by the RC-cal engine during b43_radio_2069_init and later
	 * read by rccal to derive dev->phy.ac->lpf_cap0/1. Per-board values
	 * (see the profile) so each unit's real measurement is served.
	 */
	b43_test_mirror_radio_set(0x0414, p->rccal_e);  /* R2069_RCCAL_E */
	b43_test_mirror_radio_set(0x0415, p->rccal_f);  /* R2069_RCCAL_F */

	/*
	 * R2069 0x040b (probably an enable/misc control): the vendor finds it
	 * with bit 0 set at b43_phy_ac_radio_percore_setup_1 (after
	 * rfseq_tbl_init). d6220 ch36 vendor: RAD.RD -> 0x0169 (or similar with
	 * bit 0 = 1), then maskset clear bit 0 -> RAD.WR 0x0168. The same
	 * observed pre-state is seeded here.
	 */
	b43_test_mirror_radio_set(0x040b, 0x0169);

	/*
	 * R2069 0x001a/0x021a (per-core radio control): hardware default =
	 * 0x0004 (bit 2 set). The vendor reads this value at the start of
	 * b43_phy_ac_radio_percore_setup_1 (d6220 ch36 #34708-#34719 for core
	 * 0, #34729-#34740 for core 1). Deduced from the out-of-mask bits of
	 * the observed masksets' final RAD.WRs.
	 */
	b43_test_mirror_radio_set(0x001a, 0x0004);
	b43_test_mirror_radio_set(0x021a, 0x0004);

	/*
	 * R2069 0x001e/0x021e = 0x0014 (per-core radio, board HW default). The
	 * vendor reaches #41190 (B2f of txcal_setup) with 0x0014 already in the
	 * register; the port does not touch 0x001e earlier, so the pre-seed is
	 * needed for B2f's maskset ~0x0004, 0 to produce WR = 0x0010 (= 0x0014 &
	 * 0xfffb).
	 */
	b43_test_mirror_radio_set(0x001e, 0x0014);
	b43_test_mirror_radio_set(0x021e, 0x0014);

	/*
	 * R2069 0x0033/0x0233/0x0433 (per-core radio 0x0033 shadow): hardware
	 * default = 0x0161 for cores 0/1, 0x0160 for core 2 (d6220 only, the
	 * agcombo shows 0x0161 for core 2 too). Deduced from the maskset's final
	 * RAD.WRs at #34760/#34765/#34770. Bit 0 differing for core 2 on the
	 * d6220 suggests a silicon fuse or a factory default for inactive cores.
	 */
	b43_test_mirror_radio_set(0x0033, 0x0161);
	b43_test_mirror_radio_set(0x0233, 0x0161);
	b43_test_mirror_radio_set(0x0433, 0x0160);

	/*
	 * Per-core radio afecal registers (0x06ea/0x08ea/0x0aea + stride): the
	 * vendor at #38081-#38083 (core 0), #38118-#38120 (core 1), #... emits
	 * RAD.MOD ~0x0080, 0x0080 and WR val=0x00bf. The HW pre-state has bits
	 * 0-5 set (0x003f), bit 7 clear.
	 *
	 * Radio afecal 0x0121/0x0321 (per-core): HW pre-state 0x2000 (bit 13 set).
	 */
	b43_test_mirror_radio_set(0x06ea, 0x003f);
	b43_test_mirror_radio_set(0x08ea, 0x003f);
	b43_test_mirror_radio_set(0x0aea, 0x003f);
	b43_test_mirror_radio_set(0x0121, 0x2000);
	b43_test_mirror_radio_set(0x0321, 0x2000);

	/*
	 * R2069 0x004e/0x024e (per-core RF gain block, bit 15 set at HW POR).
	 * Vendor #38385-#38387 (core 0), #38414-#38416 (core 1): RAD.MOD ~0x0e00,
	 * 0 and WR val=0x8000. The HW pre-state has bit 15 set (0x8000).
	 */
	b43_test_mirror_radio_set(0x004e, 0x8000);
	b43_test_mirror_radio_set(0x024e, 0x8000);

	/*
	 * Radio pre-state for tempsense_radio_setup (vendor #39641-#39708).
	 * Derived from the MOD+RD+WR triplets: the WR emits (cur & mask) | set.
	 *
	 * Only the registers WITHOUT earlier vendor ops have a static pre-seed
	 * here -- 0x0017/0x0217 have RAD.MOD/WR ops at #34720-#34722 and
	 * #38296-#38298 (val=0x0000) that pre-seeds would break. The value
	 * 0x0010 seen at #39657 (giving WR=0x0011 after setting bit 0) comes from
	 * a HW auto-set between #38298 and #39657, not from traced ops. In the
	 * test framework that WR emits 0x0001 instead of 0x0011 -- a divergence
	 * that cannot be avoided without modelling the HW side effect.
	 */
	b43_test_mirror_radio_set(0x0161, 0x0100);
	b43_test_mirror_radio_set(0x0361, 0x0100);
	b43_test_mirror_radio_set(0x0024, 0x0003);
	b43_test_mirror_radio_set(0x0224, 0x0003);

	/* Board-specific read overrides (win over the shared seeds above). */
	register_board_read_plans(p);

	if (!strcmp(flow, "rxiq_est_debug")) {
		register_rxiq_read_plans();
		b43_phy_ac_rxiqcal_est_debug(&g_wldev);
	} else if (!strcmp(flow, "rxiq_comp")) {
		/*
		 * Measurement->coefficient vectors from the agcombo
		 * rescan-to-bss-ch36 capture (retval): per core, rounds 1 and 2 of
		 * the accumulators (it4-it9 of the window #29845-#31965).
		 * Expected output (vendor #32043-#32048):
		 *   0x06a0=0x03fd 0x06a1=0x006e
		 *   0x08a0=0x0052 0x08a1=0x003c
		 *   0x0aa0=0x0092 0x0aa1=0x005c
		 */
		static const u16 acc_06c0[] = { 0x2b25, 0x8c91 };
		static const u16 acc_06c1[] = { 0xfffb, 0x0008 };
		static const u16 acc_06c2[] = { 0x0df4, 0x1d2a };
		static const u16 acc_06c3[] = { 0x02d7, 0x02d8 };
		static const u16 acc_06c4[] = { 0xceef, 0x6971 };
		static const u16 acc_06c5[] = { 0x037a, 0x037d };
		static const u16 acc_08c0[] = { 0x089d, 0x3fa2 };
		static const u16 acc_08c1[] = { 0xffc1, 0xffc6 };
		static const u16 acc_08c2[] = { 0xbee8, 0x721e };
		static const u16 acc_08c3[] = { 0x02f3, 0x02f1 };
		static const u16 acc_08c4[] = { 0xc11c, 0xbd95 };
		static const u16 acc_08c5[] = { 0x0352, 0x0350 };
		static const u16 acc_0ac0[] = { 0x3b1d, 0xf6da };
		static const u16 acc_0ac1[] = { 0xffb0, 0xffb4 };
		static const u16 acc_0ac2[] = { 0x3f95, 0x8e52 };
		static const u16 acc_0ac3[] = { 0x021e, 0x021e };
		static const u16 acc_0ac4[] = { 0x453a, 0x7f05 };
		static const u16 acc_0ac5[] = { 0x0290, 0x028e };
		b43_test_plan_phy_reads(0x06c0, acc_06c0, 2);
		b43_test_plan_phy_reads(0x06c1, acc_06c1, 2);
		b43_test_plan_phy_reads(0x06c2, acc_06c2, 2);
		b43_test_plan_phy_reads(0x06c3, acc_06c3, 2);
		b43_test_plan_phy_reads(0x06c4, acc_06c4, 2);
		b43_test_plan_phy_reads(0x06c5, acc_06c5, 2);
		b43_test_plan_phy_reads(0x08c0, acc_08c0, 2);
		b43_test_plan_phy_reads(0x08c1, acc_08c1, 2);
		b43_test_plan_phy_reads(0x08c2, acc_08c2, 2);
		b43_test_plan_phy_reads(0x08c3, acc_08c3, 2);
		b43_test_plan_phy_reads(0x08c4, acc_08c4, 2);
		b43_test_plan_phy_reads(0x08c5, acc_08c5, 2);
		b43_test_plan_phy_reads(0x0ac0, acc_0ac0, 2);
		b43_test_plan_phy_reads(0x0ac1, acc_0ac1, 2);
		b43_test_plan_phy_reads(0x0ac2, acc_0ac2, 2);
		b43_test_plan_phy_reads(0x0ac3, acc_0ac3, 2);
		b43_test_plan_phy_reads(0x0ac4, acc_0ac4, 2);
		b43_test_plan_phy_reads(0x0ac5, acc_0ac5, 2);

		int r = b43_phy_ac_rxiqcal_comp_update(&g_wldev, 0x07);
		fprintf(stderr, "test: rx_iq_comp_update returned %d\n", r);
	} else if (!strcmp(flow, "op_init")) {
		run_op_init();
	} else if (!strcmp(flow, "up")) {
		/*
		 * The up half of a sweep cycle, from the segment boundary on.
		 *
		 * Same reason the down phase has its own flow: switch_analog()
		 * calls rx_gain_regs_program(), whose eleven reads happen before
		 * a segment starts, so running them here takes the oracle queue
		 * entries for 0x073a, 0x0725 and their neighbours that the up
		 * phase itself needs. Everything after the radio init is the
		 * same as "full".
		 */
		/*
		 * A warm segment is not a first bring-up and the vendor's
		 * init_regs takes its two-pass branch there, so do_full_init has
		 * to be clear before op_init and not just before
		 * switch_channel. A cold segment is the opposite, and
		 * AC_FIRST_INIT picks between them: the cold sweep has one
		 * module load per channel, so both cases are now captured.
		 */
		{
			const char *e = getenv("AC_FIRST_INIT");

			g_wldev.phy.do_full_init = e && strtoul(e, NULL, 10);
		}
		run_rfkill();
		run_op_init();
		run_switch_channel();
	} else if (!strcmp(flow, "down")) {
		/*
		 * The down phase on its own: the radio init that heads a sweep
		 * segment, episodes 72 to 232 of an up cycle. 130 ops.
		 *
		 * It gets its own flow because the read oracle is a per-address
		 * queue and every flow that reaches this code emits a save-gain
		 * prologue first. Those reads touch 0x0728, 0x0720 and 0x0408
		 * and take the queue entries the down phase itself needs, so the
		 * phase then reads the *second* value for each and every
		 * read-modify-write after it lands one field off. Comparing the
		 * phase in isolation is the only way its reads line up with the
		 * capture.
		 */
		g_ac.status_mask = 0;
		/*
		 * set_channel caches these and this flow does not run it, so the
		 * chanspec and the MAC bandwidth write need them seeded from
		 * AC_CHANNEL and AC_BW.
		 */
		g_ac.cal_channel = g_chan.hw_value;
		g_ac.cal_width = g_hw.conf.chandef.width;
		g_ac.cal_freq = g_hw.conf.chandef.center_freq1;
		/*
		 * The caller writes the chanspec, and with it the MAC width,
		 * just before the radio init rather than inside it: in the
		 * attach capture that carries the OBJ class the chanspec is at
		 * episode 35395 and the first op of b43_radio_2069_init() at
		 * 35396, with the prefregs at 35420. The driver writes it from
		 * b43_phy_ac_op_software_rfkill(), which this flow does not
		 * run: keep the two in step.
		 */
		b43_phy_ac_write_chanspec(&g_wldev);
		{
			static const u16 rccal_stat[] = {
				0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
				0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
				0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0010,
			};
			b43_test_plan_radio_reads(0x0413, rccal_stat,
						  ARRAY_SIZE(rccal_stat));
		}
		b43_radio_2069_init(&g_wldev);
	} else if (!strcmp(flow, "rfkill")) {
		run_rfkill();
	} else if (!strcmp(flow, "switch_channel")) {
		/*
		 * On its own this flow models a channel change at runtime, not a
		 * bring-up: the reference vendor slice (32887:55154) has the MAC
		 * active on entry and sees it suspended and re-enabled. In `full`
		 * the value is not touched: it gets there at 1 from the preamble,
		 * which turns the MAC off with a direct write.
		 */
		g_wldev.mac_suspended = 0;
		run_switch_channel();
	} else if (!strcmp(flow, "periodic")) {
		/*
		 * A steady-state watchdog tick, with the measure block round (noise
		 * cal). Reference: d6220 sweep, tick #33124-#33879 of cold01 (ch36
		 * BW20, after the segment's first up), extracted to router-data/d6220/
		 * wl-diag-wl1-steady-tick-ch36-bw20.txt -- a file that serves both as
		 * AC_READ_ORACLE (the SHM/TSSI values are ucode state, not derivable)
		 * and as the reference for compare.py.
		 *
		 * State on entry: MAC active (like switch_channel), PHY released --
		 * the tempsense's REQUIRE wants RX_WAITED|RX_OFDM and no
		 * CLIP_ALL_DIS/CCA_RESET/RX_CCK.
		 *
		 * The toggle of 0x0520[3:2] is part of the state on entry like the
		 * other two, and its phase is a property of the extract, not a
		 * driver constant: the alternation starts from 0x0000 at the
		 * segment's first mode change and never resets (26 changes on
		 * cold01, the alternation whole and unbroken), so a mid-stream tick's
		 * value is decided by its index's parity. The extracted tick is the
		 * tenth and writes 0x0004. An extract taken on another channel falls
		 * on another index, because how many calibrations precede it changes
		 * with the family, and there the phase has to be recounted on the
		 * segment.
		 */
		g_wldev.mac_suspended = 0;
		g_ac.status_mask = B43_PHY_AC_STATE_RX_WAITED |
				   B43_PHY_AC_STATE_RX_OFDM;
		g_ac.probe_mode = 0x0004;
		g_ac.wd_turns = 9;
		g_ac.wd_switch_turns = 9;
		/*
		 * The CRS state the segment carries into this tick: the block
		 * last written at #31414 (0x34, bank 0x0500) from chain 0 at
		 * ladder index 1 and chain 1 at 3, and the five latches since
		 * (#32019-#33110) all on the same two indices. The tick's own
		 * sample lands there too, so the block is not rewritten;
		 * without this state the rings are empty and the sample alone
		 * reads as a move.
		 */
		seed_crs_rings(1);
		seed_crs_chain(1, 3);
		b43_phy_ac_watchdog(&g_wldev);
		/*
		 * The window's latch is not the tail of the round: it comes with
		 * the noise sample, 2 ms later and on another CPU, and in the cold
		 * flow the timeline's NOISE event carries it. Here there is no
		 * timeline and the flow delivers it, or the extract's last eight ops
		 * have nobody to emit them.
		 */
		b43_phy_ac_noise_sample_done(&g_wldev);
	} else if (!strcmp(flow, "crsmin")) {
		/*
		 * Self-test of the crsmin chain (uses no oracle). It runs
		 * pwork_60sec three times at ch36 BW20, index 1, ladder entry 48.
		 * Every board here is PHY rev 1, where the +4 bump stays after the
		 * first two calibrations: cycle 0 writes 52 (0x34, the observed
		 * attach value), and cycles 1 and 2, past and at the end of the
		 * cold count, compute 52 again.
		 *
		 * Those two must skip the write instead of re-emitting the eight
		 * registers: the vendor rewrites only when the threshold moves.
		 * Reading the mirror would not tell the two apart -- the value is the
		 * same either way -- so before each a sentinel is seeded and checked
		 * to survive. A bump dropped after the cold count writes 48 (0x30)
		 * over it on cycle 2.
		 */
		static const u8 want[3] = { 0x34, 0xff, 0xff };
		unsigned int k;
		int fails = 0;

		/*
		 * The state the chain would have left, which this flow does not
		 * produce: the two things AC_CRS_INDEX and AC_CRS_SUBBAND seed for
		 * the other flows. The channel is there -- the preamble sets it --
		 * but the sub-band in force starts from the 0xff sentinel, "never
		 * seen", which forces the index reset on the first round, and the
		 * index is moved by the noise sample, which the watchdog latches:
		 * this flow does not run it. Without the two, the chain would run on
		 * index 0 and the self-test fail all three cycles, measuring the
		 * ladder's first step while the expectations are the second's.
		 *
		 * Zero is ch36's pa5g group with subband5gver=4, the sub-band the
		 * channel setup would have left in force.
		 */
		g_ac.crs_subband = 0;
		seed_crs_rings(1);
		g_ac.cal_cycles = 0;

		for (k = 0; k < 3; k++) {
			u8 got;

			if (k > 0)
				b43_test_mirror_phy_set(0x0324, 0x00ff);

			b43_phyops_ac.pwork_60sec(&g_wldev);
			got = b43_test_mirror_phy_get(0x0324) & 0xff;
			fprintf(stderr,
				"crsmin: cycle %u  CRS low byte=0x%02x expected=0x%02x  %s%s\n",
				k, got, want[k], got == want[k] ? "OK" : "FAIL",
				k > 0 ? "  (sentinel: no write)" : "");
			if (got != want[k])
				fails++;
		}
		fprintf(stderr, "crsmin: %s\n", fails ? "FAIL" : "PASS");
		return fails ? 1 : 0;
	} else if (!strcmp(flow, "full")) {
		run_full();
	} else {
		fprintf(stderr, "test: unknown flow '%s'\n", flow);
		return 2;
	}

	fprintf(stderr, "test: plan consumption ---\n");
	b43_test_plans_report(stderr);
	b43_test_oracle_coverage_report();
	b43_test_oracle_report();
	return 0;
}
