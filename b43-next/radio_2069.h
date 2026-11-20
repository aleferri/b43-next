/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_RADIO_2069_H_
#define B43_RADIO_2069_H_

#include <linux/types.h>

struct b43_wldev;

#define R2069_PWRON			0x40b
#define  R2069_PWRON_KICK		0x0001
#define  R2069_PWRON_DONE		0x0008

/* RC calibration engine */
#define R2069_RCCAL_EN1			0x8ea
#define  R2069_RCCAL_EN1_CAL		0x0080	/* also the AFE cal's */
#define R2069_RCCAL_EN2			0x8ed
#define  R2069_RCCAL_EN2_MODE		0x0600
#define R2069_RCCAL_CFG			0x410
#define  R2069_RCCAL_CFG_START		0x0001
#define  R2069_RCCAL_CFG_MODE		0x0018
#define  R2069_RCCAL_CFG_MODE_SHIFT	3
#define  R2069_RCCAL_CFG_INIT		0x1000
#define R2069_RCCAL_KICK		0x411
#define  R2069_RCCAL_KICK_GO		0x0001
#define  R2069_RCCAL_KICK_NLOOP		0xff00
#define  R2069_RCCAL_KICK_NLOOP_SHIFT	8
#define R2069_RCCAL_SEED		0x412
#define R2069_RCCAL_STAT		0x413
#define  R2069_RCCAL_STAT_DONE		0x0010
#define R2069_RCCAL_E			0x414	/* comparator A, pass 0 */
#define R2069_RCCAL_F			0x415	/* comparator B, pass 0 */
#define R2069_RCCAL_G			0x416
#define  R2069_RCCAL_G_CODE		0x001f	/* pass 1 */
#define  R2069_RCCAL_G_DACBUF		0x03e0	/* pass 2 */
#define  R2069_RCCAL_G_DACBUF_SHIFT	5

/* Per-core RC trim, OR'd with core << 9 */
#define R2069_RCCAL_P1_A		0x126
#define R2069_RCCAL_P1_B		0x043
#define  R2069_RCCAL_P1_CODE		0x001f
#define R2069_RCCAL_P2_A		0x11d
#define  R2069_RCCAL_P2_A_FAST		0x0004
#define R2069_RCCAL_P2_B		0x171
#define  R2069_RCCAL_P2_B_MEAS		0x2000	/* dacbuf cap measurement */

/* AFE calibration, per core */
#define R2069_AFE_CAL_CLK		0x121
#define  R2069_AFE_CAL_CLK_EN		0x1000
#define R2069_AFE_CAL_CTRL		0x122
#define  R2069_AFE_CAL_CTRL_EN		0x000f
#define R2069_AFE_CAL_STAT		0x144
#define  R2069_AFE_CAL_STAT_VALID	0x0001	/* second read */
#define  R2069_AFE_CAL_STAT_DONE	0x0002	/* first read */

/* The RC-cal engine's setup of one pass */
struct r2069_rccal_pass {
	u8 init, mode, nloop;
	u16 seed;
};

extern const struct r2069_rccal_pass r2069_rccal_passes[3];

/*
 * One row of the per-channel table for the 2069 radio.
 *
 * All 45 cluster-1 values of an entry, u16[2..46], are radio writes, of the
 * registers r2069_upload_chantab() writes them to: radio_raw[39] at offsets
 * 4 to 80, chan_raw6[6] at 82 to 92. phy_bw[6] are the values of PHY
 * B43_PHY_AC_BW1A to BW1F.
 *
 * The rows are chan_tuning_2069rev4 of the 6.30.102.7 blob, 2.4 and 5 GHz,
 * with one register map for both bands. `b43trace check-channeltab`
 * checks them against the captures' BW20 segments. The symbol of the same
 * name in wlD6220.o (7.14.89, .data + 0x63ef0) has the same 64 rows and
 * 13 more, channels 184 to 216; each of its rows also carries five words,
 * u16[47..51], between chan_raw6 and phy_bw, which this table leaves out.
 */
struct b43_phy_ac_channeltab_e_radio2069 {
	u8  channel;
	u16 freq;
	u16 radio_raw[39];
	u16 chan_raw6[6];
	u16 phy_bw[6];
};

const struct b43_phy_ac_channeltab_e_radio2069 *
b43_phy_ac_get_channeltab_e_r2069(struct b43_wldev *dev, u16 freq);

void r2069_upload_inittabs(struct b43_wldev *dev);
void r2069_upload_chantab(struct b43_wldev *dev,
			  const struct b43_phy_ac_channeltab_e_radio2069 *e);

#endif /* B43_RADIO_2069_H_ */
