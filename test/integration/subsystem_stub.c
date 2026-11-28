// SPDX-License-Identifier: GPL-2.0
/*
 * The rest of the layer below: bcma control, ssb, mac80211 and the b43
 * subsystems that emit no ops.
 *
 * Compiled with the KERNEL's flags, not as user code, and not by taste: the
 * real types are needed here. `ieee80211_alloc_hw_nm` must return a `struct
 * ieee80211_hw` whose `priv` is aligned as in the kernel, because b43 puts
 * its `b43_wl` there and reads it back with `hw_to_b43_wl()`. With an
 * invented structure the pointer would land elsewhere and the defect would
 * show a thousand ops later.
 *
 * What emits ops and what does not, so as not to look for it later: the ops
 * go ONLY through bcma_stub.c's vtable. The ten `bcmn_*` symbols below are
 * clock, enable, GPIO and PLL, and they do not appear in the vendor's trace
 * -- wl's tracer hooks the access functions, not the core management ones. So
 * a no-op is the right answer, not a shortcut.
 *
 * `ssbn_*` are no-ops because only BCMA is needed here: b43 compiles with
 * CONFIG_B43_NEXT_BCMA and the ssb branch is not taken, but the link asks for
 * them anyway.
 */
#include <linux/platform_device.h>
#include <bcma-next/bcmn.h>
#include <ssb-next/ssbn.h>
#include <linux/etherdevice.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <net/mac80211.h>

#include "trace_out.h"
#include "chip_link.h"

/* --- bcma: the PCI host side, through the configuration space ------- */

void bcmn_host_pci_up(struct bcmn_bus *bus) { }
void bcmn_host_pci_down(struct bcmn_bus *bus) { }
int bcmn_host_pci_irq_ctl(struct bcmn_bus *bus, struct bcmn_device *core,
			  bool enable)
{
	return 0;
}

/* --- ChipCommon and PMU ---------------------------------------------------
 *
 * They are bcma's real code (driver_chipcommon.c, driver_chipcommon_pmu.c,
 * driver_pcie2.c): their accesses go through the bus stub like the 802.11
 * core's, and come out as CC.RD/CC.WR and PCIE.RD/PCIE.WR at the bus, the
 * same class reverse-tools/mmio2ops.py gives the capture.
 */

/* What bcma's code asks of the rest of bcma and of the kernel. */
bool bcmn_wait_value(struct bcmn_device *core, u16 reg, u32 mask, u32 value,
		     int timeout)
{
	int i;

	for (i = 0; i < 100; i++)
		if ((bcmn_read32(core, reg) & mask) == value)
			return true;
	return false;
}

int bcmn_sflash_init(struct bcmn_drv_cc *cc) { return -EOPNOTSUPP; }
int pcie_set_readrq(struct pci_dev *dev, int rq) { return 0; }
struct platform_device *
platform_device_register_full(const struct platform_device_info *pdevinfo)
{
	return ERR_PTR(-ENODEV);
}

/* --- ssb: not taken, but the link asks for it ------------------------- */

int ssbn_bus_powerup(struct ssbn_bus *bus, bool dynamic_pctl) { return 0; }
int ssbn_bus_may_powerdown(struct ssbn_bus *bus) { return 0; }
u32 ssbn_dma_translation(struct ssbn_device *dev) { return 0; }
void ssbn_device_enable(struct ssbn_device *dev, u32 core_specific_flags) { }
void ssbn_device_disable(struct ssbn_device *dev, u32 core_specific_flags) { }
int ssbn_device_is_enabled(struct ssbn_device *dev) { return 1; }
void ssbn_commit_settings(struct ssbn_bus *bus) { }
void ssbn_driver_unregister(struct ssbn_driver *drv) { }
void ssbn_set_devtypedata(struct ssbn_device *dev, void *data) { }
int ssbn_pcicore_dev_irqvecs_enable(struct ssbn_pcicore *pc,
				   struct ssbn_device *dev)
{
	return 0;
}

/* --- mac80211: the only one that has to return something usable ------ */

struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len,
					   const struct ieee80211_ops *ops,
					   const char *requested_name)
{
	struct ieee80211_hw *hw;

	extern const struct ieee80211_ops *b43_test_hw_ops;
	extern struct ieee80211_hw *b43_test_hw;

	/*
	 * In the kernel `priv` follows the structure, aligned; here the same is
	 * done with a single allocation, so `hw->priv` is a valid pointer and
	 * b43 writes its b43_wl there.
	 */
	hw = kzalloc(sizeof(*hw) + priv_data_len, GFP_KERNEL);
	if (!hw)
		return NULL;
	hw->priv = (char *)hw + sizeof(*hw);
	hw->wiphy = kzalloc(sizeof(*hw->wiphy), GFP_KERNEL);
	b43_test_hw_ops = ops;
	b43_test_hw = hw;
	return hw;
}

void ieee80211_free_hw(struct ieee80211_hw *hw)
{
	if (hw)
		kfree(hw->wiphy);
	kfree(hw);
}

/*
 * b43's vtable, captured as `__bcmn_driver_register` captures the probe:
 * b43_op_start is static, and without this the harness has no way to start
 * the bring-up after the probe.
 */
const struct ieee80211_ops *b43_test_hw_ops;
struct ieee80211_hw *b43_test_hw;

/*
 * register_hw chooses the channel, which in the kernel mac80211 does, not
 * b43: b43_phy_init() reads dev->wl->hw->conf.chandef.chan->hw_value, and if
 * nobody set that pointer the bring-up dies there.
 *
 * Channel and width come from the environment, `B43_CHANNEL` and `B43_BW`,
 * because the sweep's 26 segments are 26 configurations and a suite that can
 * only do the first does not measure them. They are the same two parameters
 * `../unit` takes as AC_CHANNEL and AC_BW; the prefix is B43_ to go with this
 * suite's B43_READ_ORACLE and B43_TRACE_OUT.
 *
 * The channel is not invented: it is looked for among the ones b43
 * registered in b43_setup_bands(), the same restriction mac80211 has. If
 * `B43_CHANNEL` is not among them the suite says so and takes the first,
 * instead of leaving a pointer to a channel the driver does not know.
 *
 * `center_freq1` for BW40 and BW80 is the centre of the bonded block, not of
 * the channel: it is what b43_phy_ac_write_chanspec() reads to compose the
 * chanspec. The formula is the same as ../unit/main.c's.
 */
static enum nl80211_chan_width shim_width(long bw)
{
	switch (bw) {
	case 40:  return NL80211_CHAN_WIDTH_40;
	case 80:  return NL80211_CHAN_WIDTH_80;
	default:  return NL80211_CHAN_WIDTH_20;
	}
}

/*
 * What cfg80211_chandef_usable() asks of the band before accepting a width:
 * 40 wants HT with SUP_WIDTH_20_40, 80 wants VHT. Without it hostapd does not
 * reach start_ap with that chandef, and a suite imposing it would measure a
 * configuration that does not exist on the hardware.
 */
static bool shim_width_usable(const struct ieee80211_supported_band *sb,
			      long bw)
{
	switch (bw) {
	case 40:
		return sb->ht_cap.ht_supported &&
		       (sb->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40);
	case 80:
		return sb->ht_cap.ht_supported && sb->vht_cap.vht_supported;
	default:
		return true;
	}
}

static struct ieee80211_hw *g_hw;

/*
 * The regdomain cfg80211 applies to the channels at registration. The cold
 * capture is of a wl with an empty ccode= in the NVRAM, with its internal
 * locale, whose ceilings are per bandwidth and are in
 * b43_phy_ac_locale_ceiling(). The channels' max_power is not lowered here:
 * a ceiling per 20 MHz channel, as cfg80211 expresses it, would also hold for
 * the 40 and 80 MHz blocks, where the vendor's locale is higher.
 */
static void apply_regdomain(struct ieee80211_supported_band *sb)
{
	unsigned int i;

	/* The radar duty, as cfg80211 marks it: U-NII-2A and U-NII-2C. */
	for (i = 0; i < sb->n_channels; i++) {
		u32 f = sb->channels[i].center_freq;

		if ((f >= 5260 && f <= 5320) || (f >= 5500 && f <= 5720))
			sb->channels[i].flags |= IEEE80211_CHAN_RADAR;
	}
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	long want = b43_test_env_long("B43_CHANNEL", 0);
	long bw = b43_test_env_long("B43_BW", 20);
	struct ieee80211_channel *first = NULL, *pick = NULL;
	enum nl80211_band band;
	int i;

	g_hw = hw;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sb = hw->wiphy->bands[band];

		if (!sb || !sb->n_channels)
			continue;
		apply_regdomain(sb);
		if (!first)
			first = &sb->channels[0];
		for (i = 0; i < sb->n_channels; i++) {
			if (sb->channels[i].hw_value == want) {
				pick = &sb->channels[i];
				break;
			}
		}
		if (pick)
			break;
	}

	if (!pick) {
		if (want)
			b43_trace_note("B43_CHANNEL=%d is not among the channels "
				       "b43 registered; using the first\n",
				       (int)want);
		pick = first;
	}
	if (!pick) {
		b43_trace_note("b43 registered no band%d\n", 0);
		return 0;
	}

	if (!shim_width_usable(hw->wiphy->bands[pick->band], bw)) {
		b43_trace_note("B43_BW=%d: the band does not announce the width, "
			       "cfg80211 would reject it\n", (int)bw);
		return -EINVAL;
	}

	hw->conf.chandef.chan = pick;
	hw->conf.chandef.width = shim_width(bw);
	hw->conf.chandef.center_freq1 = pick->center_freq +
		(bw == 40 ? 10 : bw == 80 ? 30 : 0);
	/*
	 * mac80211 hands the driver a power level with the channel, the
	 * channel's own max_power when userspace has not asked for less. Zero
	 * is the value b43_op_config() reads as "no power level", and on it
	 * the TX power check -- and adjust_txpower behind it -- never runs.
	 */
	hw->conf.power_level = pick->max_power;
	/*
	 * The retry limits mac80211 starts from, wiphy->retry_short/long as
	 * cfg80211 sets them; with zero b43_op_config() writes zero limits.
	 */
	hw->conf.short_frame_max_tx_count = 7;
	hw->conf.long_frame_max_tx_count = 4;
	return 0;
}
void ieee80211_unregister_hw(struct ieee80211_hw *hw) { }
void ieee80211_wake_queues(struct ieee80211_hw *hw) { }
void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue) { }
void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb) { }
void ieee80211_purge_tx_queue(struct ieee80211_hw *hw,
			      struct sk_buff_head *skbs) { }
void ieee80211_rx_list(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       struct sk_buff *skb, struct list_head *list) { }
void netif_receive_skb_list(struct list_head *head) { }
void skb_queue_purge_reason(struct sk_buff_head *list,
			    enum skb_drop_reason reason) { }
void ieee80211_handle_wake_tx_queue(struct ieee80211_hw *hw,
				    struct ieee80211_txq *txq) { }
/* The txqs the AC pulls frames from (b43_tx_pull()): empty. */
void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac) { }
struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	return NULL;
}
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw,
				     struct ieee80211_txq *txq) { return NULL; }
void __ieee80211_schedule_txq(struct ieee80211_hw *hw,
			      struct ieee80211_txq *txq, bool force) { }
/* No station and no block ack session: the captures have none. */
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif,
					 const u8 *addr) { return NULL; }
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *hw,
						   const u8 *addr,
						   const u8 *localaddr)
{
	return NULL;
}
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif,
				     const u8 *ra, u16 tid) { }
void __rcu_read_lock(void) { }
void __rcu_read_unlock(void) { }
/* Inline, like queue_work_on(): the suite is single-threaded, and the work
 * b43_phy_txpower_check() queues is adjust_txpower, ops to compare. */
void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	if (work && work->func)
		work->func(work);
}

/*
 * The delayed works are the driver's timers: the periodic work, the radar
 * poll. They are kept here with their expiry in jiffies, and
 * b43_test_run_until() fires them when main.c moves the clock forward. A work
 * already queued is not queued again, as in the kernel.
 */
#define SHIM_DWORKS	8

static struct {
	struct delayed_work *work;
	unsigned long due;
} dworks[SHIM_DWORKS];

/* Whether @work was pending; @mod moves the due time of a pending one. */
static bool shim_dwork_queue(struct delayed_work *work, unsigned long delay,
			     bool mod)
{
	int i, free = -1;

	for (i = 0; i < SHIM_DWORKS; i++) {
		if (dworks[i].work == work) {
			if (mod)
				dworks[i].due = jiffies + delay;
			return true;
		}
		if (!dworks[i].work && free < 0)
			free = i;
	}
	if (free < 0) {
		b43_trace_note("too many delayed works queued%d\n", 0);
		return false;
	}
	dworks[free].work = work;
	dworks[free].due = jiffies + delay;
	return false;
}

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct delayed_work *work,
				  unsigned long delay)
{
	shim_dwork_queue(work, delay, false);
}

bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			   struct delayed_work *work, unsigned long delay)
{
	return !shim_dwork_queue(work, delay, false);
}

bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			 struct delayed_work *work, unsigned long delay)
{
	return shim_dwork_queue(work, delay, true);
}

int b43_test_dwork_cancel(void *w)
{
	int i;

	for (i = 0; i < SHIM_DWORKS; i++) {
		if (dworks[i].work == w) {
			dworks[i].work = NULL;
			return 1;
		}
	}
	return 0;
}

bool cancel_delayed_work(struct delayed_work *work)
{
	return b43_test_dwork_cancel(work);
}

unsigned long __msecs_to_jiffies(const unsigned int m)
{
	return DIV_ROUND_UP((unsigned long)m * HZ, MSEC_PER_SEC);
}

/* The suite's clock is jiffies, which main.c moves on. */
ktime_t ktime_get(void)
{
	return (ktime_t)jiffies * (NSEC_PER_SEC / HZ);
}

/* Fire, in expiry order, the works that expire by @until. */
/* With the virtual chip, jiffies that pass are its time too. */
void b43_chip_deliver_irq(void);

static void chip_follow(unsigned long to)
{
	if (b43_chip_active() && time_after(to, jiffies)) {
		b43_chip_time((to - jiffies) * (1000000UL / HZ));
		b43_chip_deliver_irq();
	}
}

void b43_test_run_until(unsigned long until)
{
	for (;;) {
		struct delayed_work *w;
		int i, next = -1;

		for (i = 0; i < SHIM_DWORKS; i++)
			if (dworks[i].work && time_before_eq(dworks[i].due, until) &&
			    (next < 0 || time_before(dworks[i].due, dworks[next].due)))
				next = i;
		if (next < 0)
			break;
		w = dworks[next].work;
		chip_follow(dworks[next].due);
		if (time_after(dworks[next].due, jiffies))
			jiffies = dworks[next].due;
		dworks[next].work = NULL;
		w->work.func(&w->work);
	}
	chip_follow(until);
	if (time_after(until, jiffies))
		jiffies = until;
}

/*
 * The beacon mac80211 would give b43_update_templates(): header, SSID, rates
 * and TIM, what b43_write_beacon_template() reads for TIMBPOS and DTIMPER.
 * The SSID length is B43_SSID_LEN, like ../unit's AC_SSID_LEN, because it
 * goes into the template length.
 */
static struct sk_buff shim_beacon_skb;
static u8 shim_beacon[256];

struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 u16 *tim_offset, u16 *tim_length,
					 unsigned int link_id)
{
	static const u8 rates[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };
	/* mac80211's on 2.4 GHz: the CCK ones basic, the rest extended */
	static const u8 rates_2g[] = { 0x82, 0x84, 0x8b, 0x96,
				       0x0c, 0x12, 0x18, 0x24 };
	static const u8 ext_rates_2g[] = { 0x30, 0x48, 0x60, 0x6c };
	enum nl80211_band band = hw->conf.chandef.chan ?
				 hw->conf.chandef.chan->band :
				 NL80211_BAND_5GHZ;
	struct ieee80211_mgmt *m = (struct ieee80211_mgmt *)shim_beacon;
	long ssid_len = b43_test_env_long("B43_SSID_LEN", 7);
	struct ieee80211_tx_info *info;
	u8 *p;

	memset(shim_beacon, 0, sizeof(shim_beacon));
	m->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
				       IEEE80211_STYPE_BEACON);
	eth_broadcast_addr(m->da);
	memcpy(m->sa, vif->addr, ETH_ALEN);
	memcpy(m->bssid, vif->addr, ETH_ALEN);
	m->u.beacon.beacon_int = cpu_to_le16(vif->bss_conf.beacon_int);
	m->u.beacon.capab_info = cpu_to_le16(WLAN_CAPABILITY_ESS);
	p = m->u.beacon.variable;
	*p++ = WLAN_EID_SSID;
	*p++ = (u8)ssid_len;
	memset(p, 'X', ssid_len);
	p += ssid_len;
	*p++ = WLAN_EID_SUPP_RATES;
	if (band == NL80211_BAND_2GHZ) {
		*p++ = sizeof(rates_2g);
		memcpy(p, rates_2g, sizeof(rates_2g));
		p += sizeof(rates_2g);
	} else {
		*p++ = sizeof(rates);
		memcpy(p, rates, sizeof(rates));
		p += sizeof(rates);
	}
	*p++ = WLAN_EID_TIM;
	*p++ = 4;
	*p++ = 0;				/* DTIM count */
	*p++ = vif->bss_conf.dtim_period;
	*p++ = 0;
	*p++ = 0;
	if (band == NL80211_BAND_2GHZ) {
		*p++ = WLAN_EID_EXT_SUPP_RATES;
		*p++ = sizeof(ext_rates_2g);
		memcpy(p, ext_rates_2g, sizeof(ext_rates_2g));
		p += sizeof(ext_rates_2g);
	}

	memset(&shim_beacon_skb, 0, sizeof(shim_beacon_skb));
	shim_beacon_skb.data = shim_beacon;
	shim_beacon_skb.len = p - shim_beacon;
	info = IEEE80211_SKB_CB(&shim_beacon_skb);
	info->band = band;
	info->control.rates[0].idx = 0;
	return &shim_beacon_skb;
}

/*
 * The channel-context ops b43 takes from mac80211's emulation. mac80211
 * calls them; the harness drives config directly and never does.
 */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_chanctx_conf *ctx)
{
	return 0;
}

void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx)
{
}

void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx,
				      u32 changed)
{
}

int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif_chanctx_switch *vifs,
					 int n_vifs,
					 enum ieee80211_chanctx_switch_mode mode)
{
	return 0;
}

void ieee80211_radar_detected(struct ieee80211_hw *hw,
			      struct ieee80211_chanctx_conf *chanctx_conf)
{
	b43_trace_note("ieee80211_radar_detected()%.0d\n", 0);
}
const struct ieee80211_rate *
ieee80211_get_response_rate(struct ieee80211_supported_band *sband,
			    u32 basic_rates, int bitrate)
{
	struct ieee80211_rate *rate = NULL;
	int i;

	if (!sband || !sband->bitrates)
		return NULL;
	/* mac80211's: the highest basic rate not above @bitrate. */
	for (i = 0; i < sband->n_bitrates; i++) {
		struct ieee80211_rate *r = &sband->bitrates[i];

		if (!(basic_rates & BIT(i)) || r->bitrate > bitrate)
			continue;
		if (!rate || rate->bitrate < r->bitrate)
			rate = r;
	}
	return rate ? rate : &sband->bitrates[0];
}

/* --- b43's subsystems -----------------------------------------------------
 *
 * dma.c is compiled as it is, with dma_stub.c below: it programs the rings
 * and serves the frames the timeline delivers with the IRQ events. PIO is
 * the other data path, which b43 does not take on PCIe: init has to succeed
 * anyway or b43_wireless_core_init leaves before b43_security_init.
 */
struct b43_wldev;

int b43_pio_init(struct b43_wldev *dev) { return 0; }
void b43_pio_free(struct b43_wldev *dev) { }
void b43_pio_tx(struct b43_wldev *dev, struct sk_buff *skb) { }
void b43_pio_rx(void *q) { }
bool b43_pio_tx_empty(struct b43_wldev *dev) { return true; }

/*
 * leds.c and rfkill.c are compiled: below them are the LED core and the
 * mac80211 triggers, which are not here. The registration succeeds and the
 * triggers have a name, because with a null name b43_register_led() returns
 * -EINVAL and the LED path -- the mask for b43_gpio_init() and the
 * GPIO_CONTROL writes in b43_leds_init() -- does not run at all. The LED core
 * never calls brightness_set back: no trigger fires without traffic.
 */
int led_classdev_register_ext(struct device *parent,
			      struct led_classdev *led_cdev,
			      struct led_init_data *init_data) { return 0; }
void led_classdev_unregister(struct led_classdev *led_cdev) { }
const char *__ieee80211_get_tx_led_name(struct ieee80211_hw *hw) { return "tx"; }
const char *__ieee80211_get_rx_led_name(struct ieee80211_hw *hw) { return "rx"; }
const char *__ieee80211_get_assoc_led_name(struct ieee80211_hw *hw) { return "assoc"; }
const char *__ieee80211_get_radio_led_name(struct ieee80211_hw *hw) { return "radio"; }
void wiphy_rfkill_set_hw_state_reason(struct wiphy *wiphy, bool blocked,
				      enum rfkill_hard_block_reasons reason) { }

/*
 * Suspend and resume of the TX queues, and the TX state: data path, no op on
 * the bring-up. `handle_txstatus` is not called at all here, since there is
 * no transmission.
 */
void b43_pio_tx_suspend(struct b43_wldev *dev) { }
void b43_pio_tx_resume(struct b43_wldev *dev) { }
void b43_pio_handle_txstatus(struct b43_wldev *dev, const void *status) { }

/* --- mac80211: pure utilities, none emits ops ------------------------- */

unsigned int ieee80211_hdrlen(__le16 fc) { return 24; }
u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band)
{
	/* 5 GHz: mac80211's formula, not an invented value. */
	if (band == NL80211_BAND_5GHZ)
		return (5000 + chan * 5) * 1000;
	return 0;
}
int ieee80211_freq_khz_to_channel(u32 freq)
{
	freq = KHZ_TO_MHZ(freq);
	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return (freq - 2407) / 5;
	if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	if (freq < 5925)
		return (freq - 5000) / 5;
	return 0;
}
struct ieee80211_channel *ieee80211_get_channel_khz(struct wiphy *wiphy,
						    u32 freq)
{
	enum nl80211_band band;
	int i;

	if (!g_hw)
		return NULL;
	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sb = g_hw->wiphy->bands[band];

		if (!sb)
			continue;
		for (i = 0; i < sb->n_channels; i++)
			if (sb->channels[i].center_freq * 1000 == freq)
				return &sb->channels[i];
	}
	return NULL;
}
void ieee80211_rts_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       const void *frame, size_t frame_len,
		       const struct ieee80211_tx_info *frame_txctl,
		       struct ieee80211_rts *rts) { }
void ieee80211_ctstoself_get(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, const void *frame,
			     size_t frame_len,
			     const struct ieee80211_tx_info *frame_txctl,
			     struct ieee80211_cts *cts) { }
__le16 ieee80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					enum nl80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	return 0;
}
void ieee80211_get_tkip_p1k_iv(struct ieee80211_key_conf *keyconf,
			       u32 iv32, u16 *p1k) { }
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta,
		       struct sk_buff *skb, struct napi_struct *napi) { }

/*
 * Stations, their power save and the frames sent to them: the captures
 * have no station, and b43 here is an AP.
 */
void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw,
				       void (*iterator)(void *data,
							struct ieee80211_sta *sta),
				       void *data) { }
void ieee80211_sta_set_buffered(struct ieee80211_sta *sta, u8 tid,
				bool buffered) { }
void ieee80211_sta_eosp(struct ieee80211_sta *pubsta) { }
void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
			     unsigned long *frame_cnt, unsigned long *byte_cnt)
{
	if (frame_cnt)
		*frame_cnt = 0;
	if (byte_cnt)
		*byte_cnt = 0;
}
void ieee80211_get_tx_rates(struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct sk_buff *skb,
			    struct ieee80211_tx_rate *dest, int max_rates)
{
	int i;

	for (i = 0; i < max_rates; i++)
		dest[i].idx = -1;
}
void ieee80211_tx_rate_update(struct ieee80211_hw *hw,
			      struct ieee80211_sta *pubsta,
			      struct ieee80211_tx_info *info) { }
bool ieee80211_tx_prepare_skb(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, struct sk_buff *skb,
			      int band, struct ieee80211_sta **sta)
{
	return false;
}
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       int link_id, bool qos_ok) { return NULL; }
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif) { return NULL; }
const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
			 const u8 *match, unsigned int match_len,
			 unsigned int match_offset) { return NULL; }
/* cfg80211's default: no power save timeout set */
static struct wireless_dev shim_wdev = { .ps_timeout = -1 };

struct wireless_dev *ieee80211_vif_to_wdev(struct ieee80211_vif *vif)
{
	return &shim_wdev;
}

/*
 * The vtables of the PHYs the suite does not compile: NULL, and not out of
 * laziness. b43_phy_allocate() dispatches on phy->type, so it takes only the
 * board's AC or HT branch: if it ever took another branch the null pointer
 * would show it at once, instead of running a fake vtable and producing a
 * plausible trace of the wrong PHY.
 */
const struct b43_phy_operations *b43_phyops_g;
const struct b43_phy_operations *b43_phyops_n;
const struct b43_phy_operations *b43_phyops_lp;

/*
 * The work items run inline, they are not queued.
 *
 * Not a convenience: b43_bcma_probe() requests the firmware from a work --
 * INIT_WORK(&wl->firmware_load, b43_request_firmware) plus schedule_work --
 * and with a no-op queue_work that work never runs. dev->fw.ucode.data stays
 * null and b43_upload_microcode() dies dereferencing it, inside the bring-up
 * and not in the probe, far from the cause.
 *
 * Running it here is the same reasoning as the locks: the suite is
 * single-threaded, and if the work really ran in parallel the trace would no
 * longer be ordered and the positional comparison would make no sense. The
 * difference from the kernel is that there the work starts after the probe
 * has returned, here it starts inside schedule_work(): if a phase depended
 * on that order it would show as ops out of place, not as a crash.
 */
bool queue_work_on(int cpu, struct workqueue_struct *wq,
		   struct work_struct *work)
{
	if (work && work->func)
		work->func(work);
	return true;
}
