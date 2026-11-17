/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_FW_LAYOUT_H_
#define B43_FW_LAYOUT_H_

#include <linux/types.h>

struct b43_wldev;
struct b43_txstatus;
struct b43_txhdr_ampdu;
struct b43_plcp_hdr6;
struct sk_buff;
struct ieee80211_tx_info;
struct ieee80211_tx_rate;
struct ieee80211_rx_status;
struct ieee80211_supported_band;

/*
 * Where a microcode takes its beacon templates in template RAM, and what goes
 * in front of the frame there.
 *
 * The bases are constants of the microcode, not of the core: the AC builds
 * load them as immediates right before the template engine is started, those
 * of 784 and those of 832, which 928 keeps. Each beacon has the room up to the
 * next template, header included.
 *
 * In front of the frame the pre-AC microcode takes the 6 byte PLCP header. The
 * AC one takes 12 bytes with the PLCP at byte 3, the rest zero (the beacon of
 * the agcombo bus capture, 928), and counts them in the template length and
 * in the TIM position (0x3c + SSID length on 784 and 928).
 *
 * TODO: probe-response offload. Its template follows the beacons, at 0x04d8
 * in 784 and 0x0700 in 832 and 928, and the stock driver rewrites it after
 * every beacon; b43 uploads none and leaves the probe requests to mac80211.
 * See docs/retrace-todo.md.
 */
struct b43_tpl_layout {
	u16 bcn_base[2];
	u16 bcn_size;
	u8 hdr_len;
	u8 plcp_off;		/* PLCP header offset in the template, OFDM rates */
	u8 plcp_off_cck;	/* the same for CCK rates */
};

/*
 * What b43 writes to the shared memory of an AC microcode where the
 * microcodes differ: the offsets, in bytes, and the values and order their
 * own drivers use.
 */
struct b43_shm_layout {
	u16 mac_cfg;		/* first cell of the MAC config block */
	u16 mac_cfg_val;	/* and what goes in it */
	bool mac_cfg_0020;	/* 0x0020 = 0x0800 before the block */
	bool mac_cfg_16;	/* the seven words from mac_cfg + 0x16 */
	u16 cts_duration;	/* us, of the CTS-to-self of the RX-cal suspend */
	bool cts_suspended;	/* armed with the MAC already suspended */
	u16 txerr;		/* latch of the saved TX fault; 0 if none known */
};

/*
 * A microcode's TX and RX headers and TX status. Offsets in bytes; the tx_
 * ones are the pre-AC header's. rx_rate and txstatus_rates are NULL where
 * the pre-AC code applies.
 */
struct b43_fw_hdr {
	int (*gen_txhdr)(struct b43_wldev *dev, u8 *txhdr,
			 struct sk_buff *skb_frag,
			 struct ieee80211_tx_info *info, u16 cookie,
			 const struct b43_txhdr_ampdu *ampdu);
	int (*rx_rate)(struct b43_plcp_hdr6 *plcp, u16 phystat0, bool rx_5ghz,
		       struct ieee80211_rx_status *status);
	void (*txstatus_poll)(struct b43_wldev *dev);
	void (*txstatus_drain)(struct b43_wldev *dev);
	void (*txstatus_rates)(struct ieee80211_tx_info *report,
			       const struct b43_txstatus *status);
	bool txstatus_raw;	/* the status words kept in its raw[] */
	bool tx_epoch;	/* TOE prefix bit bounds TX aggregate contexts */
	u8 tx_len;
	u8 tx_cookie;
	u8 tx_rts_plcp;
	u8 tx_rts_frame;
	u8 tx_plcp;
	bool tx_ppdu_plcp;	/* PLCP of the PPDU: HT bits, A-MPDU preload */
	bool tx_rates_cut;	/* rates it cannot carry cut, not refused */
	bool tx_stbc;		/* STBC on HT rates of one stream */
	bool tx_ldpc;		/* LDPC on HT and VHT rates */
	u8 tx_preload;		/* with tx_ppdu_plcp */
	u8 rx_len;		/* as PIO reads it; 0 if none is known */
	u8 rx_frame_offset;
	u16 rx_buffersize;
	u8 rx_mac_status;
	u8 rx_mac_time;
	u8 rx_channel;
	bool rx_amsdu;		/* A-MSDUs flagged in the MAC status */
	bool rx_amsdu_ht_only;	/* rx_amsdu only under N-PHY HT */
	bool rx_ht_plcp;	/* the HT-SIG of a pre-standard N frame read */
};

extern const struct b43_fw_hdr b43_hdr_351;
extern const struct b43_fw_hdr b43_hdr_410;
extern const struct b43_fw_hdr b43_hdr_598;
extern const struct b43_fw_hdr b43_ac_hdr_784;

/* Everything b43 decides from the microcode it runs. */
struct b43_fw_layout {
	const struct b43_fw_hdr *hdr;
	const struct b43_tpl_layout *tpl;	/* NULL if not known */
	const struct b43_shm_layout *shm;	/* NULL if none or not known */
	u16 keyidxblock;	/* key index/algorithm block */
	bool new_kidx_api;	/* key indexes not shifted past the TX keys */
	bool machw_bit31;	/* MAC_HW_CAP bit 31 passed to the microcode */
	bool edcrs;		/* energy-detect carrier sense in IFSCTL1 */
	bool g_chanid_number;	/* G-PHY RX channel ID a number, not MHz - 2400 */
	bool kidx_empty_zero;	/* an empty key index slot is all zero */
	bool sec_algo_d11;	/* cipher numbers as d11.h's WSEC_ALGO_* */
	bool hw_tkip;		/* TKIP in hardware, under modparam_hwtkip */
	bool rcmta_count;	/* told the number of RCMTA slots */
	bool phytxctl_shm;	/* the pre-AC PHY TX control words in SHM */
	bool hf_usedefkeys;	/* B43_HF_USEDEFKEYS follows the WEP keys */
	bool dfq_valid;		/* B43_MACCMD_DFQ_VALID from init on */
	bool ampdu;		/* aggregates the frames flagged A-MPDU */
	bool txbf;		/* transmit beamforming block */
	bool deprecated;	/* warned about at load */
	bool unknown;		/* a revision the table does not list */
};

struct b43_fw_range {
	u16 first, last;
	struct b43_fw_layout layout;
};

/* A microcode file: the last of its ranges takes any revision not listed. */
struct b43_fw_family {
	const char *ucode;
	const struct b43_fw_range *ranges;
	unsigned int nr_ranges;
};

const struct b43_fw_family *b43_fw_family_find(u8 core_rev, u8 phy_type);
const struct b43_fw_layout *b43_fw_layout_get(const struct b43_fw_family *family,
					      u16 fw_rev);

/* The MAC config block of the layout, for a PHY of @cores, @coremask wired */
void b43_shm_mac_config_block(struct b43_wldev *dev, unsigned int cores,
			      unsigned long coremask);

#endif /* B43_FW_LAYOUT_H_ */
