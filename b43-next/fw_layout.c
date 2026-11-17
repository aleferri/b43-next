// SPDX-License-Identifier: GPL-2.0-or-later

#include "b43.h"
#include "fw_layout.h"
#include "main.h"

#define B43_SHM_SH_KEYIDXBLOCK		0x05D4	/* Key index/algorithm block (v4 firmware) */
#define B43_SHM_SH_KEYIDXBLOCK_AC	0x05E0	/* the same, AC microcode */
#define B43_SHM_SH_BT_BASE0		0x0068	/* Beacon template base 0 */
#define B43_SHM_SH_BT_BASE1		0x0468	/* Beacon template base 1 */
#define B43_SHM_SH_BT_BASE0_AC784	0x00D8	/* AC ucode 784, beacon 0 */
#define B43_SHM_SH_BT_BASE1_AC784	0x02D8	/* AC ucode 784, beacon 1 */
#define B43_SHM_SH_BT_BASE0_AC832	0x0200	/* AC ucode 832 on, beacon 0 */
#define B43_SHM_SH_BT_BASE1_AC832	0x0480	/* AC ucode 832 on, beacon 1 */
#define B43_SHM_SH_TXERR_AC784		0x16C8	/* AC ucode 784, latch */
#define B43_SHM_SH_TXERR_AC928		0x17F4	/* AC ucode 832 and 928, latch */
#define B43_SHM_SH_MACCFG_AC784		0x0894	/* AC ucode 784 */
#define B43_SHM_SH_MACCFG_AC832		0x08F4	/* AC ucode 832 */
#define B43_SHM_SH_MACCFG_AC928		0x08EC	/* AC ucode 928 */
#define B43_SHM_SH_MACCFG_LCN832	0x00E4	/* LCN ucode 832 */

static const struct b43_tpl_layout b43_tpl_layout_legacy = {
	.bcn_base	= { B43_SHM_SH_BT_BASE0, B43_SHM_SH_BT_BASE1 },
	.bcn_size	= 0x0200,
	.hdr_len	= 6,
	.plcp_off	= 0,
	.plcp_off_cck	= 0,
};

static const struct b43_tpl_layout b43_tpl_layout_ac_784 = {
	.bcn_base	= { B43_SHM_SH_BT_BASE0_AC784,
			    B43_SHM_SH_BT_BASE1_AC784 },
	.bcn_size	= 0x0200,
	.hdr_len	= 12,
	.plcp_off	= 3,
	.plcp_off_cck	= 6,
};

static const struct b43_tpl_layout b43_tpl_layout_ac_832 = {
	.bcn_base	= { B43_SHM_SH_BT_BASE0_AC832,
			    B43_SHM_SH_BT_BASE1_AC832 },
	.bcn_size	= 0x0280,
	.hdr_len	= 12,
	.plcp_off	= 3,
	.plcp_off_cck	= 6,
};

/*
 * 784 is the microcode of 6.30 on the DSL-3580L, 832 of the x86 hybrid
 * 6.30.223, 928 of 7.14; each layout is what that driver writes
 * (router-data/dsl3580l, router-data/archer-t5e, router-data/d6220).
 */
static const struct b43_shm_layout b43_shm_layout_ac_784 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_AC784,
	.mac_cfg_val	= 0x186a,
	.cts_duration	= 4400,
	.cts_suspended	= true,
	.txerr		= B43_SHM_SH_TXERR_AC784,
};

static const struct b43_shm_layout b43_shm_layout_ac_832 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_AC832,
	.mac_cfg_val	= 0x186a,
	.mac_cfg_16	= true,
	.cts_duration	= 29000,
	.txerr		= B43_SHM_SH_TXERR_AC928,
};

static const struct b43_shm_layout b43_shm_layout_ac_928 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_AC928,
	.mac_cfg_val	= 0x186a,
	.mac_cfg_0020	= true,
	.mac_cfg_16	= true,
	.cts_duration	= 29000,
	.txerr		= B43_SHM_SH_TXERR_AC928,
};

/*
 * 832 on the LCN-PHY, the BCM4313's of the x86 hybrid 6.30 (router-data
 * bcm4313-rev1/compaq-cq57): the AC 832's MAC config block, at its own base.
 */
static const struct b43_shm_layout b43_shm_layout_lcn_832 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_LCN832,
	.mac_cfg_16	= true,
};

#define B43_FW_PRE_AC					\
	.tpl		= &b43_tpl_layout_legacy,	\
	.keyidxblock	= B43_SHM_SH_KEYIDXBLOCK,	\
	.machw_bit31	= true,				\
	.hw_tkip	= true,				\
	.rcmta_count	= true,				\
	.phytxctl_shm	= true,				\
	.hf_usedefkeys	= true

/*
 * new_kidx_api: FIXME: not sure the change was at 351.
 * g_chanid_number: changed between 478.104 and 508.1084.
 */
static const struct b43_fw_range b43_fw_revs_pre_ac[] = {
	{ 0, 350, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .deprecated = true } },
	{ 351, 409, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .new_kidx_api = true, .deprecated = true } },
	{ 410, 507, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true } },
	{ 508, 597, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 598, 0xffff, { B43_FW_PRE_AC, .hdr = &b43_hdr_598,
			 .new_kidx_api = true, .g_chanid_number = true } },
};

static const struct b43_fw_range b43_fw_revs_ucode24_lcn[] = {
	{ 0, 350, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .deprecated = true } },
	{ 351, 409, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .new_kidx_api = true, .deprecated = true } },
	{ 410, 507, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true } },
	{ 508, 597, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 598, 831, { B43_FW_PRE_AC, .hdr = &b43_hdr_598,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 832, 0xffff, { B43_FW_PRE_AC, .hdr = &b43_hdr_598,
			 .new_kidx_api = true, .g_chanid_number = true,
			 .shm = &b43_shm_layout_lcn_832 } },
};

/* edcrs: firmware 784.2 uploads slower with it. */
static const struct b43_fw_range b43_fw_revs_ucode30_mimo[] = {
	{ 0, 350, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .deprecated = true } },
	{ 351, 409, { B43_FW_PRE_AC, .hdr = &b43_hdr_351,
		      .new_kidx_api = true, .deprecated = true } },
	{ 410, 507, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true } },
	{ 508, 597, { B43_FW_PRE_AC, .hdr = &b43_hdr_410,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 598, 831, { B43_FW_PRE_AC, .hdr = &b43_hdr_598,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 832, 0xffff, { B43_FW_PRE_AC, .hdr = &b43_hdr_598,
			 .new_kidx_api = true, .g_chanid_number = true,
			 .edcrs = true } },
};

#define B43_FW_AC					\
	.hdr		= &b43_ac_hdr_784,		\
	.keyidxblock	= B43_SHM_SH_KEYIDXBLOCK_AC,	\
	.new_kidx_api	= true,				\
	.g_chanid_number = true,			\
	.kidx_empty_zero = true,			\
	.sec_algo_d11	= true,				\
	.dfq_valid	= true,				\
	.ampdu		= true,				\
	.txbf		= true

/*
 * keyidxblock: 0x05D4 holds the TX chain mask here.
 * machw_bit31: in the captures only 6.30.223 (832) leaves it set.
 * hw_tkip: the RX TKIP table and the TX header's TKIP fields are not known.
 * phytxctl_shm: the beacon's is elsewhere, b43_write_beacon_phytxctl_ac().
 * hf_usedefkeys: 7.14.43 leaves the host flags alone on a WEP key
 * (vd625-agcombo/ch149-wep.bin); mac80211 encrypts management frames.
 * dfq_valid: set by 6.30 and 7.14 from init on; what it gates is not known.
 * unknown: the header of a revision not listed is assumed.
 */
static const struct b43_fw_range b43_fw_revs_ucode42[] = {
	{ 784, 784, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_784,
		      .shm = &b43_shm_layout_ac_784 } },
	{ 832, 832, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_832,
		      .shm = &b43_shm_layout_ac_832, .machw_bit31 = true } },
	{ 928, 928, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_832,
		      .shm = &b43_shm_layout_ac_928 } },
	{ 0, 0xffff, { B43_FW_AC, .unknown = true } },
};

#define B43_FW_FAMILY(name, revs)					\
	static const struct b43_fw_family b43_fw_##name = {		\
		.ucode = #name,						\
		.ranges = revs,						\
		.nr_ranges = ARRAY_SIZE(revs),				\
	}

B43_FW_FAMILY(ucode5, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode11, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode13, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode14, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode15, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode16_lp, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode16_mimo, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode22_mimo, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode24_lcn, b43_fw_revs_ucode24_lcn);
B43_FW_FAMILY(ucode25_lcn, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode25_mimo, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode26_mimo, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode29_mimo, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode30_mimo, b43_fw_revs_ucode30_mimo);
B43_FW_FAMILY(ucode33_lcn40, b43_fw_revs_pre_ac);
/* No capture covers ucode40: its layout is not known to be the AC one. */
B43_FW_FAMILY(ucode40, b43_fw_revs_pre_ac);
B43_FW_FAMILY(ucode42, b43_fw_revs_ucode42);

/* The microcode of a core with its PHY; NULL if there is none. */
const struct b43_fw_family *b43_fw_family_find(u8 core_rev, u8 phy_type)
{
	switch (core_rev) {
	case 42:
		if (phy_type == B43_PHYTYPE_AC)
			return &b43_fw_ucode42;
		break;
	case 40:
		if (phy_type == B43_PHYTYPE_AC)
			return &b43_fw_ucode40;
		break;
	case 33:
		if (phy_type == B43_PHYTYPE_LCN40)
			return &b43_fw_ucode33_lcn40;
		break;
	case 30:
		if (phy_type == B43_PHYTYPE_N)
			return &b43_fw_ucode30_mimo;
		break;
	case 29:
		if (phy_type == B43_PHYTYPE_HT)
			return &b43_fw_ucode29_mimo;
		break;
	case 26:
		if (phy_type == B43_PHYTYPE_HT)
			return &b43_fw_ucode26_mimo;
		break;
	case 28:
	case 25:
		if (phy_type == B43_PHYTYPE_N)
			return &b43_fw_ucode25_mimo;
		if (phy_type == B43_PHYTYPE_LCN)
			return &b43_fw_ucode25_lcn;
		break;
	case 24:
		if (phy_type == B43_PHYTYPE_LCN)
			return &b43_fw_ucode24_lcn;
		break;
	case 23:
		if (phy_type == B43_PHYTYPE_N)
			return &b43_fw_ucode16_mimo;
		break;
	case 22:
		if (phy_type == B43_PHYTYPE_N)
			return &b43_fw_ucode22_mimo;
		break;
	case 16 ... 19:
		if (phy_type == B43_PHYTYPE_N)
			return &b43_fw_ucode16_mimo;
		if (phy_type == B43_PHYTYPE_LP)
			return &b43_fw_ucode16_lp;
		break;
	case 15:
		return &b43_fw_ucode15;
	case 14:
		return &b43_fw_ucode14;
	case 13:
		return &b43_fw_ucode13;
	case 11 ... 12:
		return &b43_fw_ucode11;
	case 5 ... 10:
		return &b43_fw_ucode5;
	}
	return NULL;
}

const struct b43_fw_layout *b43_fw_layout_get(const struct b43_fw_family *family,
					      u16 fw_rev)
{
	const struct b43_fw_range *r = family->ranges;
	const struct b43_fw_range *last = r + family->nr_ranges - 1;

	for (; r < last; r++)
		if (fw_rev >= r->first && fw_rev <= r->last)
			break;
	return &r->layout;
}

/*
 * The invariant part of the MAC config block, right after the two zeroings
 * on the AC, after the key table's clear on the LCN-PHY (compaq-cq57,
 * #32475-#32529, at 0x00e4 with 0 in the first cell, one chain).
 *
 * Every value is the same on every segment of the d6220 cold sweep, and the
 * tg789vac writes the same ones except for the chain-count cells; the
 * BCM4313's 832 writes them too. They are transcribed; their purpose is not
 * known.
 *
 * The cells are at fixed distances from the block's first one, which each
 * microcode keeps at its own offset (struct b43_shm_layout): 0x08ec on 928,
 * 0x08f4 on 832, 0x0894 on 784. Their drivers write them in the same order
 * and with the same values, but 0x0020 only on 928 and the block at +0x16
 * not on 784 (router-data/dsl3580l/cold01-ch36-bw20.txt, 9248-9292;
 * router-data/archer-t5e/mmiotrace.zip, #13998-14054). With no layout
 * nothing is written: at another microcode's offsets the block can land on
 * live cells, as 928's does on 784's CTS-to-self.
 *
 * Not here yet: the fifteen cells at stride 0x14 that follow the width, the
 * twelve at stride 0x1c that follow the centre frequency, the read of 0x00b0
 * (the core's EXTNPHYCTL in b43.h) and 0x05dc (coremask, in KEYIDXBLOCK).
 * Who should emit the last two is not settled.
 * [capture-ref: router-data/d6220/cold-sweep.zip!cold01-ch36-bw20.txt;
 *   12860-12892]
 * [capture-ref: router-data/d6220/hot-sweep.zip!segmenti/01-up-ch36-bw20.txt;
 *   8547-8579]
 */
void b43_shm_mac_config_block(struct b43_wldev *dev, unsigned int cores,
			      unsigned long coremask)
{
	/* Sixteen consecutive words from +0x40. */
	static const u16 words_40[] = {
		0x3475, 0x3475, 0x3475, 0x217c, 0x237b, 0x217c, 0x217c, 0x217c,
		0x3276, 0x3276, 0x3475, 0x187e, 0x217c, 0x167e, 0x1d7d, 0x1f7c,
	};
	/* Seven consecutive words from +0x16. */
	static const u16 words_16[] = {
		0x41c2, 0x0000, 0x0017, 0x024b, 0x0097, 0x0500, 0x0000,
	};
	const struct b43_shm_layout *shm = dev->fw.layout.shm;
	u16 base;
	unsigned int i;

	if (!shm || !shm->mac_cfg)
		return;
	base = shm->mac_cfg;

	if (shm->mac_cfg_0020)
		b43_shm_write16(dev, B43_SHM_SHARED, B43_SHM_SH_MAXRXFRMLEN, 0x0800);
	b43_shm_write16(dev, B43_SHM_SHARED, base, shm->mac_cfg_val);
	/*
	 * The d6220, with one of its three cores unwired, writes +0x24 and +0x08
	 * and then +0x04 = 1; the agcombo and the tg789vac, all three wired,
	 * skip the pair and write +0x04 = 2. Two boards cannot tell "a core is
	 * unwired" from "two chains".
	 */
	if (hweight8((u8)coremask) != cores) {
		b43_shm_write16(dev, B43_SHM_SHARED, base + 0x24, 0x80c3);
		b43_shm_write16(dev, B43_SHM_SHARED, base + 0x08, 0x80c2);
	}
	b43_shm_write16(dev, B43_SHM_SHARED, base + 0x04,
			(u16)(hweight8((u8)coremask) - 1));

	/*
	 * Read and written back: the value is zero in the capture, and the
	 * read-rewrite form avoids inventing it.
	 */
	b43_shm_write16(dev, B43_SHM_SHARED, base + 0x600,
			b43_shm_read16(dev, B43_SHM_SHARED, base + 0x600));

	for (i = 0; i < ARRAY_SIZE(words_40); i++)
		b43_shm_write16(dev, B43_SHM_SHARED,
				(u16)(base + 0x40 + i * 2), words_40[i]);
	if (!shm->mac_cfg_16)
		return;
	for (i = 0; i < ARRAY_SIZE(words_16); i++)
		b43_shm_write16(dev, B43_SHM_SHARED,
				(u16)(base + 0x16 + i * 2), words_16[i]);
}
