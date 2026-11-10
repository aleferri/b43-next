/*
 * Sonics Silicon Backplane
 * Bus scanning
 *
 * Copyright (C) 2005-2007 Michael Buesch <m@bues.ch>
 * Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 * Copyright (C) 2006 Broadcom Corporation.
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssbn_private.h"

#include <ssb-next/ssbn.h>
#include <ssb-next/ssbn_regs.h>
#include <linux/pci.h>
#include <linux/io.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>


const char *ssbn_core_name(u16 coreid)
{
	switch (coreid) {
	case SSBN_DEV_CHIPCOMMON:
		return "ChipCommon";
	case SSBN_DEV_ILINE20:
		return "ILine 20";
	case SSBN_DEV_SDRAM:
		return "SDRAM";
	case SSBN_DEV_PCI:
		return "PCI";
	case SSBN_DEV_MIPS:
		return "MIPS";
	case SSBN_DEV_ETHERNET:
		return "Fast Ethernet";
	case SSBN_DEV_V90:
		return "V90";
	case SSBN_DEV_USB11_HOSTDEV:
		return "USB 1.1 Hostdev";
	case SSBN_DEV_ADSL:
		return "ADSL";
	case SSBN_DEV_ILINE100:
		return "ILine 100";
	case SSBN_DEV_IPSEC:
		return "IPSEC";
	case SSBN_DEV_PCMCIA:
		return "PCMCIA";
	case SSBN_DEV_INTERNAL_MEM:
		return "Internal Memory";
	case SSBN_DEV_MEMC_SDRAM:
		return "MEMC SDRAM";
	case SSBN_DEV_EXTIF:
		return "EXTIF";
	case SSBN_DEV_80211:
		return "IEEE 802.11";
	case SSBN_DEV_MIPS_3302:
		return "MIPS 3302";
	case SSBN_DEV_USB11_HOST:
		return "USB 1.1 Host";
	case SSBN_DEV_USB11_DEV:
		return "USB 1.1 Device";
	case SSBN_DEV_USB20_HOST:
		return "USB 2.0 Host";
	case SSBN_DEV_USB20_DEV:
		return "USB 2.0 Device";
	case SSBN_DEV_SDIO_HOST:
		return "SDIO Host";
	case SSBN_DEV_ROBOSWITCH:
		return "Roboswitch";
	case SSBN_DEV_PARA_ATA:
		return "PATA";
	case SSBN_DEV_SATA_XORDMA:
		return "SATA XOR-DMA";
	case SSBN_DEV_ETHERNET_GBIT:
		return "GBit Ethernet";
	case SSBN_DEV_PCIE:
		return "PCI-E";
	case SSBN_DEV_MIMO_PHY:
		return "MIMO PHY";
	case SSBN_DEV_SRAM_CTRLR:
		return "SRAM Controller";
	case SSBN_DEV_MINI_MACPHY:
		return "Mini MACPHY";
	case SSBN_DEV_ARM_1176:
		return "ARM 1176";
	case SSBN_DEV_ARM_7TDMI:
		return "ARM 7TDMI";
	case SSBN_DEV_ARM_CM3:
		return "ARM Cortex M3";
	}
	return "UNKNOWN";
}

static u16 pcidev_to_chipid(struct pci_dev *pci_dev)
{
	u16 chipid_fallback = 0;

	switch (pci_dev->device) {
	case 0x4301:
		chipid_fallback = 0x4301;
		break;
	case 0x4305 ... 0x4307:
		chipid_fallback = 0x4307;
		break;
	case 0x4403:
		chipid_fallback = 0x4402;
		break;
	case 0x4610 ... 0x4615:
		chipid_fallback = 0x4610;
		break;
	case 0x4710 ... 0x4715:
		chipid_fallback = 0x4710;
		break;
	case 0x4320 ... 0x4325:
		chipid_fallback = 0x4309;
		break;
	case PCI_DEVICE_ID_BCM4401:
	case PCI_DEVICE_ID_BCM4401B0:
	case PCI_DEVICE_ID_BCM4401B1:
		chipid_fallback = 0x4401;
		break;
	default:
		dev_err(&pci_dev->dev, "PCI-ID not in fallback list\n");
	}

	return chipid_fallback;
}

static u8 chipid_to_nrcores(u16 chipid)
{
	switch (chipid) {
	case 0x5365:
		return 7;
	case 0x4306:
		return 6;
	case 0x4310:
		return 8;
	case 0x4307:
	case 0x4301:
		return 5;
	case 0x4401:
	case 0x4402:
		return 3;
	case 0x4710:
	case 0x4610:
	case 0x4704:
		return 9;
	default:
		pr_err("CHIPID not in nrcores fallback list\n");
	}

	return 1;
}

static u32 scan_read32(struct ssbn_bus *bus, u8 current_coreidx,
		       u16 offset)
{
	u32 lo, hi;

	switch (bus->bustype) {
	case SSBN_BUSTYPE_SSB:
		offset += current_coreidx * SSBN_CORE_SIZE;
		break;
	case SSBN_BUSTYPE_PCI:
		break;
	case SSBN_BUSTYPE_PCMCIA:
		if (offset >= 0x800) {
			ssbn_pcmcia_switch_segment(bus, 1);
			offset -= 0x800;
		} else
			ssbn_pcmcia_switch_segment(bus, 0);
		lo = readw(bus->mmio + offset);
		hi = readw(bus->mmio + offset + 2);
		return lo | (hi << 16);
	case SSBN_BUSTYPE_SDIO:
		offset += current_coreidx * SSBN_CORE_SIZE;
		return ssbn_sdio_scan_read32(bus, offset);
	}
	return readl(bus->mmio + offset);
}

static int scan_switchcore(struct ssbn_bus *bus, u8 coreidx)
{
	switch (bus->bustype) {
	case SSBN_BUSTYPE_SSB:
		break;
	case SSBN_BUSTYPE_PCI:
		return ssbn_pci_switch_coreidx(bus, coreidx);
	case SSBN_BUSTYPE_PCMCIA:
		return ssbn_pcmcia_switch_coreidx(bus, coreidx);
	case SSBN_BUSTYPE_SDIO:
		return ssbn_sdio_scan_switch_coreidx(bus, coreidx);
	}
	return 0;
}

void ssbn_iounmap(struct ssbn_bus *bus)
{
	switch (bus->bustype) {
	case SSBN_BUSTYPE_SSB:
	case SSBN_BUSTYPE_PCMCIA:
		iounmap(bus->mmio);
		break;
	case SSBN_BUSTYPE_PCI:
#ifdef CONFIG_SSBN_PCIHOST
		pci_iounmap(bus->host_pci, bus->mmio);
#else
		WARN_ON(1); /* Can't reach this code. */
#endif
		break;
	case SSBN_BUSTYPE_SDIO:
		break;
	}
	bus->mmio = NULL;
	bus->mapped_device = NULL;
}

static void __iomem *ssbn_ioremap(struct ssbn_bus *bus,
				 unsigned long baseaddr)
{
	void __iomem *mmio = NULL;

	switch (bus->bustype) {
	case SSBN_BUSTYPE_SSB:
		/* Only map the first core for now. */
		fallthrough;
	case SSBN_BUSTYPE_PCMCIA:
		mmio = ioremap(baseaddr, SSBN_CORE_SIZE);
		break;
	case SSBN_BUSTYPE_PCI:
#ifdef CONFIG_SSBN_PCIHOST
		mmio = pci_iomap(bus->host_pci, 0, ~0UL);
#else
		WARN_ON(1); /* Can't reach this code. */
#endif
		break;
	case SSBN_BUSTYPE_SDIO:
		/* Nothing to ioremap in the SDIO case, just fake it */
		mmio = (void __iomem *)baseaddr;
		break;
	}

	return mmio;
}

static int we_support_multiple_80211_cores(struct ssbn_bus *bus)
{
	/* More than one 802.11 core is only supported by special chips.
	 * There are chips with two 802.11 cores, but with dangling
	 * pins on the second core. Be careful and reject them here.
	 */

#ifdef CONFIG_SSBN_PCIHOST
	if (bus->bustype == SSBN_BUSTYPE_PCI) {
		if (bus->host_pci->vendor == PCI_VENDOR_ID_BROADCOM &&
		    ((bus->host_pci->device == 0x4313) ||
		     (bus->host_pci->device == 0x431A) ||
		     (bus->host_pci->device == 0x4321) ||
		     (bus->host_pci->device == 0x4324)))
			return 1;
	}
#endif /* CONFIG_SSBN_PCIHOST */
	return 0;
}

int ssbn_bus_scan(struct ssbn_bus *bus,
		 unsigned long baseaddr)
{
	int err = -ENOMEM;
	void __iomem *mmio;
	u32 idhi, cc, rev, tmp;
	int dev_i, i;
	struct ssbn_device *dev;
	int nr_80211_cores = 0;

	mmio = ssbn_ioremap(bus, baseaddr);
	if (!mmio)
		goto out;
	bus->mmio = mmio;

	err = scan_switchcore(bus, 0); /* Switch to first core */
	if (err)
		goto err_unmap;

	idhi = scan_read32(bus, 0, SSBN_IDHIGH);
	cc = (idhi & SSBN_IDHIGH_CC) >> SSBN_IDHIGH_CC_SHIFT;
	rev = (idhi & SSBN_IDHIGH_RCLO);
	rev |= (idhi & SSBN_IDHIGH_RCHI) >> SSBN_IDHIGH_RCHI_SHIFT;

	bus->nr_devices = 0;
	if (cc == SSBN_DEV_CHIPCOMMON) {
		tmp = scan_read32(bus, 0, SSBN_CHIPCO_CHIPID);

		bus->chip_id = (tmp & SSBN_CHIPCO_IDMASK);
		bus->chip_rev = (tmp & SSBN_CHIPCO_REVMASK) >>
				SSBN_CHIPCO_REVSHIFT;
		bus->chip_package = (tmp & SSBN_CHIPCO_PACKMASK) >>
				    SSBN_CHIPCO_PACKSHIFT;
		if (rev >= 4) {
			bus->nr_devices = (tmp & SSBN_CHIPCO_NRCORESMASK) >>
					  SSBN_CHIPCO_NRCORESSHIFT;
		}
		tmp = scan_read32(bus, 0, SSBN_CHIPCO_CAP);
		bus->chipco.capabilities = tmp;
	} else {
		if (bus->bustype == SSBN_BUSTYPE_PCI) {
			bus->chip_id = pcidev_to_chipid(bus->host_pci);
			bus->chip_rev = bus->host_pci->revision;
			bus->chip_package = 0;
		} else {
			bus->chip_id = 0x4710;
			bus->chip_rev = 0;
			bus->chip_package = 0;
		}
	}
	pr_info("Found chip with id 0x%04X, rev 0x%02X and package 0x%02X\n",
		bus->chip_id, bus->chip_rev, bus->chip_package);
	if (!bus->nr_devices)
		bus->nr_devices = chipid_to_nrcores(bus->chip_id);
	if (bus->nr_devices > ARRAY_SIZE(bus->devices)) {
		pr_err("More than %d ssb cores found (%d)\n",
		       SSBN_MAX_NR_CORES, bus->nr_devices);
		err = -EINVAL;
		goto err_unmap;
	}
	if (bus->bustype == SSBN_BUSTYPE_SSB) {
		/* Now that we know the number of cores,
		 * remap the whole IO space for all cores.
		 */
		err = -ENOMEM;
		iounmap(mmio);
		mmio = ioremap(baseaddr, SSBN_CORE_SIZE * bus->nr_devices);
		if (!mmio)
			goto out;
		bus->mmio = mmio;
	}

	/* Fetch basic information about each core/device */
	for (i = 0, dev_i = 0; i < bus->nr_devices; i++) {
		err = scan_switchcore(bus, i);
		if (err)
			goto err_unmap;
		dev = &(bus->devices[dev_i]);

		idhi = scan_read32(bus, i, SSBN_IDHIGH);
		dev->id.coreid = (idhi & SSBN_IDHIGH_CC) >> SSBN_IDHIGH_CC_SHIFT;
		dev->id.revision = (idhi & SSBN_IDHIGH_RCLO);
		dev->id.revision |= (idhi & SSBN_IDHIGH_RCHI) >> SSBN_IDHIGH_RCHI_SHIFT;
		dev->id.vendor = (idhi & SSBN_IDHIGH_VC) >> SSBN_IDHIGH_VC_SHIFT;
		dev->core_index = i;
		dev->bus = bus;
		dev->ops = bus->ops;

		pr_debug("Core %d found: %s (cc 0x%03X, rev 0x%02X, vendor 0x%04X)\n",
			 i, ssbn_core_name(dev->id.coreid),
			 dev->id.coreid, dev->id.revision, dev->id.vendor);

		switch (dev->id.coreid) {
		case SSBN_DEV_80211:
			nr_80211_cores++;
			if (nr_80211_cores > 1) {
				if (!we_support_multiple_80211_cores(bus)) {
					pr_debug("Ignoring additional 802.11 core\n");
					continue;
				}
			}
			break;
		case SSBN_DEV_EXTIF:
#ifdef CONFIG_SSBN_DRIVER_EXTIF
			if (bus->extif.dev) {
				pr_warn("WARNING: Multiple EXTIFs found\n");
				break;
			}
			bus->extif.dev = dev;
#endif /* CONFIG_SSBN_DRIVER_EXTIF */
			break;
		case SSBN_DEV_CHIPCOMMON:
			if (bus->chipco.dev) {
				pr_warn("WARNING: Multiple ChipCommon found\n");
				break;
			}
			bus->chipco.dev = dev;
			break;
		case SSBN_DEV_MIPS:
		case SSBN_DEV_MIPS_3302:
#ifdef CONFIG_SSBN_DRIVER_MIPS
			if (bus->mipscore.dev) {
				pr_warn("WARNING: Multiple MIPS cores found\n");
				break;
			}
			bus->mipscore.dev = dev;
#endif /* CONFIG_SSBN_DRIVER_MIPS */
			break;
		case SSBN_DEV_PCI:
		case SSBN_DEV_PCIE:
#ifdef CONFIG_SSBN_DRIVER_PCICORE
			if (bus->bustype == SSBN_BUSTYPE_PCI) {
				/* Ignore PCI cores on PCI-E cards.
				 * Ignore PCI-E cores on PCI cards.
				 */
				if (dev->id.coreid == SSBN_DEV_PCI) {
					if (pci_is_pcie(bus->host_pci))
						continue;
				} else {
					if (!pci_is_pcie(bus->host_pci))
						continue;
				}
			}
			if (bus->pcicore.dev) {
				pr_warn("WARNING: Multiple PCI(E) cores found\n");
				break;
			}
			bus->pcicore.dev = dev;
#endif /* CONFIG_SSBN_DRIVER_PCICORE */
			break;
		case SSBN_DEV_ETHERNET:
			if (bus->bustype == SSBN_BUSTYPE_PCI) {
				if (bus->host_pci->vendor == PCI_VENDOR_ID_BROADCOM &&
				    (bus->host_pci->device & 0xFF00) == 0x4300) {
					/* This is a dangling ethernet core on a
					 * wireless device. Ignore it.
					 */
					continue;
				}
			}
			break;
		default:
			break;
		}

		dev_i++;
	}
	bus->nr_devices = dev_i;

	err = 0;
out:
	return err;
err_unmap:
	ssbn_iounmap(bus);
	goto out;
}
