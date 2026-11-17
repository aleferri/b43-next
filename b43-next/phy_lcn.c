/*

  Broadcom B43 wireless driver
  IEEE 802.11n LCN-PHY support

  Copyright (c) 2011 Rafał Miłecki <zajec5@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

  This file incorporates work covered by the following copyright and
  permission notice:

      Copyright (c) 2010 Broadcom Corporation

      Permission to use, copy, modify, and/or distribute this software for any
      purpose with or without fee is hereby granted, provided that the above
      copyright notice and this permission notice appear in all copies.
*/

#include <linux/slab.h>

#include "b43.h"
#include "phy_lcn.h"
#include "tables_phy_lcn.h"
#include "main.h"
#include "calibrate.h"
#include "calstat.h"
#include "ppr.h"

/* The entries of the calibration statistics, see b43_phy_lcn_cal_entries[] */
enum {
	LCN_CAL_IDLE_TSSI,
	LCN_CAL_TXIQLO,
	LCN_CAL_RXIQ_GAIN,
	LCN_CAL_RXIQ_COMP,
	LCN_CAL_TXPWR,
};

struct lcn_tx_gains {
	u16 gm_gain;
	u16 pga_gain;
	u16 pad_gain;
	u16 dac_gain;
};

struct lcn_tx_iir_filter {
	u8 type;
	u16 values[16];
};

enum lcn_sense_type {
	B43_SENSE_TEMP,
	B43_SENSE_VBAT,
};

/**************************************************
 * Radio 2064.
 **************************************************/

/* wlc_lcnphy_qdiv_roundup */
static u32 b43_phy_lcn_qdiv_roundup(u32 dividend, u32 divisor, u8 precision)
{
	u32 quotient = dividend / divisor, remainder = dividend % divisor;
	u32 rbit = divisor & 1, roundup = (divisor >> 1) + rbit;

	while (precision--) {
		quotient <<= 1;
		if (remainder >= roundup) {
			quotient++;
			remainder = ((remainder - roundup) << 1) + rbit;
		} else {
			remainder <<= 1;
		}
	}
	if (remainder >= roundup)
		quotient++;
	return quotient;
}

/* wlc_2064_vco_cal */
static void b43_radio_2064_vco_cal(struct b43_wldev *dev)
{
	u8 calnrst;

	b43_radio_set(dev, 0x057, 0x8);
	calnrst = b43_radio_read(dev, 0x056) & 0xf8;
	b43_radio_write(dev, 0x056, calnrst);
	udelay(1);
	b43_radio_write(dev, 0x056, calnrst | 0x03);
	udelay(1);
	b43_radio_write(dev, 0x056, calnrst | 0x07);
	udelay(300);
	b43_radio_mask(dev, 0x057, ~0x8);
}

/*
 * wlc_lcnphy_radio_2064_channel_tune_4313: the RF PLL with the reference
 * doubler, from the crystal's clock. The channel's tuning values are the
 * same on every 2.4 GHz channel.
 */
static void b43_radio_2064_channel_setup(struct b43_wldev *dev,
					 unsigned int channel, u16 freq)
{
	struct bcmn_drv_cc *cc = &dev->dev->bdev->bus->drv_cc;
	u32 xtal = bcmn_chipco_get_alp_clock(cc);
	u32 fpfd = 2 * xtal, fvco3 = 3 * freq, fref3 = 2 * fpfd;
	u32 fcal_div, qfcal, div_int, div_frac;
	u16 set_count, d28, g30;
	u8 d15, d16, f16, h28_ten, h30_ten, e30, cp_current;
	u16 save[2];

	b43_radio_set(dev, 0x09d, 0x4);
	b43_radio_write(dev, 0x09e, 0xf);

	b43_radio_write(dev, 0x02a, 0xb);
	b43_radio_maskset(dev, 0x030, ~0x3, 0xa & 0x3);
	b43_radio_mask(dev, 0x091, ~0x3);
	b43_radio_maskset(dev, 0x038, ~0xf, 0x7);
	b43_radio_maskset(dev, 0x030, ~0xc, 0xa << 2 & 0xc);
	b43_radio_maskset(dev, 0x05e, ~0xf, 0x88 & 0xf);
	b43_radio_maskset(dev, 0x05e, ~0xf0, 0x88 << 4 & 0xf0);
	b43_radio_write(dev, 0x06c, 0x80);

	save[0] = b43_radio_read(dev, 0x044);
	save[1] = b43_radio_read(dev, 0x12b);
	b43_radio_set(dev, 0x044, 0x7);
	b43_radio_set(dev, 0x12b, 0xe);

	if (xtal > 52000000)
		fcal_div = 4;
	else if (xtal > 26000000)
		fcal_div = 2;
	else
		fcal_div = 1;
	qfcal = xtal * fcal_div / 1000000;
	b43_radio_write(dev, 0x04f, 0x02);
	d15 = (xtal * fcal_div * 4 / 5) / 1000000 - 1;
	b43_radio_write(dev, 0x052, 0x07 & (d15 >> 2));
	b43_radio_write(dev, 0x053, (d15 & 0x3) << 5);
	d16 = (qfcal * 8 / (d15 + 1)) - 1;
	b43_radio_write(dev, 0x051, d16);
	f16 = ((d16 + 1) * (d15 + 1)) / qfcal;
	set_count = f16 * 3 * freq / 32 - 1;
	b43_radio_maskset(dev, 0x053, ~0x0f, (set_count >> 8) & 0x0f);
	b43_radio_set(dev, 0x053, 0x10);
	b43_radio_write(dev, 0x054, set_count & 0xff);

	div_int = ((fvco3 * (1000000 >> 4)) / fref3) << 4;
	div_frac = ((fvco3 * (1000000 >> 4)) % fref3) << 4;
	while (div_frac >= fref3) {
		div_int++;
		div_frac -= fref3;
	}
	div_frac = b43_phy_lcn_qdiv_roundup(div_frac, fref3, 20);
	b43_radio_maskset(dev, 0x045, ~0x1f, (div_int >> 4) & 0x1f);
	b43_radio_maskset(dev, 0x046, ~0x1f0, (u8)(div_int << 4));
	b43_radio_maskset(dev, 0x046, ~0x0f, (div_frac >> 16) & 0x0f);
	b43_radio_write(dev, 0x047, (div_frac >> 8) & 0xff);
	b43_radio_write(dev, 0x048, div_frac & 0xff);

	b43_radio_write(dev, 0x040, 0xfb);
	b43_radio_write(dev, 0x041, 0x9a);
	b43_radio_write(dev, 0x042, 0xa3);
	b43_radio_write(dev, 0x043, 0x0c);

	/* The charge pump, for the doubler's loop bandwidth and divider */
	d28 = ((68 - 27) * (fvco3 / 2 - 3000)) / (4200 - 3000) + 27;
	h28_ten = d28 * 10 / 30;
	e30 = (10500 - 680) / 490;
	g30 = 680 + e30 * 490;
	h30_ten = g30 * 10 / 2640;
	cp_current = (1250 * 100 / h28_ten) / h30_ten;
	b43_radio_maskset(dev, 0x03c, ~0x3f, cp_current & 0x3f);
	b43_radio_write(dev, 0x03c, channel <= 5 ? 0x8 : 0x7);
	/* wl's, by channel; brcmsmac's is 0x3 on every channel */
	b43_radio_write(dev, 0x03d, channel <= 3 ? 0xa : channel <= 7 ? 0x9 : 0x8);
	b43_radio_set(dev, 0x044, 0x0c);
	udelay(1);
	b43_radio_2064_vco_cal(dev);

	b43_radio_write(dev, 0x044, save[0]);
	b43_radio_write(dev, 0x12b, save[1]);

	if (dev->phy.rev == 1) {
		/* brcmsmac uses outdated 0x3 for 0x038 */
		b43_radio_write(dev, 0x038, 0x0);
		b43_radio_write(dev, 0x091, 0x7);
	}
	if (!(dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM)) {
		b43_radio_write(dev, 0x02a, 0xf);
		b43_radio_write(dev, 0x091, 0x3);
		b43_radio_write(dev, 0x038, 0x3);
	}
}

/* wlc_radio_2064_init */
static void b43_radio_2064_init(struct b43_wldev *dev)
{
	int i;

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		b43_radio_write(dev, 0x09c, 0x0020);
		b43_radio_write(dev, 0x105, 0x0008);
	} else {
		/* TODO */
	}
	b43_radio_write(dev, 0x032, 0x0062);
	b43_radio_write(dev, 0x033, 0x0019);
	b43_radio_write(dev, 0x0c2, 0x006f);
	b43_radio_write(dev, 0x090, 0x0010);
	b43_radio_write(dev, 0x010, 0x0000);
	if (dev->phy.rev == 1) {
		b43_radio_write(dev, 0x060, 0x007f);
		b43_radio_write(dev, 0x061, 0x0072);
		b43_radio_write(dev, 0x062, 0x007f);
	}
	b43_radio_write(dev, 0x01d, 0x0002);
	b43_radio_write(dev, 0x01e, 0x0006);

	b43_phy_write(dev, 0x4ea, 0x4688);
	if (dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM)
		b43_phy_maskset(dev, 0x4eb, ~0x7, 0x2);
	else
		b43_phy_maskset(dev, 0x4eb, ~0x7, 0x3);
	b43_phy_mask(dev, 0x4eb, ~0x01c0);
	b43_phy_maskset(dev, 0x46a, 0xff00, 0x19);

	b43_lcntab_write(dev, B43_LCNTAB16(0x00, 0x55), 0);

	b43_radio_mask(dev, 0x05b, (u16) ~0xff02);
	b43_radio_set(dev, 0x004, 0x40);
	b43_radio_set(dev, 0x120, 0x10);
	b43_radio_set(dev, 0x078, 0x80);
	b43_radio_set(dev, 0x129, 0x2);
	b43_radio_set(dev, 0x057, 0x1);
	b43_radio_set(dev, 0x05b, 0x2);

	for (i = 0; i < 100; i++) {
		if (b43_radio_read(dev, 0x05c) & 0x20)
			break;
		udelay(10);
	}
	if (i < 100)
		b43_radio_read(dev, 0x05c);
	else
		b43err(dev->wl, "Radio 2064 R calibration timed out\n");

	b43_radio_mask(dev, 0x05b, (u16) ~0xff02);
	b43_radio_mask(dev, 0x057, (u16) ~0xff01);

	b43_phy_write(dev, 0x933, 0x2d6b);
	b43_phy_write(dev, 0x934, 0x2d6b);
	b43_phy_write(dev, 0x935, 0x2d6b);
	b43_phy_write(dev, 0x936, 0x2d6b);
	b43_phy_write(dev, 0x937, 0x016b);

	b43_radio_mask(dev, 0x057, (u16) ~0xff02);

	if (!(dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM)) {
		b43_radio_write(dev, 0x02c, 0x07);
		b43_radio_write(dev, 0x032, 0x6f);
		b43_radio_write(dev, 0x08f, 0x0c);
		b43_radio_write(dev, 0x033, 0x39);
		b43_radio_write(dev, 0x003, 0x66);
		b43_radio_write(dev, 0x00d, 0x00);
		b43_radio_write(dev, 0x00c, 0x55);
		b43_radio_write(dev, 0x00a, 0x73);
		b43_radio_write(dev, 0x008, 0xff);
		b43_radio_write(dev, 0x01f, 0x00);
		b43_radio_write(dev, 0x01d, 0x07);
		b43_radio_mask(dev, 0x01e, ~0x4);
		b43_radio_write(dev, 0x032, 0x6f);
		b43_radio_write(dev, 0x033, 0x19);
		b43_radio_write(dev, 0x039, 0x0e);
	}
}

/**************************************************
 * Various PHY ops
 **************************************************/

/* wlc_lcnphy_toggle_afe_pwdn */
static void b43_phy_lcn_afe_set_unset(struct b43_wldev *dev)
{
	u16 afe_ctl2 = b43_phy_read(dev, B43_PHY_LCN_AFE_CTL2);
	u16 afe_ctl1 = b43_phy_read(dev, B43_PHY_LCN_AFE_CTL1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2 | 0x1);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1 | 0x1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2 & ~0x1);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1 & ~0x1);

	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL2, afe_ctl2);
	b43_phy_write(dev, B43_PHY_LCN_AFE_CTL1, afe_ctl1);
}

/* wlc_lcnphy_get_pa_gain */
static u16 b43_phy_lcn_get_pa_gain(struct b43_wldev *dev)
{
	return (b43_phy_read(dev, 0x4fb) & 0x7f00) >> 8;
}

/* wlc_lcnphy_set_dac_gain */
static void b43_phy_lcn_set_dac_gain(struct b43_wldev *dev, u16 dac_gain)
{
	u16 dac_ctrl;

	dac_ctrl = b43_phy_read(dev, 0x439);
	dac_ctrl = dac_ctrl & 0xc7f;
	dac_ctrl = dac_ctrl | (dac_gain << 7);
	b43_phy_maskset(dev, 0x439, ~0xfff, dac_ctrl);
}

/* wlc_lcnphy_set_bbmult */
static void b43_phy_lcn_set_bbmult(struct b43_wldev *dev, u8 m0)
{
	b43_lcntab_write(dev, B43_LCNTAB16(0x00, 0x57), m0 << 8);
}

/* wlc_lcnphy_clear_tx_power_offsets */
static void b43_phy_lcn_clear_tx_power_offsets(struct b43_wldev *dev)
{
	u8 i;

	if (1) { /* FIXME */
		b43_phy_write(dev, B43_PHY_LCN_TABLE_ADDR,
			      (0x7 << 10) | B43_LCN_TX_PWR_CTRL_RATE_OFFSET);
		for (i = 0; i < 30; i++) {
			b43_phy_write(dev, B43_PHY_LCN_TABLE_DATAHI, 0);
			b43_phy_write(dev, B43_PHY_LCN_TABLE_DATALO, 0);
		}
	}

	b43_phy_write(dev, B43_PHY_LCN_TABLE_ADDR, (0x7 << 10) | 0x80);
	for (i = 0; i < 64; i++) {
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATAHI, 0);
		b43_phy_write(dev, B43_PHY_LCN_TABLE_DATALO, 0);
	}
}

/* wlc_lcnphy_set_tx_gain_override */
static void b43_phy_lcn_set_tx_gain_override(struct b43_wldev *dev, bool enable)
{
	b43_phy_maskset(dev, 0x4b0, ~(0x1 << 7), enable << 7);
	b43_phy_maskset(dev, 0x4b0, ~(0x1 << 14), enable << 14);
	b43_phy_maskset(dev, 0x43b, ~(0x1 << 6), enable << 6);
}

/* wlc_lcnphy_set_tx_gain */
static void b43_phy_lcn_set_tx_gain(struct b43_wldev *dev,
				    struct lcn_tx_gains *target_gains)
{
	u16 pa_gain = b43_phy_lcn_get_pa_gain(dev);

	b43_phy_maskset(dev, 0x4b5, 0,
			target_gains->gm_gain | (target_gains->pga_gain << 8));
	b43_phy_maskset(dev, 0x4fb, ~0x7fff,
			(target_gains->pad_gain | (pa_gain << 8)));
	b43_phy_maskset(dev, 0x4fc, 0,
			target_gains->gm_gain | (target_gains->pga_gain << 8));
	b43_phy_maskset(dev, 0x4fd, ~0x7fff,
			(target_gains->pad_gain | (pa_gain << 8)));

	b43_phy_lcn_set_dac_gain(dev, target_gains->dac_gain);
	b43_phy_lcn_set_tx_gain_override(dev, true);
}

/* wlc_lcnphy_set_pa_gain */
static void b43_phy_lcn_set_pa_gain(struct b43_wldev *dev, u16 gain)
{
	b43_phy_maskset(dev, 0x4fb, ~0x7f00, gain << 8);
	b43_phy_maskset(dev, 0x4fd, ~0x7f00, gain << 8);
}

/* wlc_lcnphy_set_tx_iqcc */
static void b43_phy_lcn_set_tx_iqcc(struct b43_wldev *dev, u16 a, u16 b)
{
	u16 iqcc[2] = { a, b };

	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 0x50), 2, iqcc);
}

/* wlc_lcnphy_set_tx_locc */
static void b43_phy_lcn_set_tx_locc(struct b43_wldev *dev, u16 didq)
{
	b43_lcntab_write(dev, B43_LCNTAB16(0x0, 0x55), didq);
}

/* wlc_lcnphy_get_tx_pwr_ctrl */
static u16 b43_phy_lcn_get_tx_pwr_ctrl(struct b43_wldev *dev)
{
	return b43_phy_read(dev, 0x4a4) & B43_LCN_TX_PWR_CTRL_MASK;
}

/* wlc_lcnphy_get_tx_gain */
static void b43_phy_lcn_get_tx_gain(struct b43_wldev *dev,
				    struct lcn_tx_gains *gains)
{
	u16 rfgain0, rfgain1;

	gains->dac_gain = (b43_phy_read(dev, 0x439) & 0x380) >> 7;
	rfgain0 = b43_phy_read(dev, 0x4b5);
	rfgain1 = b43_phy_read(dev, 0x4fb) & 0x7fff;
	gains->gm_gain = rfgain0 & 0xff;
	gains->pga_gain = (rfgain0 >> 8) & 0xff;
	gains->pad_gain = rfgain1 & 0xff;
}

/* wlc_lcnphy_get_bbmult */
static u8 b43_phy_lcn_get_bbmult(struct b43_wldev *dev)
{
	return b43_lcntab_read(dev, B43_LCNTAB16(0x0, 0x57)) >> 8;
}

/* The gains of an index of the TX power table */
static void b43_phy_lcn_load_tx_pwr_index(struct b43_wldev *dev, u8 index)
{
	struct lcn_tx_gains gains;
	u32 iq, gain;

	iq = b43_lcntab_read(dev, B43_LCNTAB32(0x7, 0x140 + index));
	gain = b43_lcntab_read(dev, B43_LCNTAB32(0x7, 0xc0 + index));

	gains.gm_gain = gain & 0xff;
	gains.pga_gain = (gain >> 8) & 0xff;
	gains.pad_gain = (gain >> 16) & 0xff;
	gains.dac_gain = (iq >> 28) & 0x7;
	b43_phy_lcn_set_tx_gain(dev, &gains);
	b43_phy_lcn_set_pa_gain(dev, (gain >> 24) & 0x7f);
	b43_phy_lcn_set_bbmult(dev, (iq >> 20) & 0xff);
	b43_phy_lcn_set_tx_gain_override(dev, true);

	if (dev->phy.lcn->temp_pwr_ctl_capable)
		return;
	b43_phy_lcn_set_tx_iqcc(dev, (iq >> 10) & 0x3ff, iq & 0x3ff);
	b43_phy_lcn_set_tx_locc(dev,
		b43_lcntab_read(dev, B43_LCNTAB32(0x7, 0x1c0 + index)));
	b43_phy_maskset(dev, 0x6a6, ~0x1fff,
		8 * b43_lcntab_read(dev, B43_LCNTAB32(0x7, 0x240 + index)));
}

/*
 * wlc_lcnphy_txpower_recalc_target: the rates' offsets, then the target, which
 * wl writes even when it is unchanged
 */
static void b43_phy_lcn_write_tx_pwr_target(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u32 offsets[B43_LCN_TX_PWR_RATES];
	int i;

	for (i = 0; i < B43_LCN_TX_PWR_RATES; i++)
		offsets[i] = (s32)lcn->tx_pwr_offset[i];
	b43_lcntab_write_bulk(dev, B43_LCNTAB32(0x7,
					B43_LCN_TX_PWR_CTRL_RATE_OFFSET),
			      B43_LCN_TX_PWR_RATES, offsets);
	b43_phy_maskset(dev, 0x4a7, ~0xff, lcn->tx_pwr_target);
}

/*
 * wlc_lcnphy_set_tx_pwr_ctrl. Turned on, it starts from the index and the
 * number of packets of the last estimate. Returns the mode before.
 */
static u16 b43_phy_lcn_set_tx_pwr_ctrl(struct b43_wldev *dev, u16 mode)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u16 old_mode = b43_phy_lcn_get_tx_pwr_ctrl(dev);

	b43_phy_maskset(dev, 0x6da, ~0x40,
			mode == B43_LCN_TX_PWR_CTRL_HW ? 0x40 : 0);
	b43_phy_maskset(dev, 0x6a3, ~0x10,
			mode == B43_LCN_TX_PWR_CTRL_HW ? 0 : 0x10);
	if (old_mode == mode)
		return old_mode;

	if (old_mode == B43_LCN_TX_PWR_CTRL_HW)
		b43_phy_lcn_clear_tx_power_offsets(dev);
	if (mode == B43_LCN_TX_PWR_CTRL_HW) {
		if (lcn->tx_pwr_target)
			b43_phy_lcn_write_tx_pwr_target(dev);
		b43_phy_maskset(dev, 0x4a4, ~0x1ff, lcn->tssi_idx);
		b43_phy_maskset(dev, 0x4a5, ~0x700, lcn->tssi_npt << 8);
		b43_radio_mask(dev, 0x11f, ~0x4);
		lcn->tssi_tx_cnt = b43_shm_read16(dev, B43_SHM_SHARED,
						  B43_SHM_SH_TXALLFRM);
		b43_phy_lcn_set_tx_gain_override(dev, false);
		/* The index the software control sets, as brcmsmac's soft ctrl */
		b43_phy_mask(dev, 0x4a9, (u16)~0x8000);
		lcn->tx_pwr_idx_override = -1;
		b43_phy_set(dev, 0x4a4, 0xc000);
	} else {
		b43_phy_lcn_set_tx_gain_override(dev, true);
	}
	b43_phy_maskset(dev, 0x4a4, (u16)~B43_LCN_TX_PWR_CTRL_MASK, mode);
	return old_mode;
}

/* wlc_lcnphy_set_tx_pwr_by_index */
static void b43_phy_lcn_set_tx_pwr_by_index(struct b43_wldev *dev, u8 index)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;

	lcn->tx_pwr_idx_override = index;
	lcn->tx_pwr_curr_idx = index;

	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	b43_phy_lcn_load_tx_pwr_index(dev, index);
}

/* wlc_lcnphy_rev0_baseband_init */
static void b43_phy_lcn_rev0_baseband_init(struct b43_wldev *dev)
{
	b43_radio_write(dev, 0x11c, 0);

	b43_phy_write(dev, 0x43b, 0);
	b43_phy_write(dev, 0x43c, 0);
	b43_phy_write(dev, 0x44c, 0);
	b43_phy_write(dev, 0x4e6, 0);
	b43_phy_write(dev, 0x4f9, 0);
	b43_phy_write(dev, 0x4b0, 0);
	b43_phy_write(dev, 0x938, 0);
	b43_phy_write(dev, 0x4b0, 0);
	b43_phy_write(dev, 0x44e, 0);

	b43_phy_set(dev, 0x567, 0x03);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	if (!(dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM))
		b43_phy_lcn_set_tx_pwr_by_index(dev, 52);

	b43_phy_maskset(dev, 0x634, ~0xff, 0xc);
	if (dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM) {
		b43_phy_maskset(dev, 0x634, ~0xff, 0xa);
		b43_phy_write(dev, 0x910, 0x1);
	}

	b43_phy_maskset(dev, 0x448, ~0x300, 0x100);
	b43_phy_maskset(dev, 0x608, ~0xff, 0x17);
	b43_phy_maskset(dev, 0x604, ~0x7ff, 0x3ea);
}

/* wlc_lcnphy_bu_tweaks */
static void b43_phy_lcn_bu_tweaks(struct b43_wldev *dev)
{
	b43_phy_set(dev, 0x805, 0x1);

	b43_phy_maskset(dev, 0x42f, ~0x7, 0x5);
	b43_phy_maskset(dev, 0x030, ~0x7, 0x6);

	b43_phy_write(dev, 0x414, 0x1e10);
	b43_phy_write(dev, 0x415, 0x0640);

	b43_phy_maskset(dev, 0x4df, (u16) ~0xff00, 0xf700);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_phy_maskset(dev, 0x434, ~0xff, 0xfd);
	b43_phy_maskset(dev, 0x420, ~0xff, 0x10);

	if (dev->dev->bus_sprom->board_rev >= 0x1204)
		b43_radio_set(dev, 0x09b, 0xf0);

	b43_phy_write(dev, 0x7d6, 0x0902);

	b43_phy_maskset(dev, 0x429, ~0xf, 0x9);
	b43_phy_maskset(dev, 0x429, ~(0x3f << 4), 0xe << 4);

	if (dev->phy.rev == 1) {
		b43_phy_maskset(dev, 0x423, ~0xff, 0x46);
		b43_phy_maskset(dev, 0x411, ~0xff, 1);
		b43_phy_maskset(dev, 0x434, ~0xff, 0xfc);
		b43_phy_maskset(dev, 0x416, ~0xff, 0xa4);
		b43_phy_maskset(dev, 0x416, (u16)~0xff00, 0x9b00);

		b43_phy_maskset(dev, 0x656, ~0xf, 2);
		b43_phy_set(dev, 0x44d, 4);

		b43_radio_set(dev, 0x0f7, 0x4);
		b43_radio_mask(dev, 0x0f1, ~0x3);
		b43_radio_maskset(dev, 0x0f2, ~0xf8, 0x90);
		b43_radio_maskset(dev, 0x0f3, ~0x3, 0x2);
		b43_radio_maskset(dev, 0x0f3, ~0xf0, 0xa0);

		b43_radio_set(dev, 0x11f, 0x2);

		b43_phy_lcn_clear_tx_power_offsets(dev);

		/* TODO: something more? */
	}
}

/* wlc_lcnphy_vbat_temp_sense_setup */
static void b43_phy_lcn_sense_setup(struct b43_wldev *dev,
				    enum lcn_sense_type sense_type)
{
	u8 auxpga_vmidcourse, auxpga_vmidfine, auxpga_gain;
	u16 auxpga_vmid;
	u8 i;

	u16 save_radio_regs[6][2] = {
		{ 0x007, 0 }, { 0x0ff, 0 }, { 0x11f, 0 }, { 0x005, 0 },
		{ 0x025, 0 }, { 0x112, 0 },
	};
	u16 save_phy_regs[14][2] = {
		{ 0x503, 0 }, { 0x4a4, 0 }, { 0x4d0, 0 }, { 0x4d9, 0 },
		{ 0x4da, 0 }, { 0x4a6, 0 }, { 0x938, 0 }, { 0x939, 0 },
		{ 0x4d8, 0 }, { 0x4d0, 0 }, { 0x4d7, 0 }, { 0x4a5, 0 },
		{ 0x40d, 0 }, { 0x4a2, 0 },
	};
	u16 save_radio_4a4;

	msleep(1);

	/* Save */
	for (i = 0; i < 6; i++)
		save_radio_regs[i][1] = b43_radio_read(dev,
						       save_radio_regs[i][0]);
	for (i = 0; i < 14; i++)
		save_phy_regs[i][1] = b43_phy_read(dev, save_phy_regs[i][0]);
	b43_mac_suspend(dev);
	save_radio_4a4 = b43_radio_read(dev, 0x4a4);
	/* wlc_lcnphy_set_tx_pwr_ctrl(pi, LCNPHY_TX_PWR_CTRL_OFF); */

	/* Setup */
	/* TODO: wlc_lcnphy_set_tx_pwr_by_index(pi, 127); */
	b43_radio_set(dev, 0x007, 0x1);
	b43_radio_set(dev, 0x0ff, 0x10);
	b43_radio_set(dev, 0x11f, 0x4);

	b43_phy_mask(dev, 0x503, ~0x1);
	b43_phy_mask(dev, 0x503, ~0x4);
	b43_phy_mask(dev, 0x4a4, ~0x4000);
	b43_phy_mask(dev, 0x4a4, (u16) ~0x8000);
	b43_phy_mask(dev, 0x4d0, ~0x20);
	b43_phy_set(dev, 0x4a5, 0xff);
	b43_phy_maskset(dev, 0x4a5, ~0x7000, 0x5000);
	b43_phy_mask(dev, 0x4a5, ~0x700);
	b43_phy_maskset(dev, 0x40d, ~0xff, 64);
	b43_phy_maskset(dev, 0x40d, ~0x700, 0x600);
	b43_phy_maskset(dev, 0x4a2, ~0xff, 64);
	b43_phy_maskset(dev, 0x4a2, ~0x700, 0x600);
	b43_phy_maskset(dev, 0x4d9, ~0x70, 0x20);
	b43_phy_maskset(dev, 0x4d9, ~0x700, 0x300);
	b43_phy_maskset(dev, 0x4d9, ~0x7000, 0x1000);
	b43_phy_mask(dev, 0x4da, ~0x1000);
	b43_phy_set(dev, 0x4da, 0x2000);
	b43_phy_set(dev, 0x4a6, 0x8000);

	b43_radio_write(dev, 0x025, 0xc);
	b43_radio_set(dev, 0x005, 0x8);
	b43_phy_set(dev, 0x938, 0x4);
	b43_phy_set(dev, 0x939, 0x4);
	b43_phy_set(dev, 0x4a4, 0x1000);

	/* FIXME: don't hardcode */
	b43_lcntab_write(dev, B43_LCNTAB16(0x8, 0x6), 0x640);

	switch (sense_type) {
	case B43_SENSE_TEMP:
		b43_phy_set(dev, 0x4d7, 0x8);
		b43_phy_maskset(dev, 0x4d7, ~0x7000, 0x1000);
		auxpga_vmidcourse = 8;
		auxpga_vmidfine = 0x4;
		auxpga_gain = 2;
		b43_radio_set(dev, 0x082, 0x20);
		break;
	case B43_SENSE_VBAT:
		b43_phy_set(dev, 0x4d7, 0x8);
		b43_phy_maskset(dev, 0x4d7, ~0x7000, 0x3000);
		auxpga_vmidcourse = 7;
		auxpga_vmidfine = 0xa;
		auxpga_gain = 2;
		break;
	}
	auxpga_vmid = (0x200 | (auxpga_vmidcourse << 4) | auxpga_vmidfine);

	b43_phy_set(dev, 0x4d8, 0x1);
	b43_phy_maskset(dev, 0x4d8, ~(0x3ff << 2), auxpga_vmid << 2);
	b43_phy_set(dev, 0x4d8, 0x2);
	b43_phy_maskset(dev, 0x4d8, ~(0x7 << 12), auxpga_gain << 12);
	b43_phy_set(dev, 0x4d0, 0x20);
	b43_radio_write(dev, 0x112, 0x6);

	b43_dummy_transmission(dev, true, false);
	/* Wait if not done */
	if (!(b43_phy_read(dev, 0x476) & 0x8000))
		udelay(10);

	/* Restore */
	for (i = 0; i < 6; i++)
		b43_radio_write(dev, save_radio_regs[i][0],
				save_radio_regs[i][1]);
	for (i = 0; i < 14; i++)
		b43_phy_write(dev, save_phy_regs[i][0], save_phy_regs[i][1]);
	/* TODO: wlc_lcnphy_set_tx_pwr_by_index(tx_pwr_curr_idx) */
	b43_radio_write(dev, 0x4a4, save_radio_4a4);

	b43_mac_enable(dev);

	msleep(1);
}

/* wl's latch of a TX filter's coefficients */
static void b43_phy_lcn_latch_tx_iir_filter(struct b43_wldev *dev)
{
	b43_phy_set(dev, 0x6da, 0x8000);
	b43_phy_set(dev, 0x6d3, 0x8000);
	b43_phy_mask(dev, 0x6d3, (u16)~0x8000);
	b43_phy_mask(dev, 0x6da, (u16)~0x8000);
}

static bool b43_phy_lcn_load_tx_iir_cck_filter(struct b43_wldev *dev,
					       u8 filter_type)
{
	int i, j;
	u16 phy_regs[] = { 0x910, 0x91e, 0x91f, 0x924, 0x925, 0x926, 0x920,
			   0x921, 0x927, 0x928, 0x929, 0x922, 0x923, 0x930,
			   0x931, 0x932 };
	/* From brcmsmac, but type 25, which is wl's */
	struct lcn_tx_iir_filter tx_iir_filters_cck[] = {
		{ 0,  { 1, 415, 1874, 64, 128, 64, 792, 1656, 64, 128, 64, 778,
			1582, 64, 128, 64 } },
		{ 1,  { 1, 402, 1847, 259, 59, 259, 671, 1794, 68, 54, 68, 608,
			1863, 93, 167, 93 } },
		{ 2,  { 1, 415, 1874, 64, 128, 64, 792, 1656, 192, 384, 192,
			778, 1582, 64, 128, 64 } },
		{ 3,  { 1, 302, 1841, 129, 258, 129, 658, 1720, 205, 410, 205,
			754, 1760, 170, 340, 170 } },
		{ 20, { 1, 360, 1884, 242, 1734, 242, 752, 1720, 205, 1845, 205,
			767, 1760, 256, 185, 256 } },
		{ 21, { 1, 360, 1884, 149, 1874, 149, 752, 1720, 205, 1883, 205,
			767, 1760, 256, 273, 256 } },
		{ 22, { 1, 360, 1884, 98, 1948, 98, 752, 1720, 205, 1924, 205,
			767, 1760, 256, 352, 256 } },
		{ 23, { 1, 350, 1884, 116, 1966, 116, 752, 1720, 205, 2008, 205,
			767, 1760, 128, 233, 128 } },
		{ 24, { 1, 325, 1884, 32, 40, 32, 756, 1720, 256, 471, 256, 766,
			1760, 256, 1881, 256 } },
		{ 25, { 1, 299, 0xff5c, 0xffcd, 0xffc0, 0xffcd, 736, 0xfeb8,
			0xff00, 0xfe29, 0xff00, 765, 0xfee0, 0xfefa, 170,
			0xfefa } },
		{ 26, { 1, 277, 1943, 39, 117, 88, 637, 1838, 64, 192, 144, 614,
			1864, 128, 384, 288 } },
		{ 27, { 1, 245, 1943, 49, 147, 110, 626, 1838, 256, 768, 576,
			613, 1864, 128, 384, 288 } },
		{ 30, { 1, 302, 1841, 61, 122, 61, 658, 1720, 205, 410, 205,
			754, 1760, 170, 340, 170 } },
	};

	for (i = 0; i < ARRAY_SIZE(tx_iir_filters_cck); i++) {
		if (tx_iir_filters_cck[i].type == filter_type) {
			b43_phy_write(dev, phy_regs[0],
				      tx_iir_filters_cck[i].values[0]);
			for (j = 0; j < 16; j++)
				b43_phy_write(dev, phy_regs[j],
					      tx_iir_filters_cck[i].values[j]);
			b43_phy_lcn_latch_tx_iir_filter(dev);
			return true;
		}
	}

	return false;
}

static bool b43_phy_lcn_load_tx_iir_ofdm_filter(struct b43_wldev *dev,
						u8 filter_type)
{
	int i, j;
	u16 phy_regs[] = { 0x90f, 0x900, 0x901, 0x906, 0x907, 0x908, 0x902,
			   0x903, 0x909, 0x90a, 0x90b, 0x904, 0x905, 0x90c,
			   0x90d, 0x90e };
	struct lcn_tx_iir_filter tx_iir_filters_ofdm[] = {
		{ 0, { 0, 0xa2, 0x0, 0x100, 0x100, 0x0, 0x0, 0x0, 0x100, 0x0,
		       0x0, 0x278, 0xfea0, 0x80, 0x100, 0x80 } },
		{ 1, { 0, 374, 0xFF79, 16, 32, 16, 799, 0xFE74, 50, 32, 50, 750,
		       0xFE2B, 212, 0xFFCE, 212 } },
		{ 2, { 0, 375, 0xff16, 0xffdb, 0xffb4, 0xffdb, 799, 0xfe74,
		       0xffe0, 0xffec, 0xffe0, 748, 0xfef2, 0xff6c, 35,
		       0xff6c } },
	};

	for (i = 0; i < ARRAY_SIZE(tx_iir_filters_ofdm); i++) {
		if (tx_iir_filters_ofdm[i].type == filter_type) {
			b43_phy_write(dev, phy_regs[0],
				      tx_iir_filters_ofdm[i].values[0]);
			for (j = 0; j < 16; j++)
				b43_phy_write(dev, phy_regs[j],
					      tx_iir_filters_ofdm[i].values[j]);
			b43_phy_lcn_latch_tx_iir_filter(dev);
			return true;
		}
	}

	return false;
}

/**************************************************
 * TX power control.
 **************************************************/

/* wlc_lcnphy_set_tssi_mux, after the PA, as wl */
static void b43_phy_lcn_set_tssi_mux_post_pa(struct b43_wldev *dev)
{
	b43_phy_mask(dev, 0x4d9, ~0x4);
	b43_phy_set(dev, 0x4d9, 0x8);
	b43_radio_set(dev, 0x03a, 0x1);
	b43_radio_set(dev, 0x11a, 0x8);
	b43_radio_write(dev, 0x07f, 0x0);
	b43_radio_mask(dev, 0x028, ~0x1);
	b43_radio_set(dev, 0x11a, 0x4);
	b43_radio_mask(dev, 0x036, ~0x10);
	b43_radio_set(dev, 0x11a, 0x10);
	b43_radio_mask(dev, 0x036, ~0x3);
	b43_radio_maskset(dev, 0x035, ~0xff, 0x77);
	b43_radio_maskset(dev, 0x028, ~0x1e, 0xe << 1);
	b43_radio_set(dev, 0x112, 0x80);
	b43_radio_maskset(dev, 0x005, ~0x7, 0x2);
	b43_radio_mask(dev, 0x029, ~0xf0);
	b43_phy_mask(dev, 0x637, (u16)~0xc000);
}

/* wlc_lcnphy_rfseq_tbl_adc_pwrup */
static u16 b43_phy_lcn_rfseq_tbl_adc_pwrup(struct b43_wldev *dev)
{
	u16 n1, n2, n3, n4, n5, n6, n;

	n1 = b43_phy_read(dev, 0x4a5) & 0xff;
	n2 = 1 << ((b43_phy_read(dev, 0x4a5) >> 12) & 0x7);
	n3 = b43_phy_read(dev, 0x40d) & 0xff;
	n4 = 1 << ((b43_phy_read(dev, 0x40d) >> 8) & 0x7);
	n5 = b43_phy_read(dev, 0x4a2) & 0xff;
	n6 = 1 << ((b43_phy_read(dev, 0x4a2) >> 8) & 0x7);
	n = 2 * (n1 + n2 + n3 + n4 + 2 * (n5 + n6)) + 80;

	return max_t(u16, n, 1600);
}

/* wlc_lcnphy_pwrctrl_rssiparams */
static void b43_phy_lcn_pwrctrl_rssiparams(struct b43_wldev *dev)
{
	struct ssbn_sprom *sprom = dev->dev->bus_sprom;
	u16 vmid = (2 << 8) | (sprom->rssismc2g << 4) | sprom->rssismf2g;
	u16 rssi = vmid | (sprom->rssisav2g << 12);
	u16 temp = ((2 << 8) | (8 << 4) | 4) | (2 << 12);

	b43_phy_mask(dev, 0x4d8, ~0x1);
	b43_phy_mask(dev, 0x4d8, ~0x2);
	b43_phy_mask(dev, 0x4d7, ~0x8);
	b43_phy_maskset(dev, 0x4db, ~0x73ff, rssi);
	b43_phy_maskset(dev, 0x4dc, ~0x73ff, rssi);
	b43_phy_maskset(dev, 0x40a, ~0x73ff, rssi);
	b43_phy_maskset(dev, 0x40b, ~0x73ff, temp);
	b43_phy_maskset(dev, 0x40c, ~0x73ff, temp);
	b43_radio_set(dev, 0x082, 0x20);
	b43_radio_set(dev, 0x07c, 0x1);
}

/* wlc_lcnphy_tssi_setup, after the PA, as wl */
static void b43_phy_lcn_tssi_setup(struct b43_wldev *dev)
{
	int i;

	for (i = 0; i < 128; i++)
		b43_lcntab_write(dev, B43_LCNTAB32(0x7, i), i);
	for (i = 0; i < 128; i++)
		b43_lcntab_write(dev, B43_LCNTAB32(0x7, 0x2c0 + i), i);

	b43_phy_mask(dev, 0x503, ~0x1);
	b43_phy_mask(dev, 0x503, ~0x4);
	b43_phy_set(dev, 0x503, 0x10);
	b43_phy_lcn_set_tssi_mux_post_pa(dev);
	b43_phy_mask(dev, 0x4a4, ~0x4000);
	b43_phy_set(dev, 0x4a4, 0x8000);
	b43_phy_mask(dev, 0x4d0, ~0x20);
	b43_phy_mask(dev, 0x4a4, ~0x1ff);
	b43_phy_maskset(dev, 0x4a5, ~0xff, 0xff);
	b43_phy_maskset(dev, 0x4a5, ~0x7000, 5 << 12);
	b43_phy_mask(dev, 0x4a5, ~0x700);
	b43_phy_maskset(dev, 0x40d, ~0xff, 64);
	b43_phy_maskset(dev, 0x40d, ~0x700, 4 << 8);
	b43_phy_maskset(dev, 0x4a2, ~0xff, 64);
	b43_phy_maskset(dev, 0x4a2, ~0x700, 4 << 8);
	b43_phy_maskset(dev, 0x4a8, ~0xff, 0x1);
	b43_phy_lcn_clear_tx_power_offsets(dev);
	b43_phy_maskset(dev, 0x4d0, (u16)~(0x1ff << 6), 0x1f8 << 6);
	b43_phy_set(dev, 0x4a6, 0x8000);
	b43_phy_maskset(dev, 0x4a6, ~0x1ff, 0xff);
	b43_phy_maskset(dev, 0x49a, ~0x1ff, 0xff);

	b43_radio_maskset(dev, 0x028, ~0x1e, 0xe << 1);
	b43_radio_set(dev, 0x03a, 0x1);
	b43_radio_set(dev, 0x11a, 0x8);
	b43_radio_write(dev, 0x025, 0xc);
	b43_radio_set(dev, 0x03a, 0x2);
	b43_radio_set(dev, 0x005, 0x8);

	b43_phy_mask(dev, 0x4d7, ~0x8);
	b43_phy_maskset(dev, 0x4d7, ~0x7000, 2 << 12);
	b43_phy_mask(dev, 0x4d7, ~0x8);
	b43_lcntab_write(dev, B43_LCNTAB16(0x8, 6),
			 b43_phy_lcn_rfseq_tbl_adc_pwrup(dev));
	b43_phy_set(dev, 0x938, 0x4);
	b43_phy_set(dev, 0x939, 0x4);
	b43_phy_set(dev, 0x4a4, 0x1000);
	b43_phy_set(dev, 0x4d7, 0x4);
	b43_phy_mask(dev, 0x4d7, ~0xf00);
	b43_radio_mask(dev, 0x035, ~0xff);
	b43_radio_mask(dev, 0x036, ~0x3);
	b43_radio_set(dev, 0x11a, 0x8);
	b43_phy_lcn_pwrctrl_rssiparams(dev);
}

/* wlc_lcnphy_idle_tssi_est */
static void b43_phy_lcn_idle_tssi_est(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u16 save_ctrl = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	u8 save_idx = lcn->tx_pwr_curr_idx;
	u16 save_lpfgain, save_afe_switch, save_auxpga, save_aux_en;
	struct lcn_tx_gains old_gains;
	bool old_override;
	u16 tssi;
	u8 save_bbmult;

	b43_cal_begin(dev, B43_CAL_IDLE_TSSI);
	b43_phy_lcn_get_tx_pwr_ctrl(dev);
	save_lpfgain = b43_radio_read(dev, 0x112);
	save_afe_switch = b43_radio_read(dev, 0x007) & 0x1;
	save_auxpga = b43_radio_read(dev, 0x0ff) & 0x10;
	save_aux_en = b43_radio_read(dev, 0x11f) & 0x4;
	b43_phy_read(dev, 0x4ab);

	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	old_override = b43_phy_read(dev, 0x43b) & 0x40;
	b43_phy_lcn_get_tx_gain(dev, &old_gains);
	b43_phy_lcn_set_tx_gain_override(dev, true);
	b43_phy_lcn_set_tx_pwr_by_index(dev, 127);
	b43_radio_set(dev, 0x007, 0x1);
	b43_radio_set(dev, 0x0ff, 0x10);
	b43_radio_set(dev, 0x11f, 0x4);
	b43_phy_lcn_tssi_setup(dev);

	b43_phy_set(dev, 0x4d7, 0x1);
	b43_phy_set(dev, 0x4d7, 0x40);
	save_bbmult = b43_phy_lcn_get_bbmult(dev);
	b43_phy_lcn_set_bbmult(dev, 0);
	b43_dummy_transmission(dev, true, false);
	b43_phy_read(dev, 0x4ab);

	/* The idle TSSI, two's complement both ways: the same 9 bits */
	tssi = b43_phy_read(dev, 0x63e) & 0x1ff;
	b43_cal_report(dev, LCN_CAL_IDLE_TSSI, sign_extend32(tssi, 8));
	b43_phy_maskset(dev, 0x4a6, ~0x1ff, tssi);
	b43_phy_mask(dev, 0x44c, ~0x1000);

	b43_phy_lcn_set_bbmult(dev, save_bbmult);
	b43_phy_lcn_set_tx_gain_override(dev, old_override);
	b43_phy_lcn_set_tx_gain(dev, &old_gains);
	if (save_ctrl != B43_LCN_TX_PWR_CTRL_OFF)
		b43_phy_lcn_set_tx_pwr_ctrl(dev, save_ctrl);
	else
		b43_phy_lcn_set_tx_pwr_by_index(dev, save_idx);

	b43_radio_write(dev, 0x112, save_lpfgain);
	b43_radio_maskset(dev, 0x007, ~0x1, save_afe_switch);
	b43_radio_maskset(dev, 0x0ff, ~0x10, save_auxpga);
	b43_radio_maskset(dev, 0x11f, ~0x4, save_aux_en);
	b43_radio_set(dev, 0x112, 0x80);
}

/* wlc_lcnphy_tssi2dbm */
static s32 b43_phy_lcn_tssi2dbm(s32 tssi, s32 a1, s32 b0, s32 b1)
{
	s32 a = 32768 + a1 * tssi;
	s32 b = 1024 * b0 + 64 * b1 * tssi;

	return (2 * b + a) / (2 * a);
}

/* The power of each TSSI, from the PA's SROM parameters */
static void b43_phy_lcn_load_tssi_pwr(struct b43_wldev *dev)
{
	struct ssbn_sprom *sprom = dev->dev->bus_sprom;
	s32 b0 = (s16)sprom->pa0b0, b1 = (s16)sprom->pa0b1;
	s32 a1 = (s16)sprom->pa0b2;
	s32 pwr, min_pwr;
	int tssi;

	min_pwr = b43_phy_lcn_tssi2dbm(125, a1, b0, b1);
	for (tssi = 0; tssi < 128; tssi++) {
		pwr = b43_phy_lcn_tssi2dbm(tssi, a1, b0, b1);
		b43_lcntab_write(dev, B43_LCNTAB32(0x7, tssi),
				 max(pwr, min_pwr));
	}
}

/* wlc_lcnphy_tx_pwr_ctrl_init, with the hardware control */
static void b43_phy_lcn_tx_pwr_ctl_init_hw(struct b43_wldev *dev)
{

	b43_phy_lcn_idle_tssi_est(dev);
	b43_phy_lcn_clear_tx_power_offsets(dev);
	b43_phy_lcn_load_tssi_pwr(dev);
	b43_phy_mask(dev, 0x4d0, ~0x1);
	b43_phy_mask(dev, 0x4d3, ~0xff);
	b43_phy_mask(dev, 0x4d3, (u16)~0xff00);
	b43_phy_mask(dev, 0x4d0, ~0x10);
	b43_phy_mask(dev, 0x4d0, ~0x4);
	b43_phy_mask(dev, 0x410, ~0x80);
	b43_phy_write(dev, 0x4a8, 10);
	/* The target comes with the TX power's recalculation */
	b43_phy_mask(dev, 0x4a7, ~0xff);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_HW);
}

/* wlc_lcnphy_tx_pwr_ctrl_init */
static void b43_phy_lcn_tx_pwr_ctl_init(struct b43_wldev *dev)
{
	struct lcn_tx_gains tx_gains;
	u8 bbmult;

	b43_mac_suspend(dev);

	if (!dev->phy.lcn->hw_pwr_ctl_capable) {
		if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
			tx_gains.gm_gain = 4;
			tx_gains.pga_gain = 12;
			tx_gains.pad_gain = 12;
			tx_gains.dac_gain = 0;
			bbmult = 150;
		} else {
			tx_gains.gm_gain = 7;
			tx_gains.pga_gain = 15;
			tx_gains.pad_gain = 14;
			tx_gains.dac_gain = 0;
			bbmult = 150;
		}
		b43_phy_lcn_set_tx_gain(dev, &tx_gains);
		b43_phy_lcn_set_bbmult(dev, bbmult);
		b43_phy_lcn_sense_setup(dev, B43_SENSE_TEMP);
	} else {
		b43_phy_lcn_tx_pwr_ctl_init_hw(dev);
	}

	b43_mac_enable(dev);
}

/* wlc_lcnphy_txrx_spur_avoidance_mode */
static void b43_phy_lcn_txrx_spur_avoidance_mode(struct b43_wldev *dev,
						 bool enable)
{
	if (enable) {
		b43_phy_write(dev, 0x942, 0xf);
		b43_phy_write(dev, 0x942, 0x0);
		b43_phy_write(dev, 0x942, 0xf);
		b43_phy_write(dev, 0x93b, ((1 << 13) + 23));
		b43_phy_write(dev, 0x93c, ((1 << 13) + 1989));

		b43_phy_write(dev, 0x44a, 0x084);
		b43_phy_write(dev, 0x44a, 0x080);
		b43_phy_write(dev, 0x6d3, 0x2222);
		b43_phy_write(dev, 0x6d3, 0x2220);
	} else {
		b43_phy_write(dev, 0x942, 0x0);
		b43_phy_write(dev, 0x942, 0xf);
		b43_phy_write(dev, 0x942, 0x0);
		b43_phy_write(dev, 0x93b, ((0 << 13) + 23));
		b43_phy_write(dev, 0x93c, ((0 << 13) + 1989));
	}
	b43_mac_switch_freq(dev, enable);
}

/**************************************************
 * Calibrations.
 **************************************************/

static const u16 b43_phy_lcn_iqcal_loft_ladder[] = {
	0x0200, 0x0300, 0x0400, 0x0600, 0x0800, 0x0b00, 0x1000, 0x1001,
	0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1707, 0x2007,
	0x2d07, 0x4007, 0x5b07, 0x8007,
};

static const u16 b43_phy_lcn_iqcal_ir_ladder[] = {
	0x0100, 0x0200, 0x0400, 0x0600, 0x0800, 0x0b00, 0x1000, 0x1700,
	0x2000, 0x2d00, 0x4000, 0x4001, 0x4002, 0x4003, 0x4004, 0x4005,
	0x4006, 0x4007, 0x5b07, 0x8007,
};

/* The radio registers of the TX I/Q-LO calibration's loopback */
static const u16 b43_phy_lcn_iqlo_loopback_regs[] = {
	0x036, 0x11a, 0x03a, 0x025, 0x028, 0x005, 0x112, 0x0ff, 0x11f, 0x00b,
	0x113, 0x007, 0x0fc, 0x0fd, 0x012, 0x057, 0x059, 0x05c, 0x078, 0x092,
};

/* wlc_lcnphy_deaf_mode, at 20 MHz */
static void b43_phy_lcn_deaf_mode(struct b43_wldev *dev, bool deaf)
{
	b43_phy_maskset(dev, 0x4b0, ~0x20, deaf << 5);
	b43_phy_mask(dev, 0x4b1, ~0x200);
	b43_phy_maskset(dev, 0x410, ~0x60, deaf ? 0 : 0x60);
	b43_phy_maskset(dev, 0x410, ~0x80, deaf << 7);
}

/* wlc_lcnphy_set_trsw_override */
static void b43_phy_lcn_set_trsw_override(struct b43_wldev *dev, bool tx,
					  bool rx)
{
	b43_phy_maskset(dev, 0x44d, ~0x3, (tx ? 0x2 : 0) | (rx ? 0x1 : 0));
	b43_phy_set(dev, 0x44c, 0x3);
}

/* wlc_lcnphy_tx_pu, at 2.4 GHz */
static void b43_phy_lcn_tx_pu(struct b43_wldev *dev, bool enable)
{
	if (!enable) {
		b43_phy_mask(dev, 0x43b, ~0x12);
		b43_phy_set(dev, 0x43c, 0x2);
		b43_phy_mask(dev, 0x44c, ~0x102f);
		b43_phy_mask(dev, 0x44d, ~0x4028);
		b43_phy_set(dev, 0x44d, 0x4);
		b43_phy_maskset(dev, 0x44d, ~0x3, 0x1);
		b43_phy_mask(dev, 0x4f9, ~0x7);
		b43_phy_mask(dev, 0x4fa, ~0x7);
		return;
	}
	b43_phy_set(dev, 0x43b, 0x2);
	b43_phy_mask(dev, 0x43c, ~0x2);
	b43_phy_set(dev, 0x43b, 0x10);
	b43_phy_mask(dev, 0x43c, ~0x40);
	b43_phy_set(dev, 0x44c, 0x1000);
	b43_phy_set(dev, 0x44d, 0x4000);
	b43_phy_lcn_set_trsw_override(dev, true, false);
	b43_phy_mask(dev, 0x44d, ~0x4);
	b43_phy_set(dev, 0x44c, 0x4);
	b43_phy_set(dev, 0x44c, 0x8);
	b43_phy_set(dev, 0x44d, 0x8);
	b43_phy_set(dev, 0x44c, 0x20);
	b43_phy_mask(dev, 0x44d, ~0x20);
	b43_phy_set(dev, 0x4f9, 0x2);
	b43_phy_set(dev, 0x4fa, 0x2);
	b43_phy_set(dev, 0x4f9, 0x4);
	b43_phy_set(dev, 0x4fa, 0x4);
	b43_phy_set(dev, 0x4f9, 0x1);
	b43_phy_set(dev, 0x4fa, 0x1);
}

/* wlc_lcnphy_run_samples, looping until stopped */
static void b43_phy_lcn_run_samples(struct b43_wldev *dev, u16 samps,
				    bool iqcal)
{
	b43_phy_set(dev, 0x6da, 0x8080);
	b43_phy_maskset(dev, 0x642, ~0x7f, samps - 1);
	b43_phy_maskset(dev, 0x640, 0, 0xffff);
	b43_phy_maskset(dev, 0x641, 0, 0);
	if (iqcal) {
		b43_phy_mask(dev, 0x453, (u16)~0x8000);
		b43_phy_set(dev, 0x453, 0x8000);
	} else {
		b43_phy_write(dev, 0x63f, 1);
		b43_phy_lcn_tx_pu(dev, true);
	}
	b43_radio_set(dev, 0x112, 0x6);
}

/* wlc_lcnphy_start_tx_tone: @freq kHz at 40 MHz, as many samples as a turn */
static void b43_phy_lcn_start_tx_tone(struct b43_wldev *dev, s32 freq,
				      u16 max, bool iqcal)
{
	u32 samples[64];
	u16 n, k;
	s32 rot;

	b43_phy_lcn_deaf_mode(dev, true);
	for (k = 1; ; k++) {
		n = 40000 * k / abs(freq);
		if (n * abs(freq) == 40000 * k)
			break;
	}
	rot = b43_tone_rot(freq, 40);
	for (k = 0; k < n; k++)
		samples[k] = b43_tone_pack10(b43_tone_sample(k * rot, max));

	b43_phy_mask(dev, 0x6d6, ~0x3);
	b43_phy_set(dev, 0x6da, 0x8);
	b43_lcntab_write_bulk(dev, B43_LCNTAB32(0x15, 0), n, samples);
	b43_phy_lcn_run_samples(dev, n, iqcal);
}

/* wlc_lcnphy_stop_tx_tone */
static void b43_phy_lcn_stop_tx_tone(struct b43_wldev *dev)
{
	u16 status = b43_phy_read(dev, 0x644);

	if (status & 0x1) {
		b43_phy_lcn_tx_pu(dev, false);
		b43_phy_set(dev, 0x63f, 0x2);
	} else if (status & 0x2) {
		b43_phy_mask(dev, 0x453, (u16)~0x8000);
	}
	b43_phy_maskset(dev, 0x6d6, ~0x3, 0x1);
	b43_phy_mask(dev, 0x6da, ~0x8);
	b43_phy_mask(dev, 0x6da, ~0x80);
	b43_radio_mask(dev, 0x112, ~0x6);
	b43_phy_lcn_deaf_mode(dev, false);
}

/* wlc_lcnphy_tx_iqlo_loopback, at 2.4 GHz */
static void b43_phy_lcn_tx_iqlo_loopback(struct b43_wldev *dev, u16 *save)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(b43_phy_lcn_iqlo_loopback_regs); i++)
		save[i] = b43_radio_read(dev,
					 b43_phy_lcn_iqlo_loopback_regs[i]);
	b43_phy_set(dev, 0x44c, 0x1000);
	b43_phy_set(dev, 0x44d, 0x4000);
	b43_phy_set(dev, 0x44c, 0x800);
	b43_phy_mask(dev, 0x44d, ~0x2000);
	b43_phy_set(dev, 0x43b, 0x2);
	b43_phy_mask(dev, 0x43c, ~0x2);
	b43_phy_set(dev, 0x43b, 0x1);
	b43_phy_mask(dev, 0x43c, ~0x1);
	b43_radio_mask(dev, 0x03a, ~0x6);
	b43_radio_set(dev, 0x11a, 0x1);
	b43_radio_set(dev, 0x036, 0x01);
	b43_radio_set(dev, 0x11a, 0x18);
	udelay(20);
	b43_radio_set(dev, 0x03a, 0x3);
	udelay(20);
	b43_radio_write(dev, 0x025, 0xf);
	b43_radio_maskset(dev, 0x028, ~0x1e, 0x6 << 1);
	udelay(20);
	b43_radio_write(dev, 0x005, 0x8);
	b43_radio_set(dev, 0x112, 0x80);
	udelay(20);
	b43_radio_set(dev, 0x0ff, 0x10);
	b43_radio_set(dev, 0x11f, 0x44);
	udelay(20);
	b43_radio_set(dev, 0x00b, 0x7);
	b43_radio_set(dev, 0x113, 0x10);
	udelay(20);
	b43_radio_write(dev, 0x007, 0x1);
	udelay(20);
	b43_radio_maskset(dev, 0x0fc, ~0x3, 0x2a6 >> 8);
	b43_radio_write(dev, 0x0fd, 0x2a6 & 0xff);
	b43_radio_set(dev, 0x11f, 0x44);
	udelay(20);
	b43_radio_set(dev, 0x0ff, 0x10);
	udelay(20);
	b43_radio_write(dev, 0x012, 0x02);
	b43_radio_set(dev, 0x112, 0x06);
	b43_radio_write(dev, 0x036, 0x11);
	b43_radio_write(dev, 0x059, 0xcc);
	b43_radio_write(dev, 0x05c, 0x2e);
	b43_radio_write(dev, 0x078, 0xd7);
	b43_radio_write(dev, 0x092, 0x15);
}

/* wlc_lcnphy_tx_iqlo_loopback_cleanup */
static void b43_phy_lcn_tx_iqlo_loopback_cleanup(struct b43_wldev *dev,
						 const u16 *save)
{
	int i;

	b43_phy_mask(dev, 0x44c, 0);
	b43_phy_mask(dev, 0x43b, 0xc);
	for (i = 0; i < ARRAY_SIZE(b43_phy_lcn_iqlo_loopback_regs); i++)
		b43_radio_write(dev, b43_phy_lcn_iqlo_loopback_regs[i],
				save[i]);
}

/* wlc_lcnphy_iqcal_wait */
static bool b43_phy_lcn_iqcal_wait(struct b43_wldev *dev)
{
	int i;

	for (i = 0; i < 5000; i++) {
		if (!(b43_phy_read(dev, 0x451) & 0xc000))
			return true;
		udelay(100);
	}
	return false;
}

/* wlc_lcnphy_tx_iqlo_cal, a full calibration with the tone stopped after */
static void b43_phy_lcn_tx_iqlo_cal(struct b43_wldev *dev,
				    struct lcn_tx_gains *target)
{
	/* wl's; brcmsmac starts with one 0x8434 */
	static const u16 cmds[] = {
		0x8421, 0x8423, 0x8334, 0x8084, 0x8267, 0x8056, 0x8234,
	};
	static const u16 cmd_nums[] = {
		0x7a97, 0x7a97, 0x7a97, 0x7a97, 0x7a87, 0x7a87, 0x7b97,
	};
	u16 save_rf[ARRAY_SIZE(b43_phy_lcn_iqlo_loopback_regs)];
	u16 coeffs[11] = { 0 };
	u16 save_rxfe, save_calib, save_ctrl2, old_ctrl, diq, zero = 0;
	struct lcn_tx_gains old_gains;
	bool old_override;
	int i, type;

	save_rxfe = b43_phy_read(dev, 0x6db);
	save_calib = b43_phy_read(dev, 0x6da);
	b43_phy_set(dev, 0x6da, 0x40);
	b43_phy_set(dev, 0x6db, 0x3);

	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 64), 11, coeffs);
	b43_phy_write(dev, 0x6da, 0xffff);
	b43_phy_set(dev, 0x503, 0x8);
	old_ctrl = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	b43_phy_set(dev, 0x4a4, 0x1000);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	save_ctrl2 = b43_phy_read(dev, 0x4db);
	b43_phy_maskset(dev, 0x4db, ~0x3ff, 0x2a6);
	b43_phy_maskset(dev, 0x4db, ~0x7000, 2 << 12);
	b43_phy_lcn_tx_iqlo_loopback(dev, save_rf);

	old_override = b43_phy_read(dev, 0x43b) & 0x40;
	if (old_override)
		b43_phy_lcn_get_tx_gain(dev, &old_gains);
	b43_phy_lcn_set_tx_gain(dev, target);
	b43_phy_write(dev, 0x453, 0xaa9);
	b43_phy_write(dev, 0x93d, 0xc0);
	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 0),
			      ARRAY_SIZE(b43_phy_lcn_iqcal_loft_ladder),
			      b43_phy_lcn_iqcal_loft_ladder);
	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 32),
			      ARRAY_SIZE(b43_phy_lcn_iqcal_ir_ladder),
			      b43_phy_lcn_iqcal_ir_ladder);
	b43_phy_lcn_start_tx_tone(dev, 3750, 88, true);
	b43_phy_write(dev, 0x6da, 0xffff);

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		type = (cmds[i] >> 8) & 0xf;
		b43_phy_write(dev, 0x452, cmd_nums[i]);
		if (type == 3 || type == 4) {
			diq = b43_lcntab_read(dev, B43_LCNTAB16(0x0, 69));
			b43_lcntab_write(dev, B43_LCNTAB16(0x0, 69), zero);
		}
		b43_phy_write(dev, 0x451, cmds[i]);
		if (!b43_phy_lcn_iqcal_wait(dev)) {
			b43err(dev->wl, "TX I/Q-LO calibration timed out\n");
			goto cleanup;
		}
		b43_lcntab_read_bulk(dev, B43_LCNTAB16(0x0, 96), 11, coeffs);
		b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 64), 11, coeffs);
		if (type == 3 || type == 4)
			b43_lcntab_write(dev, B43_LCNTAB16(0x0, 69), diq);
		b43_lcntab_read_bulk(dev, B43_LCNTAB16(0x0, 96), 11, coeffs);
	}
	b43_lcntab_read_bulk(dev, B43_LCNTAB16(0x0, 96), 11, coeffs);
	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 80), 4, coeffs);
	b43_lcntab_write_bulk(dev, B43_LCNTAB16(0x0, 85), 2, &coeffs[5]);

cleanup:
	b43_phy_lcn_tx_iqlo_loopback_cleanup(dev, save_rf);
	b43_phy_lcn_stop_tx_tone(dev);
	b43_phy_write(dev, 0x4db, save_ctrl2);
	b43_phy_write(dev, 0x453, 0);
	if (old_override)
		b43_phy_lcn_set_tx_gain(dev, &old_gains);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, old_ctrl);
	b43_phy_write(dev, 0x6da, save_calib);
	b43_phy_write(dev, 0x6db, save_rxfe);
}

/* The TX I/Q and LO coefficients, for every index of the TX power table */
static void b43_phy_lcn_load_tx_iqlo(struct b43_wldev *dev, u16 a, u16 b,
				     u16 didq)
{
	u32 val;
	int i;

	for (i = 0; i < 128; i++) {
		val = b43_lcntab_read(dev, B43_LCNTAB32(0x7, 0x140 + i));
		val = (val & 0xfff00000) | ((u32)(a & 0x3ff) << 10) |
		      (b & 0x3ff);
		b43_lcntab_write(dev, B43_LCNTAB32(0x7, 0x140 + i), val);
		b43_lcntab_write(dev, B43_LCNTAB32(0x7, 0x1c0 + i), didq);
	}
}

/*
 * wlc_lcnphy_txpwrtbl_iqlo_cal: the I/Q and LO coefficients of index 30's
 * gains, for every index of the TX power table.
 */
static void b43_phy_lcn_txpwrtbl_iqlo_cal(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	struct lcn_tx_gains target = {
		.gm_gain = 7, .pga_gain = 0, .pad_gain = 21, .dac_gain = 0,
	};
	struct lcn_tx_gains old_gains;
	u16 save_ctrl = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	u16 save_pa_gain, iqcc[2], didq;
	u8 save_bbmult, save_idx = lcn->tx_pwr_curr_idx;
	int i;

	b43_cal_begin(dev, B43_CAL_TXIQLO);
	b43_phy_lcn_get_tx_gain(dev, &old_gains);
	save_pa_gain = b43_phy_lcn_get_pa_gain(dev);
	save_bbmult = b43_phy_lcn_get_bbmult(dev);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	b43_phy_lcn_set_tx_pwr_by_index(dev, 30);
	b43_phy_lcn_tx_iqlo_cal(dev, &target);

	b43_lcntab_read_bulk(dev, B43_LCNTAB16(0x0, 80), 2, iqcc);
	didq = b43_lcntab_read(dev, B43_LCNTAB16(0x0, 85));
	b43_phy_lcn_load_tx_iqlo(dev, iqcc[0], iqcc[1], didq);
	lcn->txiqcc[0] = sign_extend32(iqcc[0], 9);
	lcn->txiqcc[1] = sign_extend32(iqcc[1], 9);
	lcn->txdidq = didq;
	for (i = 0; i < 4; i++)
		lcn->loft[i] = b43_radio_read(dev, 0x089 + i);
	b43_cal_report(dev, LCN_CAL_TXIQLO, lcn->txiqcc[0], lcn->txiqcc[1],
		       lcn->txdidq, lcn->loft[0], lcn->loft[1], lcn->loft[2],
		       lcn->loft[3]);

	b43_phy_lcn_set_bbmult(dev, save_bbmult);
	b43_phy_lcn_set_pa_gain(dev, save_pa_gain);
	b43_phy_lcn_set_tx_gain(dev, &old_gains);
	if (save_ctrl != B43_LCN_TX_PWR_CTRL_OFF)
		b43_phy_lcn_set_tx_pwr_ctrl(dev, save_ctrl);
	else
		b43_phy_lcn_set_tx_pwr_by_index(dev, save_idx);
}

/* The radio registers the RX I/Q calibration saves */
static const u16 b43_phy_lcn_rxiq_cal_regs[] = {
	0x098, 0x116, 0x12c, 0x06a, 0x00b, 0x01b, 0x113, 0x01d, 0x114, 0x02e,
	0x12a,
};

/* wlc_lcnphy_clear_trsw_override */
static void b43_phy_lcn_clear_trsw_override(struct b43_wldev *dev)
{
	b43_phy_mask(dev, 0x44c, ~0x3);
}

/* wlc_lcnphy_rx_gain_override_enable, rev 1 at 2.4 GHz */
static void b43_phy_lcn_rx_gain_override(struct b43_wldev *dev, bool enable)
{
	b43_phy_maskset(dev, 0x4b0, ~0x100, enable << 8);
	b43_phy_maskset(dev, 0x44c, ~0x1, enable);
	b43_phy_maskset(dev, 0x44c, ~0x10, enable << 4);
	b43_phy_maskset(dev, 0x44c, ~0x40, enable << 6);
	b43_phy_maskset(dev, 0x4b0, ~0x20, enable << 5);
	b43_phy_maskset(dev, 0x4b0, ~0x40, enable << 6);
	b43_phy_maskset(dev, 0x4b0, ~0x400, enable << 10);
	b43_phy_maskset(dev, 0x4e5, ~0x8, enable << 3);
}

/* wlc_lcnphy_set_rx_gain_by_distribution, rev 1, no TR switch nor LNA */
static void b43_phy_lcn_set_rx_gain(struct b43_wldev *dev, u16 biq1,
				    u16 tia, u16 lna2)
{
	b43_phy_maskset(dev, 0x4b6, 0, ((biq1 & 0xf) << 12) |
			((tia & 0xf) << 8) | ((lna2 & 0x3) << 6) |
			((lna2 & 0x3) << 4));
	b43_phy_mask(dev, 0x4b7, ~0xf);
	b43_phy_mask(dev, 0x4b1, ~0x1800);
	b43_phy_mask(dev, 0x4b1, ~0x200);
	b43_phy_mask(dev, 0x4b1, ~0x400);
	b43_phy_set(dev, 0x44d, 0x1);
}

/* wlc_lcnphy_set_rx_iq_comp */
static void b43_phy_lcn_set_rx_iq_comp(struct b43_wldev *dev, u16 a, u16 b)
{
	b43_phy_maskset(dev, 0x645, ~0x3ff, a);
	b43_phy_maskset(dev, 0x646, ~0x3ff, b);
	b43_phy_maskset(dev, 0x647, ~0x3ff, a);
	b43_phy_maskset(dev, 0x648, ~0x3ff, b);
	b43_phy_maskset(dev, 0x649, ~0x3ff, a);
	b43_phy_maskset(dev, 0x64a, ~0x3ff, b);
}

/* wlc_lcnphy_rx_iq_est */
static bool b43_phy_lcn_rx_iq_est(struct b43_wldev *dev, u16 samps, u8 wait,
				  s32 *iq, u32 *ii, u32 *qq)
{
	bool done = false;
	int i;

	b43_phy_set(dev, 0x6da, 0x20);
	b43_phy_mask(dev, 0x410, ~0x8);
	b43_phy_maskset(dev, 0x482, 0, samps);
	b43_phy_maskset(dev, 0x481, ~0xff, wait);
	b43_phy_mask(dev, 0x481, ~0x100);
	b43_phy_set(dev, 0x481, 0x200);
	for (i = 0; i < 5000; i++) {
		if (!(b43_phy_read(dev, 0x481) & 0x200)) {
			done = true;
			break;
		}
		udelay(100);
	}
	if (done) {
		*iq = (b43_phy_read(dev, 0x483) << 16) |
		      b43_phy_read(dev, 0x484);
		*ii = (b43_phy_read(dev, 0x485) << 16) |
		      b43_phy_read(dev, 0x486);
		*qq = (b43_phy_read(dev, 0x487) << 16) |
		      b43_phy_read(dev, 0x488);
	}
	b43_phy_set(dev, 0x410, 0x8);
	b43_phy_mask(dev, 0x6da, ~0x20);
	return done;
}

/* wlc_lcnphy_calc_rx_iq_comp */
static bool b43_phy_lcn_calc_rx_iq_comp(struct b43_wldev *dev, u16 samps)
{
	u16 a0 = b43_phy_read(dev, 0x645) & 0x3ff;
	u16 b0 = b43_phy_read(dev, 0x646) & 0x3ff;
	bool ok = false;
	s32 iq, a, b;
	u32 ii, qq;

	b43_phy_mask(dev, 0x6d1, ~0x4);
	b43_phy_set(dev, 0x64b, 0x40);
	b43_phy_lcn_set_rx_iq_comp(dev, 0, 0);
	if (b43_phy_lcn_rx_iq_est(dev, samps, 32, &iq, &ii, &qq) &&
	    b43_rx_iq_comp(iq, ii, qq, &a, &b) == B43_RX_IQ_COMP_OK) {
		a0 = a & 0x3ff;
		b0 = b & 0x3ff;
		ok = true;
	}
	b43_phy_lcn_set_rx_iq_comp(dev, a0, b0);
	b43_phy_set(dev, 0x64b, 0x1);
	b43_phy_set(dev, 0x64b, 0x8);
	b43_cal_report(dev, LCN_CAL_RXIQ_COMP, ok, sign_extend32(a0, 9),
		       sign_extend32(b0, 9));
	dev->phy.lcn->rxiqcomp[0] = a0;
	dev->phy.lcn->rxiqcomp[1] = b0;
	return ok;
}

/*
 * wlc_lcnphy_rx_iq_cal_gain: the loopback tone at half and at full
 * amplitude, wl's 60 and 120 where brcmsmac has 20 and 40; the gain fits if
 * the powers grow by 3 to 7 dB.
 */
static bool b43_phy_lcn_rx_iq_cal_gain(struct b43_wldev *dev, u16 biq1,
				       u16 tia, u16 lna2)
{
	s32 iq;
	u32 il, ql, ih, qh;

	b43_phy_lcn_set_rx_gain(dev, biq1, tia, lna2);
	b43_phy_lcn_rx_gain_override(dev, true);
	b43_phy_lcn_start_tx_tone(dev, 2000, 60, false);
	udelay(500);
	b43_radio_write(dev, 0x112, 0);
	if (!b43_phy_lcn_rx_iq_est(dev, 1024, 32, &iq, &il, &ql))
		return false;
	b43_phy_lcn_start_tx_tone(dev, 2000, 120, false);
	udelay(500);
	b43_radio_write(dev, 0x112, 0);
	if (!b43_phy_lcn_rx_iq_est(dev, 1024, 32, &iq, &ih, &qh))
		return false;

	return ih > 2 * il && ih < 5 * il && qh > 2 * ql && qh < 5 * ql;
}

/* wlc_lcnphy_rx_iq_cal, on the loopback, with the TX gains of @tx_idx */
static bool b43_phy_lcn_rx_iq_cal(struct b43_wldev *dev, u8 tx_idx)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u16 save_rf[ARRAY_SIZE(b43_phy_lcn_rxiq_cal_regs)];
	static const u16 save_regs[] = {
		0x631, 0x44c, 0x44d, 0x4b0, 0x4b1, 0x4f9, 0x4fa, 0x938,
		0x939, 0x43b, 0x43c, 0x6da, 0x6db,
	};
	u16 save[ARRAY_SIZE(save_regs)];
	u16 ctrl = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	int lna2, tia, biq1;
	bool override, ok = false;
	u8 old_idx = 0;
	int i;

	b43_cal_begin(dev, B43_CAL_RXIQ);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	for (i = 0; i < ARRAY_SIZE(b43_phy_lcn_rxiq_cal_regs); i++)
		save_rf[i] = b43_radio_read(dev, b43_phy_lcn_rxiq_cal_regs[i]);
	save[0] = b43_phy_read(dev, save_regs[0]);
	b43_phy_set(dev, 0x631, 0x0015);
	for (i = 1; i < ARRAY_SIZE(save_regs); i++)
		save[i] = b43_phy_read(dev, save_regs[i]);

	override = b43_phy_read(dev, 0x43b) & 0x40;
	if (override)
		old_idx = lcn->tx_pwr_curr_idx;
	b43_phy_lcn_set_tx_pwr_by_index(dev, tx_idx);
	b43_phy_set(dev, 0x4f9, 0x1);
	b43_phy_mask(dev, 0x4fa, ~0x1);
	b43_phy_set(dev, 0x43b, 0x2);
	b43_phy_mask(dev, 0x43c, ~0x2);
	b43_radio_write(dev, 0x116, 0x06);
	b43_radio_write(dev, 0x12c, 0x07);
	b43_radio_write(dev, 0x06a, 0xd3);
	b43_radio_write(dev, 0x098, 0x03);
	b43_radio_write(dev, 0x00b, 0x7);
	b43_radio_set(dev, 0x113, 0x10);
	b43_radio_write(dev, 0x01d, 0x01);
	b43_radio_write(dev, 0x114, 0x01);
	b43_radio_write(dev, 0x02e, 0x10);
	b43_radio_write(dev, 0x12a, 0x08);
	b43_phy_set(dev, 0x938, 0x1);
	b43_phy_mask(dev, 0x939, ~0x1);
	b43_phy_set(dev, 0x938, 0x2);
	b43_phy_set(dev, 0x939, 0x2);
	b43_phy_set(dev, 0x938, 0x4);
	b43_phy_set(dev, 0x939, 0x4);
	b43_phy_set(dev, 0x938, 0x8);
	b43_phy_set(dev, 0x939, 0x8);
	b43_phy_set(dev, 0x938, 0x20);
	b43_phy_mask(dev, 0x939, ~0x20);
	b43_phy_set(dev, 0x43b, 0x1);
	b43_phy_mask(dev, 0x43c, ~0x1);
	b43_phy_write(dev, 0x6da, 0xffff);
	b43_phy_set(dev, 0x6db, 0x3);
	b43_phy_lcn_set_trsw_override(dev, true, false);

	for (lna2 = 3; lna2 >= 0; lna2--)
		for (tia = 4; tia >= 0; tia--)
			for (biq1 = 6; biq1 >= 0; biq1--)
				if (b43_phy_lcn_rx_iq_cal_gain(dev, biq1, tia,
							       lna2)) {
					b43_cal_report(dev, LCN_CAL_RXIQ_GAIN,
						       lna2, tia, biq1);
					ok = b43_phy_lcn_calc_rx_iq_comp(dev,
									 1024);
					goto stop_tone;
				}
stop_tone:
	b43_phy_lcn_stop_tx_tone(dev);
	/* brcmsmac's: 0x44c gets 0x44d's old value too */
	b43_phy_write(dev, 0x631, save[0]);
	b43_phy_write(dev, 0x44c, save[2]);
	for (i = 2; i < ARRAY_SIZE(save_regs); i++)
		b43_phy_write(dev, save_regs[i], save[i]);
	b43_phy_lcn_clear_trsw_override(dev);
	b43_phy_mask(dev, 0x44c, ~0x4);
	for (i = 0; i < ARRAY_SIZE(b43_phy_lcn_rxiq_cal_regs); i++)
		b43_radio_write(dev, b43_phy_lcn_rxiq_cal_regs[i], save_rf[i]);
	if (override)
		b43_phy_lcn_set_tx_pwr_by_index(dev, old_idx);
	else
		b43_phy_lcn_set_tx_gain_override(dev, false);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, ctrl);
	b43_phy_lcn_rx_gain_override(dev, false);
	return ok;
}

/* wlc_lcnphy_periodic_cal, in wl's order */
static void b43_phy_lcn_periodic_cal(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u16 save_ctrl = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	u8 index = lcn->tx_pwr_curr_idx;

	b43_mac_suspend(dev);
	b43_phy_lcn_deaf_mode(dev, true);
	if (lcn->hw_pwr_ctl_capable) {
		b43_phy_lcn_idle_tssi_est(dev);
		b43_phy_lcn_load_tssi_pwr(dev);
	}
	b43_phy_lcn_txpwrtbl_iqlo_cal(dev);
	b43_phy_lcn_rx_iq_cal(dev, 40);
	lcn->cal_done = true;
	b43_phy_lcn_set_tx_pwr_by_index(dev, index);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, save_ctrl);
	b43_phy_lcn_deaf_mode(dev, false);
	b43_mac_enable(dev);
}

/**************************************************
 * Channel switching ops.
 **************************************************/

/* wlc_lcnphy_set_chanspec_tweaks */
static void b43_phy_lcn_set_channel_tweaks(struct b43_wldev *dev, int channel)
{
	struct bcmn_drv_cc *cc = &dev->dev->bdev->bus->drv_cc;

	/* wl's, before brcmsmac's tweaks */
	b43_phy_set(dev, 0x780, 0x400);
	b43_phy_set(dev, 0x780, 0x200);
	b43_phy_maskset(dev, 0x448, ~0x300, (channel == 14) ? 0x200 : 0x100);

	if (channel == 1 || channel == 2 || channel == 3 || channel == 4 ||
	    channel == 9 || channel == 10 || channel == 11 || channel == 12) {
		bcmn_chipco_pll_write(cc, 0x2, 0x03000c04);
		bcmn_chipco_pll_maskset(cc, 0x3, 0x00ffffff, 0x0);
		bcmn_chipco_pll_write(cc, 0x4, 0x200005c0);

		bcmn_cc_set32(cc, BCMN_CC_PMU_CTL, 0x400);

		b43_phy_write(dev, 0x942, 0);

		b43_phy_lcn_txrx_spur_avoidance_mode(dev, false);
		b43_phy_maskset(dev, 0x424, (u16) ~0xff00, 0x1b00);
		b43_phy_write(dev, 0x425, 0x5907);
	} else {
		bcmn_chipco_pll_write(cc, 0x2, 0x03140c04);
		bcmn_chipco_pll_maskset(cc, 0x3, 0x00ffffff, 0x333333);
		bcmn_chipco_pll_write(cc, 0x4, 0x202c2820);

		bcmn_cc_set32(cc, BCMN_CC_PMU_CTL, 0x400);

		b43_phy_write(dev, 0x942, 0);

		b43_phy_lcn_txrx_spur_avoidance_mode(dev, true);
		b43_phy_maskset(dev, 0x424, (u16) ~0xff00, 0x2500);
		b43_phy_write(dev, 0x425, 0x590f);
	}

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);
}

/* wl's pulse of an AFE override after a tune: on, its value on and off, off */
static void b43_phy_lcn_afe_pulse(struct b43_wldev *dev, u16 bit)
{
	b43_phy_set(dev, B43_PHY_LCN_AFE_CTL1, bit);
	b43_phy_set(dev, B43_PHY_LCN_AFE_CTL2, bit);
	b43_phy_mask(dev, B43_PHY_LCN_AFE_CTL2, ~bit);
	b43_phy_mask(dev, B43_PHY_LCN_AFE_CTL1, ~bit);
}

/* The calibration's results on a new channel, as wl loads them */
static void b43_phy_lcn_load_cal(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	int i;

	b43_phy_lcn_set_tx_iqcc(dev, lcn->txiqcc[0], lcn->txiqcc[1]);
	b43_phy_lcn_set_tx_locc(dev, lcn->txdidq);
	b43_phy_lcn_load_tx_iqlo(dev, lcn->txiqcc[0], lcn->txiqcc[1],
				 lcn->txdidq);
	for (i = 0; i < 4; i++)
		b43_radio_write(dev, 0x089 + i, lcn->loft[i]);
	b43_phy_lcn_set_rx_iq_comp(dev, lcn->rxiqcomp[0], lcn->rxiqcomp[1]);
}

/* wlc_phy_chanspec_set_lcnphy */
static int b43_phy_lcn_set_channel(struct b43_wldev *dev,
				   struct ieee80211_channel *channel,
				   enum nl80211_channel_type channel_type)
{
	static const u16 sfo_cfg[14][2] = {
		{965, 1087}, {967, 1085}, {969, 1082}, {971, 1080}, {973, 1078},
		{975, 1076}, {977, 1073}, {979, 1071}, {981, 1069}, {983, 1067},
		{985, 1065}, {987, 1063}, {989, 1060}, {994, 1055},
	};

	b43_phy_lcn_set_channel_tweaks(dev, channel->hw_value);

	b43_phy_set(dev, 0x44a, 0x44);
	b43_phy_write(dev, 0x44a, 0x80);

	b43_radio_2064_channel_setup(dev, channel->hw_value,
				     channel->center_freq);
	mdelay(1);

	b43_phy_lcn_afe_set_unset(dev);

	b43_phy_write(dev, 0x657, sfo_cfg[channel->hw_value - 1][0]);
	b43_phy_write(dev, 0x658, sfo_cfg[channel->hw_value - 1][1]);

	if (channel->hw_value == 14) {
		b43_phy_maskset(dev, 0x448, ~(0x3 << 8), (2) << 8);
		b43_phy_lcn_load_tx_iir_cck_filter(dev, 3);
	} else {
		b43_phy_maskset(dev, 0x448, ~(0x3 << 8), (1) << 8);
		/* brcmsmac uses filter_type 2, we follow wl with 25 */
		b43_phy_lcn_load_tx_iir_cck_filter(dev, 25);
	}
	/* brcmsmac has 0 with a FEM, and 3, which it lacks, without */
	if (dev->dev->bus_sprom->boardflags_lo & B43_BFL_FEM)
		b43_phy_lcn_load_tx_iir_ofdm_filter(dev, 0);
	else
		b43_phy_lcn_load_tx_iir_ofdm_filter(dev, 2);

	b43_phy_maskset(dev, 0x4eb, ~(0x7 << 3), 0x1 << 3);
	b43_phy_lcn_afe_pulse(dev, 0x1);
	b43_phy_lcn_afe_pulse(dev, 0x2);

	if (dev->phy.lcn->cal_done)
		b43_phy_lcn_load_cal(dev);

	return 0;
}

/**************************************************
 * Basic PHY ops.
 **************************************************/

static int b43_phy_lcn_op_allocate(struct b43_wldev *dev)
{
	struct b43_phy_lcn *phy_lcn;

	phy_lcn = kzalloc_obj(*phy_lcn);
	if (!phy_lcn)
		return -ENOMEM;
	dev->phy.lcn = phy_lcn;

	return 0;
}

static void b43_phy_lcn_op_free(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lcn *phy_lcn = phy->lcn;

	kfree(phy_lcn);
	phy->lcn = NULL;
}

static void b43_phy_lcn_op_prepare_structs(struct b43_wldev *dev)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lcn *phy_lcn = phy->lcn;

	memset(phy_lcn, 0, sizeof(*phy_lcn));

	/* wlc_phy_attach_lcnphy: rev 1 */
	if (dev->dev->bus_sprom->tempsense_option == 3)
		phy_lcn->hw_pwr_ctl_capable = true;
	else
		phy_lcn->temp_pwr_ctl_capable = true;
	phy_lcn->tssi_idx = B43_LCN_TX_PWR_CTRL_START_IDX;
	phy_lcn->tssi_npt = B43_LCN_TX_PWR_CTRL_START_NPT;
}

/* wlc_phy_init_lcnphy */
static int b43_phy_lcn_op_init(struct b43_wldev *dev)
{
	struct bcmn_drv_cc *cc = &dev->dev->bdev->bus->drv_cc;

	b43_phy_set(dev, 0x44a, 0x80);
	b43_phy_mask(dev, 0x44a, 0x7f);
	b43_phy_set(dev, 0x6d1, 0x80);
	b43_phy_write(dev, 0x6d0, 0xf);

	b43_phy_lcn_afe_set_unset(dev);

	b43_phy_write(dev, 0x60a, 0xa0);
	b43_phy_write(dev, 0x46a, 0x19);
	b43_phy_maskset(dev, 0x663, 0xFF00, 0x50);

	b43_phy_lcn_tables_init(dev);

	b43_phy_lcn_rev0_baseband_init(dev);
	b43_phy_lcn_bu_tweaks(dev);

	if (dev->phy.radio_ver == 0x2064)
		b43_radio_2064_init(dev);
	else
		B43_WARN_ON(1);

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
		b43_phy_lcn_tx_pwr_ctl_init(dev);

	b43_switch_channel(dev, dev->phy.channel);

	bcmn_chipco_regctl_maskset(cc, 0, ~0xf, 0x9);
	bcmn_chipco_chipctl_maskset(cc, 0, 0, 0x03cddddd);

	b43_phy_set(dev, 0x448, 0x4000);
	udelay(100);
	b43_phy_mask(dev, 0x448, ~0x4000);

	if (dev->phy.lcn->hw_pwr_ctl_capable)
		b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_HW);
	b43_phy_lcn_periodic_cal(dev);

	return 0;
}

static void b43_phy_lcn_op_software_rfkill(struct b43_wldev *dev,
					bool blocked)
{
	if (b43_read32(dev, B43_MMIO_MACCTL) & B43_MACCTL_ENABLED)
		b43err(dev->wl, "MAC not suspended\n");

	if (blocked) {
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL2, ~0x7c00);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL1, 0x1f00);

		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL5, ~0x7f00);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL4, ~0x2);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL3, 0x808);

		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL7, ~0x8);
		b43_phy_set(dev, B43_PHY_LCN_RF_CTL6, 0x8);
	} else {
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL1, ~0x1f00);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL3, ~0x808);
		b43_phy_mask(dev, B43_PHY_LCN_RF_CTL6, ~0x8);
	}
}

static void b43_phy_lcn_op_switch_analog(struct b43_wldev *dev, bool on)
{
	if (on) {
		b43_phy_mask(dev, B43_PHY_LCN_AFE_CTL1, ~0x7);
	} else {
		b43_phy_set(dev, B43_PHY_LCN_AFE_CTL2, 0x7);
		b43_phy_set(dev, B43_PHY_LCN_AFE_CTL1, 0x7);
	}
}

static int b43_phy_lcn_op_switch_channel(struct b43_wldev *dev,
					unsigned int new_channel)
{
	struct ieee80211_channel *channel = dev->wl->hw->conf.chandef.chan;
	enum nl80211_channel_type channel_type =
		cfg80211_get_chandef_type(&dev->wl->hw->conf.chandef);

	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return b43_phy_lcn_set_channel(dev, channel, channel_type);
}

static unsigned int b43_phy_lcn_op_get_default_chan(struct b43_wldev *dev)
{
	if (b43_current_band(dev->wl) == NL80211_BAND_2GHZ)
		return 1;
	return 36;
}

/*
 * wl's locale for the CQ57's ccode, in qdBm before the backoff: measured, as
 * the HT-PHY's, from the targets wl writes on the channels of its scans
 * (58 on 1-11, 40 on 12-13). Only the rows that bind.
 */
static const struct b43_ppr_locale b43_phy_lcn_locale_ofdm[] = {
	{ 1, 11, 64 },
	{ 12, 13, 44 },
};

static const struct b43_ppr_locale b43_phy_lcn_locale_cck[] = {
	{ 12, 13, 52 },
};

/* wl's backoff of the OFDM and MCS rates, where brcmsmac has 6 */
#define B43_LCN_TX_PWR_BACKOFF		4
/* The CCK offset of brcmsmac's wlc_lcnphy_set_tx_pwr_soft_ctrl() */
#define B43_LCN_TX_PWR_CCK_OFFSET	22

/*
 * The target is the lowest power of the OFDM and MCS rates, the loop holding
 * each frame at its rate's offset from it. The CCK rates have their own
 * chain, without the backoff and with their own locale, and an offset of the
 * hardware: an approximation of wl's, which on the CQ57 gives its offsets.
 */
static enum b43_txpwr_result
b43_phy_lcn_op_recalc_txpower(struct b43_wldev *dev, bool ignore_tssi)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_lcn *lcn = phy->lcn;
	const struct ssbn_sprom *sprom = dev->dev->bus_sprom;
	struct ieee80211_channel *chan = phy->chandef->chan;
	s8 offset[B43_LCN_TX_PWR_RATES];
	u8 cck[B43_PPR_CCK_RATES], limit, target;
	struct b43_ppr ppr;
	unsigned int i;

	if (!lcn->hw_pwr_ctl_capable || chan->band != NL80211_BAND_2GHZ)
		return B43_TXPWR_RES_DONE;

	b43_ppr_load_sprom_legacy(&ppr, sprom, B43_BAND_2G, true);
	if (chan->max_power)
		b43_ppr_apply_max(&ppr, max_t(int, INT_TO_Q52(chan->max_power) -
					     sprom->antenna_gain.a0, 0));
	if (phy->desired_txpower)
		b43_ppr_apply_max(&ppr, INT_TO_Q52(phy->desired_txpower));

	limit = b43_ppr_locale_limit(b43_phy_lcn_locale_cck,
				     ARRAY_SIZE(b43_phy_lcn_locale_cck),
				     chan->hw_value);
	for (i = 0; i < B43_PPR_CCK_RATES; i++) {
		cck[i] = b43_ppr_get_cck(&ppr, i);
		if (limit)
			cck[i] = min(cck[i], limit);
	}

	limit = b43_ppr_locale_limit(b43_phy_lcn_locale_ofdm,
				     ARRAY_SIZE(b43_phy_lcn_locale_ofdm),
				     chan->hw_value);
	if (limit)
		b43_ppr_apply_max(&ppr, limit);
	b43_ppr_add(&ppr, -B43_LCN_TX_PWR_BACKOFF);
	target = b43_ppr_get_bw_min(&ppr, B43_PPR_BW_20);

	for (i = 0; i < B43_PPR_CCK_RATES; i++)
		offset[i] = target - cck[i] + B43_LCN_TX_PWR_CCK_OFFSET;
	for (i = 0; i < B43_PPR_OFDM_RATES; i++)
		offset[B43_PPR_CCK_RATES + i] = target -
			b43_ppr_get_ofdm(&ppr, B43_PPR_BW_20, B43_PPR_SISO, i);
	for (i = 0; i < 8; i++)
		offset[B43_PPR_CCK_RATES + B43_PPR_OFDM_RATES + i] = target -
			b43_ppr_get_mcs(&ppr, B43_PPR_BW_20, B43_PPR_SISO, i);

	b43_cal_begin(dev, B43_CAL_TXPWR);
	if (b43_cal_on(dev)) {
		u32 v[1 + B43_LCN_TX_PWR_RATES];

		v[0] = target;
		for (i = 0; i < B43_LCN_TX_PWR_RATES; i++)
			v[1 + i] = offset[i];
		b43_cal_report_values(dev, LCN_CAL_TXPWR, v, ARRAY_SIZE(v));
	}

	if (target == lcn->tx_pwr_target &&
	    !memcmp(offset, lcn->tx_pwr_offset, sizeof(offset)))
		return B43_TXPWR_RES_DONE;
	if (target != lcn->tx_pwr_target) {
		lcn->tssi_idx = B43_LCN_TX_PWR_CTRL_START_IDX;
		lcn->tssi_npt = B43_LCN_TX_PWR_CTRL_RESET_NPT;
	}
	lcn->tx_pwr_target = target;
	memcpy(lcn->tx_pwr_offset, offset, sizeof(offset));
	return B43_TXPWR_RES_NEED_ADJUST;
}

/* wlc_phy_txpower_recalc_target_lcnphy, with the hardware control */
static void b43_phy_lcn_op_adjust_txpower(struct b43_wldev *dev)
{
	struct b43_phy_lcn *lcn = dev->phy.lcn;
	u16 mode;

	if (!lcn->tx_pwr_target)
		return;

	b43_mac_suspend(dev);
	mode = b43_phy_lcn_get_tx_pwr_ctrl(dev);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, B43_LCN_TX_PWR_CTRL_OFF);
	b43_phy_lcn_write_tx_pwr_target(dev);
	b43_phy_lcn_set_tx_pwr_ctrl(dev, mode);
	b43_mac_enable(dev);
}

/* After every channel switch wl writes the TX power again, changed or not */
static void b43_phy_lcn_op_channel_calibrate(struct b43_wldev *dev,
					     bool cac_done)
{
	b43_phy_lcn_op_recalc_txpower(dev, true);
	b43_phy_lcn_op_adjust_txpower(dev);
	b43_mac_enable(dev);
}

/**************************************************
 * R/W ops.
 **************************************************/

static void b43_phy_lcn_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				   u16 set)
{
	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
}

static u16 b43_phy_lcn_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	/* LCN-PHY needs 0x200 for read access */
	reg |= 0x200;

	b43_write16f(dev, B43_MMIO_RADIO24_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_RADIO24_DATA);
}

static void b43_phy_lcn_op_radio_write(struct b43_wldev *dev, u16 reg,
				       u16 value)
{
	b43_write16f(dev, B43_MMIO_RADIO24_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO24_DATA, value);
}

/**************************************************
 * PHY ops struct.
 **************************************************/

B43_CAL_TABLE(b43_phy_lcn_cal_entries) = {
	B43_CAL_ENTRY(LCN_CAL_IDLE_TSSI, IDLE_TSSI, "idle", COMPUTED, 3,
		      B43_CAL_S(tssi)),
	B43_CAL_ENTRY(LCN_CAL_TXIQLO, TXIQLO, "coef", COMPUTED, 3,
		      B43_CAL_SV(iqcc, 2), B43_CAL_X(didq, 4),
		      B43_CAL_XV(loft, 2, 4)),
	B43_CAL_ENTRY(LCN_CAL_RXIQ_GAIN, RXIQ, "gain", EVALUATED, 3,
		      B43_CAL_U(lna2), B43_CAL_U(tia), B43_CAL_U(biq1)),
	B43_CAL_ENTRY(LCN_CAL_RXIQ_COMP, RXIQ, "comp", COMPUTED, 3,
		      B43_CAL_U(ok), B43_CAL_SV(comp, 2)),
	B43_CAL_ENTRY(LCN_CAL_TXPWR, TXPWR, "target", COMPUTED, 3,
		      B43_CAL_U(target),
		      B43_CAL_SV(offset, B43_LCN_TX_PWR_RATES)),
};

const struct b43_phy_operations b43_phyops_lcn = {
	.allocate		= b43_phy_lcn_op_allocate,
	.free			= b43_phy_lcn_op_free,
	.prepare_structs	= b43_phy_lcn_op_prepare_structs,
	.init			= b43_phy_lcn_op_init,
	.phy_maskset		= b43_phy_lcn_op_maskset,
	.radio_read		= b43_phy_lcn_op_radio_read,
	.radio_write		= b43_phy_lcn_op_radio_write,
	.software_rfkill	= b43_phy_lcn_op_software_rfkill,
	.switch_analog		= b43_phy_lcn_op_switch_analog,
	.switch_channel		= b43_phy_lcn_op_switch_channel,
	.get_default_chan	= b43_phy_lcn_op_get_default_chan,
	.recalc_txpower		= b43_phy_lcn_op_recalc_txpower,
	.adjust_txpower		= b43_phy_lcn_op_adjust_txpower,
	.channel_calibrate	= b43_phy_lcn_op_channel_calibrate,
	B43_CAL_OPS(b43_phy_lcn_cal_entries)
};
