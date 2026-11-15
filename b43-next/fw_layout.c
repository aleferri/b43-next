// SPDX-License-Identifier: GPL-2.0-or-later

#include "b43.h"
#include "fw_layout.h"

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
	.cts_duration	= 4400,
	.cts_suspended	= true,
	.txerr		= B43_SHM_SH_TXERR_AC784,
};

static const struct b43_shm_layout b43_shm_layout_ac_832 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_AC832,
	.mac_cfg_16	= true,
	.cts_duration	= 29000,
	.txerr		= B43_SHM_SH_TXERR_AC928,
};

static const struct b43_shm_layout b43_shm_layout_ac_928 = {
	.mac_cfg	= B43_SHM_SH_MACCFG_AC928,
	.mac_cfg_0020	= true,
	.mac_cfg_16	= true,
	.cts_duration	= 29000,
	.txerr		= B43_SHM_SH_TXERR_AC928,
};

#define B43_FW_PRE_AC					\
	.tpl		= &b43_tpl_layout_legacy,	\
	.keyidxblock	= B43_SHM_SH_KEYIDXBLOCK,	\
	.machw_bit31	= true

/*
 * new_kidx_api: FIXME: not sure the change was at 351.
 * g_chanid_number: changed between 478.104 and 508.1084.
 */
static const struct b43_fw_range b43_fw_revs_pre_ac[] = {
	{ 0, 350, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_351 } },
	{ 351, 409, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_351,
		      .new_kidx_api = true } },
	{ 410, 507, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_410,
		      .new_kidx_api = true } },
	{ 508, 597, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_410,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 598, 0xffff, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_598,
			 .new_kidx_api = true, .g_chanid_number = true } },
};

/* edcrs: firmware 784.2 uploads slower with it. */
static const struct b43_fw_range b43_fw_revs_ucode30_mimo[] = {
	{ 0, 350, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_351 } },
	{ 351, 409, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_351,
		      .new_kidx_api = true } },
	{ 410, 507, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_410,
		      .new_kidx_api = true } },
	{ 508, 597, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_410,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 598, 831, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_598,
		      .new_kidx_api = true, .g_chanid_number = true } },
	{ 832, 0xffff, { B43_FW_PRE_AC, .hdr_format = B43_FW_HDR_598,
			 .new_kidx_api = true, .g_chanid_number = true,
			 .edcrs = true } },
};

#define B43_FW_AC					\
	.hdr_format	= B43_FW_HDR_AC,		\
	.keyidxblock	= B43_SHM_SH_KEYIDXBLOCK_AC,	\
	.new_kidx_api	= true,				\
	.g_chanid_number = true

/*
 * keyidxblock: the block starts further on than on the v4 firmware: 0x05D4
 * holds PHY state, the transmit chain mask, and writing there breaks the
 * transmit path silently.
 *
 * machw_bit31: cleared by every stock driver in the captures -- 6.30.102.7
 * with ucode 784, 7.14 with 928 on three boards -- except the x86 hybrid
 * 6.30.223, which leaves it set for its ucode 832. That one is followed too:
 * the captures cannot tell the ucode from the build.
 */
static const struct b43_fw_range b43_fw_revs_ucode42[] = {
	{ 784, 784, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_784,
		      .shm = &b43_shm_layout_ac_784 } },
	{ 832, 832, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_832,
		      .shm = &b43_shm_layout_ac_832, .machw_bit31 = true } },
	{ 928, 928, { B43_FW_AC, .tpl = &b43_tpl_layout_ac_832,
		      .shm = &b43_shm_layout_ac_928 } },
	{ 0, 0xffff, { B43_FW_AC } },
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
B43_FW_FAMILY(ucode24_lcn, b43_fw_revs_pre_ac);
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
