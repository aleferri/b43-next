/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_FW_LAYOUT_H_
#define B43_FW_LAYOUT_H_

#include <linux/types.h>

enum b43_firmware_hdr_format {
	B43_FW_HDR_598,
	B43_FW_HDR_410,
	B43_FW_HDR_351,
	/*
	 * The microcode of the AC cores, 832.127 in wl 6.30.223 and 928.1201
	 * in 7.14.43 alike: a 40 byte RX header and an eight-word TX status.
	 * The TX header and the fields inside the RX header are not visible
	 * at the bus; where b43 reads the RX header as the 598 one, in
	 * b43_hdr_layout_ac and the A-MSDU path, that is assumed, not verified.
	 */
	B43_FW_HDR_AC,
};

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
	bool mac_cfg_0020;	/* 0x0020 = 0x0800 before the block */
	bool mac_cfg_16;	/* the seven words from mac_cfg + 0x16 */
	u16 cts_duration;	/* us, of the CTS-to-self of the RX-cal suspend */
	bool cts_suspended;	/* armed with the MAC already suspended */
	u16 txerr;		/* latch of the saved TX fault; 0 if none known */
};

/*
 * Where the fields that move with the microcode sit in its TX and RX headers,
 * in bytes from the start of the header, and how DMA ring 0 receives them.
 * The tx_ offsets but tx_len are those of the pre-AC header,
 * b43_generate_txhdr()'s.
 */
struct b43_hdr_layout {
	u8 tx_len;
	u8 tx_cookie;
	u8 tx_rts_plcp;
	u8 tx_rts_frame;
	u8 tx_plcp;
	bool tx_ppdu_plcp;	/* PLCP of the PPDU: HT bits, A-MPDU preload */
	u8 tx_preload;		/* with tx_ppdu_plcp */
	u8 rx_len;		/* as PIO reads it; 0 if none is known */
	u8 rx_frame_offset;
	u16 rx_buffersize;
	u8 rx_mac_status;
	u8 rx_mac_time;
	u8 rx_channel;
};

extern const struct b43_hdr_layout b43_hdr_layout_351;
extern const struct b43_hdr_layout b43_hdr_layout_410;
extern const struct b43_hdr_layout b43_hdr_layout_598;
extern const struct b43_hdr_layout b43_hdr_layout_ac;

/* Everything b43 decides from the microcode it runs. */
struct b43_fw_layout {
	enum b43_firmware_hdr_format hdr_format;
	const struct b43_hdr_layout *hdr;
	/* NULL for an AC microcode not known here */
	const struct b43_tpl_layout *tpl;
	/* NULL but for an AC microcode known here */
	const struct b43_shm_layout *shm;
	u16 keyidxblock;	/* key index/algorithm block */
	bool new_kidx_api;	/* key indexes not shifted past the TX keys */
	bool machw_bit31;	/* MAC_HW_CAP bit 31 passed to the microcode */
	bool edcrs;		/* energy-detect carrier sense in IFSCTL1 */
	bool g_chanid_number;	/* G-PHY RX channel ID a number, not MHz - 2400 */
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

#endif /* B43_FW_LAYOUT_H_ */
