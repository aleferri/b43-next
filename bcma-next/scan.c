/*
 * Broadcom specific AMBA
 * Bus scanning
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "scan.h"
#include "bcmn_private.h"

#include <bcma-next/bcmn.h>
#include <bcma-next/bcmn_regs.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

struct bcmn_device_id_name {
	u16 id;
	const char *name;
};

static const struct bcmn_device_id_name bcmn_arm_device_names[] = {
	{ BCMN_CORE_4706_MAC_GBIT_COMMON, "BCM4706 GBit MAC Common" },
	{ BCMN_CORE_ARM_1176, "ARM 1176" },
	{ BCMN_CORE_ARM_7TDMI, "ARM 7TDMI" },
	{ BCMN_CORE_ARM_CM3, "ARM CM3" },
};

static const struct bcmn_device_id_name bcmn_bcm_device_names[] = {
	{ BCMN_CORE_OOB_ROUTER, "OOB Router" },
	{ BCMN_CORE_4706_CHIPCOMMON, "BCM4706 ChipCommon" },
	{ BCMN_CORE_4706_SOC_RAM, "BCM4706 SOC RAM" },
	{ BCMN_CORE_4706_MAC_GBIT, "BCM4706 GBit MAC" },
	{ BCMN_CORE_NS_PCIEG2, "PCIe Gen 2" },
	{ BCMN_CORE_NS_DMA, "DMA" },
	{ BCMN_CORE_NS_SDIO3, "SDIO3" },
	{ BCMN_CORE_NS_USB20, "USB 2.0" },
	{ BCMN_CORE_NS_USB30, "USB 3.0" },
	{ BCMN_CORE_NS_A9JTAG, "ARM Cortex A9 JTAG" },
	{ BCMN_CORE_NS_DDR23, "Denali DDR2/DDR3 memory controller" },
	{ BCMN_CORE_NS_ROM, "ROM" },
	{ BCMN_CORE_NS_NAND, "NAND flash controller" },
	{ BCMN_CORE_NS_QSPI, "SPI flash controller" },
	{ BCMN_CORE_NS_CHIPCOMMON_B, "Chipcommon B" },
	{ BCMN_CORE_ARMCA9, "ARM Cortex A9 core (ihost)" },
	{ BCMN_CORE_AMEMC, "AMEMC (DDR)" },
	{ BCMN_CORE_ALTA, "ALTA (I2S)" },
	{ BCMN_CORE_INVALID, "Invalid" },
	{ BCMN_CORE_CHIPCOMMON, "ChipCommon" },
	{ BCMN_CORE_ILINE20, "ILine 20" },
	{ BCMN_CORE_SRAM, "SRAM" },
	{ BCMN_CORE_SDRAM, "SDRAM" },
	{ BCMN_CORE_PCI, "PCI" },
	{ BCMN_CORE_ETHERNET, "Fast Ethernet" },
	{ BCMN_CORE_V90, "V90" },
	{ BCMN_CORE_USB11_HOSTDEV, "USB 1.1 Hostdev" },
	{ BCMN_CORE_ADSL, "ADSL" },
	{ BCMN_CORE_ILINE100, "ILine 100" },
	{ BCMN_CORE_IPSEC, "IPSEC" },
	{ BCMN_CORE_UTOPIA, "UTOPIA" },
	{ BCMN_CORE_PCMCIA, "PCMCIA" },
	{ BCMN_CORE_INTERNAL_MEM, "Internal Memory" },
	{ BCMN_CORE_MEMC_SDRAM, "MEMC SDRAM" },
	{ BCMN_CORE_OFDM, "OFDM" },
	{ BCMN_CORE_EXTIF, "EXTIF" },
	{ BCMN_CORE_80211, "IEEE 802.11" },
	{ BCMN_CORE_PHY_A, "PHY A" },
	{ BCMN_CORE_PHY_B, "PHY B" },
	{ BCMN_CORE_PHY_G, "PHY G" },
	{ BCMN_CORE_USB11_HOST, "USB 1.1 Host" },
	{ BCMN_CORE_USB11_DEV, "USB 1.1 Device" },
	{ BCMN_CORE_USB20_HOST, "USB 2.0 Host" },
	{ BCMN_CORE_USB20_DEV, "USB 2.0 Device" },
	{ BCMN_CORE_SDIO_HOST, "SDIO Host" },
	{ BCMN_CORE_ROBOSWITCH, "Roboswitch" },
	{ BCMN_CORE_PARA_ATA, "PATA" },
	{ BCMN_CORE_SATA_XORDMA, "SATA XOR-DMA" },
	{ BCMN_CORE_ETHERNET_GBIT, "GBit Ethernet" },
	{ BCMN_CORE_PCIE, "PCIe" },
	{ BCMN_CORE_PHY_N, "PHY N" },
	{ BCMN_CORE_SRAM_CTL, "SRAM Controller" },
	{ BCMN_CORE_MINI_MACPHY, "Mini MACPHY" },
	{ BCMN_CORE_PHY_LP, "PHY LP" },
	{ BCMN_CORE_PMU, "PMU" },
	{ BCMN_CORE_PHY_SSN, "PHY SSN" },
	{ BCMN_CORE_SDIO_DEV, "SDIO Device" },
	{ BCMN_CORE_PHY_HT, "PHY HT" },
	{ BCMN_CORE_MAC_GBIT, "GBit MAC" },
	{ BCMN_CORE_DDR12_MEM_CTL, "DDR1/DDR2 Memory Controller" },
	{ BCMN_CORE_PCIE_RC, "PCIe Root Complex" },
	{ BCMN_CORE_OCP_OCP_BRIDGE, "OCP to OCP Bridge" },
	{ BCMN_CORE_SHARED_COMMON, "Common Shared" },
	{ BCMN_CORE_OCP_AHB_BRIDGE, "OCP to AHB Bridge" },
	{ BCMN_CORE_SPI_HOST, "SPI Host" },
	{ BCMN_CORE_I2S, "I2S" },
	{ BCMN_CORE_SDR_DDR1_MEM_CTL, "SDR/DDR1 Memory Controller" },
	{ BCMN_CORE_SHIM, "SHIM" },
	{ BCMN_CORE_PCIE2, "PCIe Gen2" },
	{ BCMN_CORE_ARM_CR4, "ARM CR4" },
	{ BCMN_CORE_GCI, "GCI" },
	{ BCMN_CORE_CMEM, "CNDS DDR2/3 memory controller" },
	{ BCMN_CORE_ARM_CA7, "ARM CA7" },
	{ BCMN_CORE_DEFAULT, "Default" },
};

static const struct bcmn_device_id_name bcmn_mips_device_names[] = {
	{ BCMN_CORE_MIPS, "MIPS" },
	{ BCMN_CORE_MIPS_3302, "MIPS 3302" },
	{ BCMN_CORE_MIPS_74K, "MIPS 74K" },
};

static const char *bcmn_device_name(const struct bcmn_device_id *id)
{
	const struct bcmn_device_id_name *names;
	int size, i;

	/* search manufacturer specific names */
	switch (id->manuf) {
	case BCMN_MANUF_ARM:
		names = bcmn_arm_device_names;
		size = ARRAY_SIZE(bcmn_arm_device_names);
		break;
	case BCMN_MANUF_BCM:
		names = bcmn_bcm_device_names;
		size = ARRAY_SIZE(bcmn_bcm_device_names);
		break;
	case BCMN_MANUF_MIPS:
		names = bcmn_mips_device_names;
		size = ARRAY_SIZE(bcmn_mips_device_names);
		break;
	default:
		return "UNKNOWN";
	}

	for (i = 0; i < size; i++) {
		if (names[i].id == id->id)
			return names[i].name;
	}

	return "UNKNOWN";
}

static u32 bcmn_scan_read32(struct bcmn_bus *bus, u16 offset)
{
	return readl(bus->mmio + offset);
}

static void bcmn_scan_switch_core(struct bcmn_bus *bus, u32 addr)
{
	if (bus->hosttype == BCMN_HOSTTYPE_PCI)
		pci_write_config_dword(bus->host_pci, BCMN_PCI_BAR0_WIN,
				       addr);
}

static u32 bcmn_erom_get_ent(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = readl(*eromptr);
	(*eromptr)++;
	return ent;
}

static void bcmn_erom_push_ent(u32 __iomem **eromptr)
{
	(*eromptr)--;
}

static s32 bcmn_erom_get_ci(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcmn_erom_get_ent(bus, eromptr);
	if (!(ent & SCAN_ER_VALID))
		return -ENOENT;
	if ((ent & SCAN_ER_TAG) != SCAN_ER_TAG_CI)
		return -ENOENT;
	return ent;
}

static bool bcmn_erom_is_end(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcmn_erom_get_ent(bus, eromptr);
	bcmn_erom_push_ent(eromptr);
	return (ent == (SCAN_ER_TAG_END | SCAN_ER_VALID));
}

static bool bcmn_erom_is_bridge(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcmn_erom_get_ent(bus, eromptr);
	bcmn_erom_push_ent(eromptr);
	return (((ent & SCAN_ER_VALID)) &&
		((ent & SCAN_ER_TAGX) == SCAN_ER_TAG_ADDR) &&
		((ent & SCAN_ADDR_TYPE) == SCAN_ADDR_TYPE_BRIDGE));
}

static void bcmn_erom_skip_component(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent;
	while (1) {
		ent = bcmn_erom_get_ent(bus, eromptr);
		if ((ent & SCAN_ER_VALID) &&
		    ((ent & SCAN_ER_TAG) == SCAN_ER_TAG_CI))
			break;
		if (ent == (SCAN_ER_TAG_END | SCAN_ER_VALID))
			break;
	}
	bcmn_erom_push_ent(eromptr);
}

static s32 bcmn_erom_get_mst_port(struct bcmn_bus *bus, u32 __iomem **eromptr)
{
	u32 ent = bcmn_erom_get_ent(bus, eromptr);
	if (!(ent & SCAN_ER_VALID))
		return -ENOENT;
	if ((ent & SCAN_ER_TAG) != SCAN_ER_TAG_MP)
		return -ENOENT;
	return ent;
}

static u32 bcmn_erom_get_addr_desc(struct bcmn_bus *bus, u32 __iomem **eromptr,
				  u32 type, u8 port)
{
	u32 addrl;
	u32 size;

	u32 ent = bcmn_erom_get_ent(bus, eromptr);
	if ((!(ent & SCAN_ER_VALID)) ||
	    ((ent & SCAN_ER_TAGX) != SCAN_ER_TAG_ADDR) ||
	    ((ent & SCAN_ADDR_TYPE) != type) ||
	    (((ent & SCAN_ADDR_PORT) >> SCAN_ADDR_PORT_SHIFT) != port)) {
		bcmn_erom_push_ent(eromptr);
		return (u32)-EINVAL;
	}

	addrl = ent & SCAN_ADDR_ADDR;
	if (ent & SCAN_ADDR_AG32)
		bcmn_erom_get_ent(bus, eromptr);

	if ((ent & SCAN_ADDR_SZ) == SCAN_ADDR_SZ_SZD) {
		size = bcmn_erom_get_ent(bus, eromptr);
		if (size & SCAN_SIZE_SG32)
			bcmn_erom_get_ent(bus, eromptr);
	}

	return addrl;
}

static struct bcmn_device *bcmn_find_core_by_index(struct bcmn_bus *bus,
						   u16 index)
{
	struct bcmn_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->core_index == index)
			return core;
	}
	return NULL;
}

static struct bcmn_device *bcmn_find_core_reverse(struct bcmn_bus *bus, u16 coreid)
{
	struct bcmn_device *core;

	list_for_each_entry_reverse(core, &bus->cores, list) {
		if (core->id.id == coreid)
			return core;
	}
	return NULL;
}

#define IS_ERR_VALUE_U32(x) ((x) >= (u32)-MAX_ERRNO)

static int bcmn_get_next_core(struct bcmn_bus *bus, u32 __iomem **eromptr,
			      struct bcmn_device_id *match, int core_num,
			      struct bcmn_device *core)
{
	u32 tmp;
	u8 i, j, k;
	s32 cia, cib;
	u8 ports[2], wrappers[2];

	/* get CIs */
	cia = bcmn_erom_get_ci(bus, eromptr);
	if (cia < 0) {
		bcmn_erom_push_ent(eromptr);
		if (bcmn_erom_is_end(bus, eromptr))
			return -ESPIPE;
		return -EILSEQ;
	}
	cib = bcmn_erom_get_ci(bus, eromptr);
	if (cib < 0)
		return -EILSEQ;

	/* parse CIs */
	core->id.class = (cia & SCAN_CIA_CLASS) >> SCAN_CIA_CLASS_SHIFT;
	core->id.id = (cia & SCAN_CIA_ID) >> SCAN_CIA_ID_SHIFT;
	core->id.manuf = (cia & SCAN_CIA_MANUF) >> SCAN_CIA_MANUF_SHIFT;
	ports[0] = (cib & SCAN_CIB_NMP) >> SCAN_CIB_NMP_SHIFT;
	ports[1] = (cib & SCAN_CIB_NSP) >> SCAN_CIB_NSP_SHIFT;
	wrappers[0] = (cib & SCAN_CIB_NMW) >> SCAN_CIB_NMW_SHIFT;
	wrappers[1] = (cib & SCAN_CIB_NSW) >> SCAN_CIB_NSW_SHIFT;
	core->id.rev = (cib & SCAN_CIB_REV) >> SCAN_CIB_REV_SHIFT;

	if (((core->id.manuf == BCMN_MANUF_ARM) &&
	     (core->id.id == 0xFFF)) ||
	    (ports[1] == 0)) {
		bcmn_erom_skip_component(bus, eromptr);
		return -ENXIO;
	}

	/* check if component is a core at all */
	if (wrappers[0] + wrappers[1] == 0) {
		/* Some specific cores don't need wrappers */
		switch (core->id.id) {
		case BCMN_CORE_4706_MAC_GBIT_COMMON:
		case BCMN_CORE_NS_CHIPCOMMON_B:
		case BCMN_CORE_PMU:
		case BCMN_CORE_GCI:
		/* Not used yet: case BCMN_CORE_OOB_ROUTER: */
			break;
		default:
			bcmn_erom_skip_component(bus, eromptr);
			return -ENXIO;
		}
	}

	if (bcmn_erom_is_bridge(bus, eromptr)) {
		bcmn_erom_skip_component(bus, eromptr);
		return -ENXIO;
	}

	if (bcmn_find_core_by_index(bus, core_num)) {
		bcmn_erom_skip_component(bus, eromptr);
		return -ENODEV;
	}

	if (match && ((match->manuf != BCMN_ANY_MANUF &&
	      match->manuf != core->id.manuf) ||
	     (match->id != BCMN_ANY_ID && match->id != core->id.id) ||
	     (match->rev != BCMN_ANY_REV && match->rev != core->id.rev) ||
	     (match->class != BCMN_ANY_CLASS && match->class != core->id.class)
	    )) {
		bcmn_erom_skip_component(bus, eromptr);
		return -ENODEV;
	}

	/* get & parse master ports */
	for (i = 0; i < ports[0]; i++) {
		s32 mst_port_d = bcmn_erom_get_mst_port(bus, eromptr);
		if (mst_port_d < 0)
			return -EILSEQ;
	}

	/* First Slave Address Descriptor should be port 0:
	 * the main register space for the core
	 */
	tmp = bcmn_erom_get_addr_desc(bus, eromptr, SCAN_ADDR_TYPE_SLAVE, 0);
	if (tmp == 0 || IS_ERR_VALUE_U32(tmp)) {
		/* Try again to see if it is a bridge */
		tmp = bcmn_erom_get_addr_desc(bus, eromptr,
					      SCAN_ADDR_TYPE_BRIDGE, 0);
		if (tmp == 0 || IS_ERR_VALUE_U32(tmp)) {
			return -EILSEQ;
		} else {
			bcmn_info(bus, "Bridge found\n");
			return -ENXIO;
		}
	}
	core->addr = tmp;

	/* get & parse slave ports */
	k = 0;
	for (i = 0; i < ports[1]; i++) {
		for (j = 0; ; j++) {
			tmp = bcmn_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_SLAVE, i);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: slave port %d "
				 * "has %d descriptors\n", i, j); */
				break;
			} else if (k < ARRAY_SIZE(core->addr_s)) {
				core->addr_s[k] = tmp;
				k++;
			}
		}
	}

	/* get & parse master wrappers */
	for (i = 0; i < wrappers[0]; i++) {
		for (j = 0; ; j++) {
			tmp = bcmn_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_MWRAP, i);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: master wrapper %d "
				 * "has %d descriptors\n", i, j); */
				break;
			} else {
				if (i == 0 && j == 0)
					core->wrap = tmp;
			}
		}
	}

	/* get & parse slave wrappers */
	for (i = 0; i < wrappers[1]; i++) {
		u8 hack = (ports[1] == 1) ? 0 : 1;
		for (j = 0; ; j++) {
			tmp = bcmn_erom_get_addr_desc(bus, eromptr,
				SCAN_ADDR_TYPE_SWRAP, i + hack);
			if (IS_ERR_VALUE_U32(tmp)) {
				/* no more entries for port _i_ */
				/* pr_debug("erom: master wrapper %d "
				 * has %d descriptors\n", i, j); */
				break;
			} else {
				if (wrappers[0] == 0 && !i && !j)
					core->wrap = tmp;
			}
		}
	}
	if (bus->hosttype == BCMN_HOSTTYPE_SOC) {
		core->io_addr = ioremap(core->addr, BCMN_CORE_SIZE);
		if (!core->io_addr)
			return -ENOMEM;
		if (core->wrap) {
			core->io_wrap = ioremap(core->wrap,
							BCMN_CORE_SIZE);
			if (!core->io_wrap) {
				iounmap(core->io_addr);
				return -ENOMEM;
			}
		}
	}
	return 0;
}

void bcmn_detect_chip(struct bcmn_bus *bus)
{
	s32 tmp;
	struct bcmn_chipinfo *chipinfo = &(bus->chipinfo);
	char chip_id[8];

	bcmn_scan_switch_core(bus, BCMN_ADDR_BASE);

	tmp = bcmn_scan_read32(bus, BCMN_CC_ID);
	chipinfo->id = (tmp & BCMN_CC_ID_ID) >> BCMN_CC_ID_ID_SHIFT;
	chipinfo->rev = (tmp & BCMN_CC_ID_REV) >> BCMN_CC_ID_REV_SHIFT;
	chipinfo->pkg = (tmp & BCMN_CC_ID_PKG) >> BCMN_CC_ID_PKG_SHIFT;

	snprintf(chip_id, ARRAY_SIZE(chip_id),
		 (chipinfo->id > 0x9999) ? "%d" : "0x%04X", chipinfo->id);
	bcmn_info(bus, "Found chip with id %s, rev 0x%02X and package 0x%02X\n",
		  chip_id, chipinfo->rev, chipinfo->pkg);
}

int bcmn_bus_scan(struct bcmn_bus *bus)
{
	u32 erombase;
	u32 __iomem *eromptr, *eromend;

	int err, core_num = 0;

	/* Skip if bus was already scanned (e.g. during early register) */
	if (bus->nr_cores)
		return 0;

	erombase = bcmn_scan_read32(bus, BCMN_CC_EROM);
	if (bus->hosttype == BCMN_HOSTTYPE_SOC) {
		eromptr = ioremap(erombase, BCMN_CORE_SIZE);
		if (!eromptr)
			return -ENOMEM;
	} else {
		eromptr = bus->mmio;
	}

	eromend = eromptr + BCMN_CORE_SIZE / sizeof(u32);

	bcmn_scan_switch_core(bus, erombase);

	while (eromptr < eromend) {
		struct bcmn_device *other_core;
		struct bcmn_device *core = kzalloc(sizeof(*core), GFP_KERNEL);
		if (!core) {
			err = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(&core->list);
		core->bus = bus;

		err = bcmn_get_next_core(bus, &eromptr, NULL, core_num, core);
		if (err < 0) {
			kfree(core);
			if (err == -ENODEV) {
				core_num++;
				continue;
			} else if (err == -ENXIO) {
				continue;
			} else if (err == -ESPIPE) {
				break;
			}
			goto out;
		}

		core->core_index = core_num++;
		bus->nr_cores++;
		other_core = bcmn_find_core_reverse(bus, core->id.id);
		core->core_unit = (other_core == NULL) ? 0 : other_core->core_unit + 1;
		bcmn_prepare_core(bus, core);

		bcmn_info(bus, "Core %d found: %s (manuf 0x%03X, id 0x%03X, rev 0x%02X, class 0x%X)\n",
			  core->core_index, bcmn_device_name(&core->id),
			  core->id.manuf, core->id.id, core->id.rev,
			  core->id.class);

		list_add_tail(&core->list, &bus->cores);
	}

	err = 0;
out:
	if (bus->hosttype == BCMN_HOSTTYPE_SOC)
		iounmap(eromptr);

	return err;
}
