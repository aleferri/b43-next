// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom B43 wireless driver
 *
 * The LCN40-PHY (BCM43142, radio 2065), from the stock driver's bus
 * traffic.
 */

#include "b43.h"
#include "phy_lcn40.h"

static int b43_phy_lcn40_op_allocate(struct b43_wldev *dev)
{
	return 0;
}

static void b43_phy_lcn40_op_free(struct b43_wldev *dev)
{
}

static void b43_phy_lcn40_op_prepare_structs(struct b43_wldev *dev)
{
}

static int b43_phy_lcn40_op_init(struct b43_wldev *dev)
{
	b43err(dev->wl, "LCN40-PHY: no init yet\n");
	return -EOPNOTSUPP;
}

static u16 b43_phy_lcn40_op_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	return b43_read16(dev, B43_MMIO_PHY_DATA);
}

/* Address and data in one access, as the stock driver writes them */
static void b43_phy_lcn40_op_write(struct b43_wldev *dev, u16 reg, u16 value)
{
	b43_write32(dev, B43_MMIO_PHY_CONTROL, (u32)value << 16 | reg);
}

static void b43_phy_lcn40_op_maskset(struct b43_wldev *dev, u16 reg, u16 mask,
				     u16 set)
{
	b43_write16f(dev, B43_MMIO_PHY_CONTROL, reg);
	b43_write16(dev, B43_MMIO_PHY_DATA,
		    (b43_read16(dev, B43_MMIO_PHY_DATA) & mask) | set);
}

static u16 b43_phy_lcn40_op_radio_read(struct b43_wldev *dev, u16 reg)
{
	b43_write16f(dev, B43_MMIO_RADIO24_CONTROL, reg | 0x200);
	return b43_read16(dev, B43_MMIO_RADIO24_DATA);
}

static void b43_phy_lcn40_op_radio_write(struct b43_wldev *dev, u16 reg,
					 u16 value)
{
	b43_write16f(dev, B43_MMIO_RADIO24_CONTROL, reg);
	b43_write16(dev, B43_MMIO_RADIO24_DATA, value);
}

static void b43_phy_lcn40_op_software_rfkill(struct b43_wldev *dev,
					     bool blocked)
{
}

/* The stock driver touches no PHY register around its core resets */
static void b43_phy_lcn40_op_switch_analog(struct b43_wldev *dev, bool on)
{
}

static int b43_phy_lcn40_op_switch_channel(struct b43_wldev *dev,
					   unsigned int new_channel)
{
	return -EOPNOTSUPP;
}

static enum b43_txpwr_result
b43_phy_lcn40_op_recalc_txpower(struct b43_wldev *dev, bool ignore_tssi)
{
	return B43_TXPWR_RES_DONE;
}

const struct b43_phy_operations b43_phyops_lcn40 = {
	.allocate		= b43_phy_lcn40_op_allocate,
	.free			= b43_phy_lcn40_op_free,
	.prepare_structs	= b43_phy_lcn40_op_prepare_structs,
	.init			= b43_phy_lcn40_op_init,
	.phy_read		= b43_phy_lcn40_op_read,
	.phy_write		= b43_phy_lcn40_op_write,
	.phy_maskset		= b43_phy_lcn40_op_maskset,
	.radio_read		= b43_phy_lcn40_op_radio_read,
	.radio_write		= b43_phy_lcn40_op_radio_write,
	.software_rfkill	= b43_phy_lcn40_op_software_rfkill,
	.switch_analog		= b43_phy_lcn40_op_switch_analog,
	.switch_channel		= b43_phy_lcn40_op_switch_channel,
	.recalc_txpower		= b43_phy_lcn40_op_recalc_txpower,
};
