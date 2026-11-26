/*
 * Broadcom specific AMBA
 * PCI Host
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/slab.h>
#include <bcma-next/bcmn.h>
#include <linux/pci.h>
#include <linux/module.h>

static void bcmn_host_pci_switch_core(struct bcmn_device *core)
{
	int win2 = core->bus->host_is_pcie2 ?
		BCMN_PCIE2_BAR0_WIN2 : BCMN_PCI_BAR0_WIN2;

	pci_write_config_dword(core->bus->host_pci, BCMN_PCI_BAR0_WIN,
			       core->addr);
	pci_write_config_dword(core->bus->host_pci, win2, core->wrap);
	core->bus->mapped_core = core;
	bcmn_debug(core->bus, "Switched to core: 0x%X\n", core->id.id);
}

/* Provides access to the requested core. Returns base offset that has to be
 * used. It makes use of fixed windows when possible. */
static u16 bcmn_host_pci_provide_access_to_core(struct bcmn_device *core)
{
	switch (core->id.id) {
	case BCMN_CORE_CHIPCOMMON:
		return 3 * BCMN_CORE_SIZE;
	case BCMN_CORE_PCIE:
		return 2 * BCMN_CORE_SIZE;
	}

	if (core->bus->mapped_core != core)
		bcmn_host_pci_switch_core(core);
	return 0;
}

static u8 bcmn_host_pci_read8(struct bcmn_device *core, u16 offset)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	return ioread8(core->bus->mmio + offset);
}

static u16 bcmn_host_pci_read16(struct bcmn_device *core, u16 offset)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	return ioread16(core->bus->mmio + offset);
}

static u32 bcmn_host_pci_read32(struct bcmn_device *core, u16 offset)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	return ioread32(core->bus->mmio + offset);
}

static void bcmn_host_pci_write8(struct bcmn_device *core, u16 offset,
				 u8 value)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	iowrite8(value, core->bus->mmio + offset);
}

static void bcmn_host_pci_write16(struct bcmn_device *core, u16 offset,
				 u16 value)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	iowrite16(value, core->bus->mmio + offset);
}

static void bcmn_host_pci_write32(struct bcmn_device *core, u16 offset,
				 u32 value)
{
	offset += bcmn_host_pci_provide_access_to_core(core);
	iowrite32(value, core->bus->mmio + offset);
}

#ifdef CONFIG_BCMN_BLOCKIO
static void bcmn_host_pci_block_read(struct bcmn_device *core, void *buffer,
				     size_t count, u16 offset, u8 reg_width)
{
	void __iomem *addr = core->bus->mmio + offset;
	if (core->bus->mapped_core != core)
		bcmn_host_pci_switch_core(core);
	switch (reg_width) {
	case sizeof(u8):
		ioread8_rep(addr, buffer, count);
		break;
	case sizeof(u16):
		WARN_ON(count & 1);
		ioread16_rep(addr, buffer, count >> 1);
		break;
	case sizeof(u32):
		WARN_ON(count & 3);
		ioread32_rep(addr, buffer, count >> 2);
		break;
	default:
		WARN_ON(1);
	}
}

static void bcmn_host_pci_block_write(struct bcmn_device *core,
				      const void *buffer, size_t count,
				      u16 offset, u8 reg_width)
{
	void __iomem *addr = core->bus->mmio + offset;
	if (core->bus->mapped_core != core)
		bcmn_host_pci_switch_core(core);
	switch (reg_width) {
	case sizeof(u8):
		iowrite8_rep(addr, buffer, count);
		break;
	case sizeof(u16):
		WARN_ON(count & 1);
		iowrite16_rep(addr, buffer, count >> 1);
		break;
	case sizeof(u32):
		WARN_ON(count & 3);
		iowrite32_rep(addr, buffer, count >> 2);
		break;
	default:
		WARN_ON(1);
	}
}
#endif

static u32 bcmn_host_pci_aread32(struct bcmn_device *core, u16 offset)
{
	if (core->bus->mapped_core != core)
		bcmn_host_pci_switch_core(core);
	return ioread32(core->bus->mmio + (1 * BCMN_CORE_SIZE) + offset);
}

static void bcmn_host_pci_awrite32(struct bcmn_device *core, u16 offset,
				  u32 value)
{
	if (core->bus->mapped_core != core)
		bcmn_host_pci_switch_core(core);
	iowrite32(value, core->bus->mmio + (1 * BCMN_CORE_SIZE) + offset);
}

static const struct bcmn_host_ops bcmn_host_pci_ops = {
	.read8		= bcmn_host_pci_read8,
	.read16		= bcmn_host_pci_read16,
	.read32		= bcmn_host_pci_read32,
	.write8		= bcmn_host_pci_write8,
	.write16	= bcmn_host_pci_write16,
	.write32	= bcmn_host_pci_write32,
#ifdef CONFIG_BCMN_BLOCKIO
	.block_read	= bcmn_host_pci_block_read,
	.block_write	= bcmn_host_pci_block_write,
#endif
	.aread32	= bcmn_host_pci_aread32,
	.awrite32	= bcmn_host_pci_awrite32,
};

static int bcmn_host_pci_probe(struct pci_dev *dev,
			       const struct pci_device_id *id)
{
	struct bcmn_bus *bus;
	int err;
	u32 val;

	bus = devm_kzalloc(&dev->dev, sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return -ENOMEM;

	/* Basic PCI configuration */
	err = pcim_enable_device(dev);
	if (err)
		return err;

	pci_set_master(dev);

	/* Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_read_config_dword(dev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(dev, 0x40, val & 0xffff00ff);

	/* SSB needed additional powering up, do we have any AMBA PCI cards? */
	if (!pci_is_pcie(dev)) {
		bcmn_err(bus, "PCI card detected, they are not supported.\n");
		return -ENXIO;
	}

	bus->dev = &dev->dev;

	/* Map MMIO */
	bus->mmio = pcim_iomap_region(dev, 0, "bcma-next-pci-bridge");
	if (IS_ERR(bus->mmio))
		return PTR_ERR(bus->mmio);

	/* Host specific */
	bus->host_pci = dev;
	bus->hosttype = BCMN_HOSTTYPE_PCI;
	bus->ops = &bcmn_host_pci_ops;

	bus->boardinfo.vendor = bus->host_pci->subsystem_vendor;
	bus->boardinfo.type = bus->host_pci->subsystem_device;

	/* Initialize struct, detect chip */
	bcmn_init_bus(bus);

	/* Scan bus to find out generation of PCIe core */
	err = bcmn_bus_scan(bus);
	if (err)
		return err;

	if (bcmn_find_core(bus, BCMN_CORE_PCIE2))
		bus->host_is_pcie2 = true;

	/* Register */
	err = bcmn_bus_register(bus);
	if (err) {
		bcmn_unregister_cores(bus);
		return err;
	}

	pci_set_drvdata(dev, bus);

	return 0;
}

static void bcmn_host_pci_remove(struct pci_dev *dev)
{
	struct bcmn_bus *bus = pci_get_drvdata(dev);

	bcmn_bus_unregister(bus);
}

#ifdef CONFIG_PM_SLEEP
static int bcmn_host_pci_suspend(struct device *dev)
{
	struct bcmn_bus *bus = dev_get_drvdata(dev);

	bus->mapped_core = NULL;

	return bcmn_bus_suspend(bus);
}

static int bcmn_host_pci_resume(struct device *dev)
{
	struct bcmn_bus *bus = dev_get_drvdata(dev);

	return bcmn_bus_resume(bus);
}

static SIMPLE_DEV_PM_OPS(bcmn_pm_ops, bcmn_host_pci_suspend,
			 bcmn_host_pci_resume);
#define BCMN_PM_OPS	(&bcmn_pm_ops)

#else /* CONFIG_PM_SLEEP */

#define BCMN_PM_OPS     NULL

#endif /* CONFIG_PM_SLEEP */

static const struct pci_device_id bcmn_pci_bridge_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x0576) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4313) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 43224) },	/* 0xa8d8 */
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4331) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4353) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4357) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4358) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4359) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4360) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, 0x4365, PCI_VENDOR_ID_DELL, 0x0016) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, 0x4365, PCI_VENDOR_ID_DELL, 0x0018) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, 0x4365, PCI_VENDOR_ID_FOXCONN, 0xe092) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, 0x4365, PCI_VENDOR_ID_HP, 0x2230) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_BROADCOM, 0x4365, PCI_VENDOR_ID_HP, 0x804a) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43a0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43a9) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43aa) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43b1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x43b3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 0x4727) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 43227) },	/* 0xa8db, BCM43217 (sic!) */
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, 43228) },	/* 0xa8dc */
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, bcmn_pci_bridge_tbl);

static struct pci_driver bcmn_pci_bridge_driver = {
	.name = "bcma-next-pci-bridge",
	.id_table = bcmn_pci_bridge_tbl,
	.probe = bcmn_host_pci_probe,
	.remove = bcmn_host_pci_remove,
	.driver.pm = BCMN_PM_OPS,
};

int __init bcmn_host_pci_init(void)
{
	return pci_register_driver(&bcmn_pci_bridge_driver);
}

void bcmn_host_pci_exit(void)
{
	pci_unregister_driver(&bcmn_pci_bridge_driver);
}

/**************************************************
 * Runtime ops for drivers.
 **************************************************/

/* See also pcicore_up */
void bcmn_host_pci_up(struct bcmn_bus *bus)
{
	if (bus->hosttype != BCMN_HOSTTYPE_PCI)
		return;

	if (bus->host_is_pcie2)
		bcmn_core_pcie2_up(&bus->drv_pcie2);
	else
		bcmn_core_pci_up(&bus->drv_pci[0]);
}

/* See also pcicore_down */
void bcmn_host_pci_down(struct bcmn_bus *bus)
{
	if (bus->hosttype != BCMN_HOSTTYPE_PCI)
		return;

	if (!bus->host_is_pcie2)
		bcmn_core_pci_down(&bus->drv_pci[0]);
}

/* See also si_pci_setup */
int bcmn_host_pci_irq_ctl(struct bcmn_bus *bus, struct bcmn_device *core,
			  bool enable)
{
	struct pci_dev *pdev;
	u32 coremask, tmp;
	int err = 0;

	if (bus->hosttype != BCMN_HOSTTYPE_PCI) {
		/* This bcma device is not on a PCI host-bus. So the IRQs are
		 * not routed through the PCI core.
		 * So we must not enable routing through the PCI core. */
		goto out;
	}

	pdev = bus->host_pci;

	err = pci_read_config_dword(pdev, BCMN_PCI_IRQMASK, &tmp);
	if (err)
		goto out;

	coremask = BIT(core->core_index) << 8;
	if (enable)
		tmp |= coremask;
	else
		tmp &= ~coremask;

	err = pci_write_config_dword(pdev, BCMN_PCI_IRQMASK, tmp);

out:
	return err;
}
