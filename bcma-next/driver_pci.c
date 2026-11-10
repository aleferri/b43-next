/*
 * Broadcom specific AMBA
 * PCI Core
 *
 * Copyright 2005, 2011, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/export.h>
#include <bcma-next/bcmn.h>

/**************************************************
 * R/W ops.
 **************************************************/

u32 bcmn_pcie_read(struct bcmn_drv_pci *pc, u32 address)
{
	pcicore_write32(pc, BCMN_CORE_PCI_PCIEIND_ADDR, address);
	pcicore_read32(pc, BCMN_CORE_PCI_PCIEIND_ADDR);
	return pcicore_read32(pc, BCMN_CORE_PCI_PCIEIND_DATA);
}

static void bcmn_pcie_write(struct bcmn_drv_pci *pc, u32 address, u32 data)
{
	pcicore_write32(pc, BCMN_CORE_PCI_PCIEIND_ADDR, address);
	pcicore_read32(pc, BCMN_CORE_PCI_PCIEIND_ADDR);
	pcicore_write32(pc, BCMN_CORE_PCI_PCIEIND_DATA, data);
}

static void bcmn_pcie_mdio_set_phy(struct bcmn_drv_pci *pc, u16 phy)
{
	u32 v;
	int i;

	v = BCMN_CORE_PCI_MDIODATA_START;
	v |= BCMN_CORE_PCI_MDIODATA_WRITE;
	v |= (BCMN_CORE_PCI_MDIODATA_DEV_ADDR <<
	      BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF);
	v |= (BCMN_CORE_PCI_MDIODATA_BLK_ADDR <<
	      BCMN_CORE_PCI_MDIODATA_REGADDR_SHF);
	v |= BCMN_CORE_PCI_MDIODATA_TA;
	v |= (phy << 4);
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_DATA, v);

	udelay(10);
	for (i = 0; i < 200; i++) {
		v = pcicore_read32(pc, BCMN_CORE_PCI_MDIO_CONTROL);
		if (v & BCMN_CORE_PCI_MDIOCTL_ACCESS_DONE)
			break;
		usleep_range(1000, 2000);
	}
}

static u16 bcmn_pcie_mdio_read(struct bcmn_drv_pci *pc, u16 device, u8 address)
{
	int max_retries = 10;
	u16 ret = 0;
	u32 v;
	int i;

	/* enable mdio access to SERDES */
	v = BCMN_CORE_PCI_MDIOCTL_PREAM_EN;
	v |= BCMN_CORE_PCI_MDIOCTL_DIVISOR_VAL;
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_CONTROL, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcmn_pcie_mdio_set_phy(pc, device);
		v = (BCMN_CORE_PCI_MDIODATA_DEV_ADDR <<
		     BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF);
		v |= (address << BCMN_CORE_PCI_MDIODATA_REGADDR_SHF);
	} else {
		v = (device << BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF_OLD);
		v |= (address << BCMN_CORE_PCI_MDIODATA_REGADDR_SHF_OLD);
	}

	v |= BCMN_CORE_PCI_MDIODATA_START;
	v |= BCMN_CORE_PCI_MDIODATA_READ;
	v |= BCMN_CORE_PCI_MDIODATA_TA;

	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_DATA, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, BCMN_CORE_PCI_MDIO_CONTROL);
		if (v & BCMN_CORE_PCI_MDIOCTL_ACCESS_DONE) {
			udelay(10);
			ret = pcicore_read32(pc, BCMN_CORE_PCI_MDIO_DATA);
			break;
		}
		usleep_range(1000, 2000);
	}
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_CONTROL, 0);
	return ret;
}

static void bcmn_pcie_mdio_write(struct bcmn_drv_pci *pc, u16 device,
				u8 address, u16 data)
{
	int max_retries = 10;
	u32 v;
	int i;

	/* enable mdio access to SERDES */
	v = BCMN_CORE_PCI_MDIOCTL_PREAM_EN;
	v |= BCMN_CORE_PCI_MDIOCTL_DIVISOR_VAL;
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_CONTROL, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcmn_pcie_mdio_set_phy(pc, device);
		v = (BCMN_CORE_PCI_MDIODATA_DEV_ADDR <<
		     BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF);
		v |= (address << BCMN_CORE_PCI_MDIODATA_REGADDR_SHF);
	} else {
		v = (device << BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF_OLD);
		v |= (address << BCMN_CORE_PCI_MDIODATA_REGADDR_SHF_OLD);
	}

	v |= BCMN_CORE_PCI_MDIODATA_START;
	v |= BCMN_CORE_PCI_MDIODATA_WRITE;
	v |= BCMN_CORE_PCI_MDIODATA_TA;
	v |= data;
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_DATA, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, BCMN_CORE_PCI_MDIO_CONTROL);
		if (v & BCMN_CORE_PCI_MDIOCTL_ACCESS_DONE)
			break;
		usleep_range(1000, 2000);
	}
	pcicore_write32(pc, BCMN_CORE_PCI_MDIO_CONTROL, 0);
}

static u16 bcmn_pcie_mdio_writeread(struct bcmn_drv_pci *pc, u16 device,
				    u8 address, u16 data)
{
	bcmn_pcie_mdio_write(pc, device, address, data);
	return bcmn_pcie_mdio_read(pc, device, address);
}

/**************************************************
 * Early init.
 **************************************************/

static void bcmn_core_pci_fixcfg(struct bcmn_drv_pci *pc)
{
	struct bcmn_device *core = pc->core;
	u16 val16, core_index;
	uint regoff;

	regoff = BCMN_CORE_PCI_SPROM(BCMN_CORE_PCI_SPROM_PI_OFFSET);
	core_index = (u16)core->core_index;

	val16 = pcicore_read16(pc, regoff);
	if (((val16 & BCMN_CORE_PCI_SPROM_PI_MASK) >> BCMN_CORE_PCI_SPROM_PI_SHIFT)
	     != core_index) {
		val16 = (core_index << BCMN_CORE_PCI_SPROM_PI_SHIFT) |
			(val16 & ~BCMN_CORE_PCI_SPROM_PI_MASK);
		pcicore_write16(pc, regoff, val16);
	}
}

/*
 * Apply some early fixes required before accessing SPROM.
 * See also si_pci_fixcfg.
 */
void bcmn_core_pci_early_init(struct bcmn_drv_pci *pc)
{
	if (pc->early_setup_done)
		return;

	pc->hostmode = bcmn_core_pci_is_in_hostmode(pc);
	if (pc->hostmode)
		goto out;

	bcmn_core_pci_fixcfg(pc);

out:
	pc->early_setup_done = true;
}

/**************************************************
 * Workarounds.
 **************************************************/

static u8 bcmn_pcicore_polarity_workaround(struct bcmn_drv_pci *pc)
{
	u32 tmp;

	tmp = bcmn_pcie_read(pc, BCMN_CORE_PCI_PLP_STATUSREG);
	if (tmp & BCMN_CORE_PCI_PLP_POLARITYINV_STAT)
		return BCMN_CORE_PCI_SERDES_RX_CTRL_FORCE |
		       BCMN_CORE_PCI_SERDES_RX_CTRL_POLARITY;
	else
		return BCMN_CORE_PCI_SERDES_RX_CTRL_FORCE;
}

static void bcmn_pcicore_serdes_workaround(struct bcmn_drv_pci *pc)
{
	u16 tmp;

	bcmn_pcie_mdio_write(pc, BCMN_CORE_PCI_MDIODATA_DEV_RX,
	                     BCMN_CORE_PCI_SERDES_RX_CTRL,
			     bcmn_pcicore_polarity_workaround(pc));
	tmp = bcmn_pcie_mdio_read(pc, BCMN_CORE_PCI_MDIODATA_DEV_PLL,
	                          BCMN_CORE_PCI_SERDES_PLL_CTRL);
	if (tmp & BCMN_CORE_PCI_PLL_CTRL_FREQDET_EN)
		bcmn_pcie_mdio_write(pc, BCMN_CORE_PCI_MDIODATA_DEV_PLL,
		                     BCMN_CORE_PCI_SERDES_PLL_CTRL,
		                     tmp & ~BCMN_CORE_PCI_PLL_CTRL_FREQDET_EN);
}

/* Fix MISC config to allow coming out of L2/L3-Ready state w/o PRST */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void bcmn_core_pci_config_fixup(struct bcmn_drv_pci *pc)
{
	u16 val16;
	uint regoff;

	regoff = BCMN_CORE_PCI_SPROM(BCMN_CORE_PCI_SPROM_MISC_CONFIG);

	val16 = pcicore_read16(pc, regoff);

	if (!(val16 & BCMN_CORE_PCI_SPROM_L23READY_EXIT_NOPERST)) {
		val16 |= BCMN_CORE_PCI_SPROM_L23READY_EXIT_NOPERST;
		pcicore_write16(pc, regoff, val16);
	}
}

/**************************************************
 * Init.
 **************************************************/

static void bcmn_core_pci_clientmode_init(struct bcmn_drv_pci *pc)
{
	bcmn_pcicore_serdes_workaround(pc);
	bcmn_core_pci_config_fixup(pc);
}

void bcmn_core_pci_init(struct bcmn_drv_pci *pc)
{
	if (pc->setup_done)
		return;

	bcmn_core_pci_early_init(pc);

	if (pc->hostmode)
		bcmn_core_pci_hostmode_init(pc);
	else
		bcmn_core_pci_clientmode_init(pc);
}

void bcmn_core_pci_power_save(struct bcmn_bus *bus, bool up)
{
	struct bcmn_drv_pci *pc;
	u16 data;

	if (bus->hosttype != BCMN_HOSTTYPE_PCI)
		return;

	pc = &bus->drv_pci[0];

	if (pc->core->id.rev >= 15 && pc->core->id.rev <= 20) {
		data = up ? 0x74 : 0x7C;
		bcmn_pcie_mdio_writeread(pc, BCMN_CORE_PCI_MDIO_BLK1,
					 BCMN_CORE_PCI_MDIO_BLK1_MGMT1, 0x7F64);
		bcmn_pcie_mdio_writeread(pc, BCMN_CORE_PCI_MDIO_BLK1,
					 BCMN_CORE_PCI_MDIO_BLK1_MGMT3, data);
	} else if (pc->core->id.rev >= 21 && pc->core->id.rev <= 22) {
		data = up ? 0x75 : 0x7D;
		bcmn_pcie_mdio_writeread(pc, BCMN_CORE_PCI_MDIO_BLK1,
					 BCMN_CORE_PCI_MDIO_BLK1_MGMT1, 0x7E65);
		bcmn_pcie_mdio_writeread(pc, BCMN_CORE_PCI_MDIO_BLK1,
					 BCMN_CORE_PCI_MDIO_BLK1_MGMT3, data);
	}
}
EXPORT_SYMBOL_GPL(bcmn_core_pci_power_save);

static void bcmn_core_pci_extend_L1timer(struct bcmn_drv_pci *pc, bool extend)
{
	u32 w;

	w = bcmn_pcie_read(pc, BCMN_CORE_PCI_DLLP_PMTHRESHREG);
	if (extend)
		w |= BCMN_CORE_PCI_ASPMTIMER_EXTEND;
	else
		w &= ~BCMN_CORE_PCI_ASPMTIMER_EXTEND;
	bcmn_pcie_write(pc, BCMN_CORE_PCI_DLLP_PMTHRESHREG, w);
	bcmn_pcie_read(pc, BCMN_CORE_PCI_DLLP_PMTHRESHREG);
}

void bcmn_core_pci_up(struct bcmn_drv_pci *pc)
{
	bcmn_core_pci_extend_L1timer(pc, true);
}

void bcmn_core_pci_down(struct bcmn_drv_pci *pc)
{
	bcmn_core_pci_extend_L1timer(pc, false);
}
