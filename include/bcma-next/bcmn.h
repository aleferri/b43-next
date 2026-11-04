/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMN_H_
#define LINUX_BCMN_H_

#include <linux/pci.h>
#include <linux/mod_devicetable.h>

#include <bcma-next/bcmn_driver_arm_c9.h>
#include <bcma-next/bcmn_driver_chipcommon.h>
#include <bcma-next/bcmn_driver_pci.h>
#include <bcma-next/bcmn_driver_pcie2.h>
#include <bcma-next/bcmn_driver_mips.h>
#include <bcma-next/bcmn_driver_gmac_cmn.h>
#include <ssb-next/ssbn.h> /* SPROM sharing */

#include <bcma-next/bcmn_regs.h>

struct bcmn_device;
struct bcmn_bus;

/* As struct bcma_device_id in <linux/mod_devicetable.h> */
struct bcmn_device_id {
	__u16	manuf;
	__u16	id;
	__u8	rev;
	__u8	class;
} __attribute__((packed,aligned(2)));
#define BCMN_CORE(_manuf, _id, _rev, _class)  \
	{ .manuf = _manuf, .id = _id, .rev = _rev, .class = _class, }

#define BCMN_ANY_MANUF		0xFFFF
#define BCMN_ANY_ID		0xFFFF
#define BCMN_ANY_REV		0xFF
#define BCMN_ANY_CLASS		0xFF

enum bcmn_hosttype {
	BCMN_HOSTTYPE_PCI,
	BCMN_HOSTTYPE_SDIO,
	BCMN_HOSTTYPE_SOC,
};

struct bcmn_chipinfo {
	u16 id;
	u8 rev;
	u8 pkg;
};

struct bcmn_boardinfo {
	u16 vendor;
	u16 type;
};

enum bcmn_clkmode {
	BCMN_CLKMODE_FAST,
	BCMN_CLKMODE_DYNAMIC,
};

struct bcmn_host_ops {
	u8 (*read8)(struct bcmn_device *core, u16 offset);
	u16 (*read16)(struct bcmn_device *core, u16 offset);
	u32 (*read32)(struct bcmn_device *core, u16 offset);
	void (*write8)(struct bcmn_device *core, u16 offset, u8 value);
	void (*write16)(struct bcmn_device *core, u16 offset, u16 value);
	void (*write32)(struct bcmn_device *core, u16 offset, u32 value);
#ifdef CONFIG_BCMN_BLOCKIO
	void (*block_read)(struct bcmn_device *core, void *buffer,
			   size_t count, u16 offset, u8 reg_width);
	void (*block_write)(struct bcmn_device *core, const void *buffer,
			    size_t count, u16 offset, u8 reg_width);
#endif
	/* Agent ops */
	u32 (*aread32)(struct bcmn_device *core, u16 offset);
	void (*awrite32)(struct bcmn_device *core, u16 offset, u32 value);
};

/* Core manufacturers */
#define BCMN_MANUF_ARM			0x43B
#define BCMN_MANUF_MIPS			0x4A7
#define BCMN_MANUF_BCM			0x4BF

/* Core class values. */
#define BCMN_CL_SIM			0x0
#define BCMN_CL_EROM			0x1
#define BCMN_CL_CORESIGHT		0x9
#define BCMN_CL_VERIF			0xB
#define BCMN_CL_OPTIMO			0xD
#define BCMN_CL_GEN			0xE
#define BCMN_CL_PRIMECELL		0xF

/* Core-ID values. */
#define BCMN_CORE_OOB_ROUTER		0x367	/* Out of band */
#define BCMN_CORE_4706_CHIPCOMMON	0x500
#define BCMN_CORE_NS_PCIEG2		0x501
#define BCMN_CORE_NS_DMA		0x502
#define BCMN_CORE_NS_SDIO3		0x503
#define BCMN_CORE_NS_USB20		0x504
#define BCMN_CORE_NS_USB30		0x505
#define BCMN_CORE_NS_A9JTAG		0x506
#define BCMN_CORE_NS_DDR23		0x507
#define BCMN_CORE_NS_ROM		0x508
#define BCMN_CORE_NS_NAND		0x509
#define BCMN_CORE_NS_QSPI		0x50A
#define BCMN_CORE_NS_CHIPCOMMON_B	0x50B
#define BCMN_CORE_4706_SOC_RAM		0x50E
#define BCMN_CORE_ARMCA9		0x510
#define BCMN_CORE_4706_MAC_GBIT		0x52D
#define BCMN_CORE_AMEMC			0x52E	/* DDR1/2 memory controller core */
#define BCMN_CORE_ALTA			0x534	/* I2S core */
#define BCMN_CORE_4706_MAC_GBIT_COMMON	0x5DC
#define BCMN_CORE_DDR23_PHY		0x5DD
#define BCMN_CORE_INVALID		0x700
#define BCMN_CORE_CHIPCOMMON		0x800
#define BCMN_CORE_ILINE20		0x801
#define BCMN_CORE_SRAM			0x802
#define BCMN_CORE_SDRAM			0x803
#define BCMN_CORE_PCI			0x804
#define BCMN_CORE_MIPS			0x805
#define BCMN_CORE_ETHERNET		0x806
#define BCMN_CORE_V90			0x807
#define BCMN_CORE_USB11_HOSTDEV		0x808
#define BCMN_CORE_ADSL			0x809
#define BCMN_CORE_ILINE100		0x80A
#define BCMN_CORE_IPSEC			0x80B
#define BCMN_CORE_UTOPIA		0x80C
#define BCMN_CORE_PCMCIA		0x80D
#define BCMN_CORE_INTERNAL_MEM		0x80E
#define BCMN_CORE_MEMC_SDRAM		0x80F
#define BCMN_CORE_OFDM			0x810
#define BCMN_CORE_EXTIF			0x811
#define BCMN_CORE_80211			0x812
#define BCMN_CORE_PHY_A			0x813
#define BCMN_CORE_PHY_B			0x814
#define BCMN_CORE_PHY_G			0x815
#define BCMN_CORE_MIPS_3302		0x816
#define BCMN_CORE_USB11_HOST		0x817
#define BCMN_CORE_USB11_DEV		0x818
#define BCMN_CORE_USB20_HOST		0x819
#define BCMN_CORE_USB20_DEV		0x81A
#define BCMN_CORE_SDIO_HOST		0x81B
#define BCMN_CORE_ROBOSWITCH		0x81C
#define BCMN_CORE_PARA_ATA		0x81D
#define BCMN_CORE_SATA_XORDMA		0x81E
#define BCMN_CORE_ETHERNET_GBIT		0x81F
#define BCMN_CORE_PCIE			0x820
#define BCMN_CORE_PHY_N			0x821
#define BCMN_CORE_SRAM_CTL		0x822
#define BCMN_CORE_MINI_MACPHY		0x823
#define BCMN_CORE_ARM_1176		0x824
#define BCMN_CORE_ARM_7TDMI		0x825
#define BCMN_CORE_PHY_LP		0x826
#define BCMN_CORE_PMU			0x827
#define BCMN_CORE_PHY_SSN		0x828
#define BCMN_CORE_SDIO_DEV		0x829
#define BCMN_CORE_ARM_CM3		0x82A
#define BCMN_CORE_PHY_HT		0x82B
#define BCMN_CORE_MIPS_74K		0x82C
#define BCMN_CORE_MAC_GBIT		0x82D
#define BCMN_CORE_DDR12_MEM_CTL		0x82E
#define BCMN_CORE_PCIE_RC		0x82F	/* PCIe Root Complex */
#define BCMN_CORE_OCP_OCP_BRIDGE	0x830
#define BCMN_CORE_SHARED_COMMON		0x831
#define BCMN_CORE_OCP_AHB_BRIDGE	0x832
#define BCMN_CORE_SPI_HOST		0x833
#define BCMN_CORE_I2S			0x834
#define BCMN_CORE_SDR_DDR1_MEM_CTL	0x835	/* SDR/DDR1 memory controller core */
#define BCMN_CORE_SHIM			0x837	/* SHIM component in ubus/6362 */
#define BCMN_CORE_PHY_AC		0x83B
#define BCMN_CORE_PCIE2			0x83C	/* PCI Express Gen2 */
#define BCMN_CORE_USB30_DEV		0x83D
#define BCMN_CORE_ARM_CR4		0x83E
#define BCMN_CORE_GCI			0x840
#define BCMN_CORE_CMEM			0x846	/* CNDS DDR2/3 memory controller */
#define BCMN_CORE_ARM_CA7		0x847
#define BCMN_CORE_SYS_MEM		0x849
#define BCMN_CORE_DEFAULT		0xFFF

#define BCMN_MAX_NR_CORES		16
#define BCMN_CORE_SIZE			0x1000

/* Chip IDs of PCIe devices */
#define BCMN_CHIP_ID_BCM4313	0x4313
#define BCMN_CHIP_ID_BCM43142	43142
#define BCMN_CHIP_ID_BCM43131	43131
#define BCMN_CHIP_ID_BCM43217	43217
#define BCMN_CHIP_ID_BCM43222	43222
#define BCMN_CHIP_ID_BCM43224	43224
#define  BCMN_PKG_ID_BCM43224_FAB_CSM	0x8
#define  BCMN_PKG_ID_BCM43224_FAB_SMIC	0xa
#define BCMN_CHIP_ID_BCM43225	43225
#define BCMN_CHIP_ID_BCM43227	43227
#define BCMN_CHIP_ID_BCM43228	43228
#define BCMN_CHIP_ID_BCM43421	43421
#define BCMN_CHIP_ID_BCM43428	43428
#define BCMN_CHIP_ID_BCM43431	43431
#define BCMN_CHIP_ID_BCM43460	43460
#define BCMN_CHIP_ID_BCM4331	0x4331
#define BCMN_CHIP_ID_BCM6362	0x6362
#define BCMN_CHIP_ID_BCM4360	0x4360
#define BCMN_CHIP_ID_BCM4352	0x4352

/* Chip IDs of SoCs */
#define BCMN_CHIP_ID_BCM4706	0x5300
#define  BCMN_PKG_ID_BCM4706L	1
#define BCMN_CHIP_ID_BCM4716	0x4716
#define  BCMN_PKG_ID_BCM4716	8
#define  BCMN_PKG_ID_BCM4717	9
#define  BCMN_PKG_ID_BCM4718	10
#define BCMN_CHIP_ID_BCM47162	47162
#define BCMN_CHIP_ID_BCM4748	0x4748
#define BCMN_CHIP_ID_BCM4749	0x4749
#define BCMN_CHIP_ID_BCM5356	0x5356
#define BCMN_CHIP_ID_BCM5357	0x5357
#define  BCMN_PKG_ID_BCM5358	9
#define  BCMN_PKG_ID_BCM47186	10
#define  BCMN_PKG_ID_BCM5357	11
#define BCMN_CHIP_ID_BCM53572	53572
#define  BCMN_PKG_ID_BCM47188	9
#define BCMN_CHIP_ID_BCM4707	53010
#define  BCMN_PKG_ID_BCM4707	1
#define  BCMN_PKG_ID_BCM4708	2
#define  BCMN_PKG_ID_BCM4709	0
#define BCMN_CHIP_ID_BCM47094	53030
#define BCMN_CHIP_ID_BCM53018	53018
#define BCMN_CHIP_ID_BCM53573	53573
#define  BCMN_PKG_ID_BCM53573	0
#define  BCMN_PKG_ID_BCM47189	1

/* Board types (on PCI usually equals to the subsystem dev id) */
/* BCM4313 */
#define BCMN_BOARD_TYPE_BCM94313BU	0X050F
#define BCMN_BOARD_TYPE_BCM94313HM	0X0510
#define BCMN_BOARD_TYPE_BCM94313EPA	0X0511
#define BCMN_BOARD_TYPE_BCM94313HMG	0X051C
/* BCM4716 */
#define BCMN_BOARD_TYPE_BCM94716NR2	0X04CD
/* BCM43224 */
#define BCMN_BOARD_TYPE_BCM943224X21	0X056E
#define BCMN_BOARD_TYPE_BCM943224X21_FCC	0X00D1
#define BCMN_BOARD_TYPE_BCM943224X21B	0X00E9
#define BCMN_BOARD_TYPE_BCM943224M93	0X008B
#define BCMN_BOARD_TYPE_BCM943224M93A	0X0090
#define BCMN_BOARD_TYPE_BCM943224X16	0X0093
#define BCMN_BOARD_TYPE_BCM94322X9	0X008D
#define BCMN_BOARD_TYPE_BCM94322M35E	0X008E
/* BCM43228 */
#define BCMN_BOARD_TYPE_BCM943228BU8	0X0540
#define BCMN_BOARD_TYPE_BCM943228BU9	0X0541
#define BCMN_BOARD_TYPE_BCM943228BU	0X0542
#define BCMN_BOARD_TYPE_BCM943227HM4L	0X0543
#define BCMN_BOARD_TYPE_BCM943227HMB	0X0544
#define BCMN_BOARD_TYPE_BCM943228HM4L	0X0545
#define BCMN_BOARD_TYPE_BCM943228SD	0X0573
/* BCM4331 */
#define BCMN_BOARD_TYPE_BCM94331X19	0X00D6
#define BCMN_BOARD_TYPE_BCM94331X28	0X00E4
#define BCMN_BOARD_TYPE_BCM94331X28B	0X010E
#define BCMN_BOARD_TYPE_BCM94331PCIEBT3AX	0X00E4
#define BCMN_BOARD_TYPE_BCM94331X12_2G	0X00EC
#define BCMN_BOARD_TYPE_BCM94331X12_5G	0X00ED
#define BCMN_BOARD_TYPE_BCM94331X29B	0X00EF
#define BCMN_BOARD_TYPE_BCM94331CSAX	0X00EF
#define BCMN_BOARD_TYPE_BCM94331X19C	0X00F5
#define BCMN_BOARD_TYPE_BCM94331X33	0X00F4
#define BCMN_BOARD_TYPE_BCM94331BU	0X0523
#define BCMN_BOARD_TYPE_BCM94331S9BU	0X0524
#define BCMN_BOARD_TYPE_BCM94331MC	0X0525
#define BCMN_BOARD_TYPE_BCM94331MCI	0X0526
#define BCMN_BOARD_TYPE_BCM94331PCIEBT4	0X0527
#define BCMN_BOARD_TYPE_BCM94331HM	0X0574
#define BCMN_BOARD_TYPE_BCM94331PCIEDUAL	0X059B
#define BCMN_BOARD_TYPE_BCM94331MCH5	0X05A9
#define BCMN_BOARD_TYPE_BCM94331CS	0X05C6
#define BCMN_BOARD_TYPE_BCM94331CD	0X05DA
/* BCM53572 */
#define BCMN_BOARD_TYPE_BCM953572BU	0X058D
#define BCMN_BOARD_TYPE_BCM953572NR2	0X058E
#define BCMN_BOARD_TYPE_BCM947188NR2	0X058F
#define BCMN_BOARD_TYPE_BCM953572SDRNR2	0X0590
/* BCM43142 */
#define BCMN_BOARD_TYPE_BCM943142HM	0X05E0

struct bcmn_device {
	struct bcmn_bus *bus;
	struct bcmn_device_id id;

	struct device dev;
	struct device *dma_dev;

	unsigned int irq;
	bool dev_registered;

	u8 core_index;
	u8 core_unit;

	u32 addr;
	u32 addr_s[8];
	u32 wrap;

	void __iomem *io_addr;
	void __iomem *io_wrap;

	void *drvdata;
	struct list_head list;
};

static inline void *bcmn_get_drvdata(struct bcmn_device *core)
{
	return core->drvdata;
}
static inline void bcmn_set_drvdata(struct bcmn_device *core, void *drvdata)
{
	core->drvdata = drvdata;
}

struct bcmn_driver {
	const char *name;
	const struct bcmn_device_id *id_table;

	int (*probe)(struct bcmn_device *dev);
	void (*remove)(struct bcmn_device *dev);
	int (*suspend)(struct bcmn_device *dev);
	int (*resume)(struct bcmn_device *dev);
	void (*shutdown)(struct bcmn_device *dev);

	struct device_driver drv;
};
extern
int __bcmn_driver_register(struct bcmn_driver *drv, struct module *owner);
#define bcmn_driver_register(drv) \
	__bcmn_driver_register(drv, THIS_MODULE)

extern void bcmn_driver_unregister(struct bcmn_driver *drv);

/* The bus type and its PCI host, set up by the init of the module they are in */
int bcmn_modinit(void);
void bcmn_modexit(void);

/* module_bcma_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_bcma_driver(__bcmn_driver) \
	module_driver(__bcmn_driver, bcmn_driver_register, \
			bcmn_driver_unregister)

struct bcmn_bus {
	struct device *dev;

	/* The MMIO area. */
	void __iomem *mmio;

	const struct bcmn_host_ops *ops;

	enum bcmn_hosttype hosttype;
	bool host_is_pcie2; /* Used for BCMN_HOSTTYPE_PCI only */
	struct pci_dev *host_pci; /* PCI bus pointer (BCMN_HOSTTYPE_PCI only) */

	struct bcmn_chipinfo chipinfo;

	struct bcmn_boardinfo boardinfo;

	struct bcmn_device *mapped_core;
	struct list_head cores;
	u8 nr_cores;
	u8 num;

	struct bcmn_drv_cc drv_cc;
	struct bcmn_drv_cc_b drv_cc_b;
	struct bcmn_drv_pci drv_pci[2];
	struct bcmn_drv_pcie2 drv_pcie2;
	struct bcmn_drv_mips drv_mips;
	struct bcmn_drv_gmac_cmn drv_gmac_cmn;

	/* We decided to share SPROM struct with SSB as long as we do not need
	 * any hacks for BCMA. This simplifies drivers code. */
	struct ssbn_sprom sprom;

	/* Backplane registers, EROM included, are big-endian */
	bool big_endian;
};

static inline u32 bcmn_read8(struct bcmn_device *core, u16 offset)
{
	return core->bus->ops->read8(core, offset);
}
static inline u32 bcmn_read16(struct bcmn_device *core, u16 offset)
{
	return core->bus->ops->read16(core, offset);
}
static inline u32 bcmn_read32(struct bcmn_device *core, u16 offset)
{
	return core->bus->ops->read32(core, offset);
}
static inline
void bcmn_write8(struct bcmn_device *core, u16 offset, u32 value)
{
	core->bus->ops->write8(core, offset, value);
}
static inline
void bcmn_write16(struct bcmn_device *core, u16 offset, u32 value)
{
	core->bus->ops->write16(core, offset, value);
}
static inline
void bcmn_write32(struct bcmn_device *core, u16 offset, u32 value)
{
	core->bus->ops->write32(core, offset, value);
}
#ifdef CONFIG_BCMN_BLOCKIO
static inline void bcmn_block_read(struct bcmn_device *core, void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	core->bus->ops->block_read(core, buffer, count, offset, reg_width);
}
static inline void bcmn_block_write(struct bcmn_device *core,
				    const void *buffer, size_t count,
				    u16 offset, u8 reg_width)
{
	core->bus->ops->block_write(core, buffer, count, offset, reg_width);
}
#endif
static inline u32 bcmn_aread32(struct bcmn_device *core, u16 offset)
{
	return core->bus->ops->aread32(core, offset);
}
static inline
void bcmn_awrite32(struct bcmn_device *core, u16 offset, u32 value)
{
	core->bus->ops->awrite32(core, offset, value);
}

static inline void bcmn_mask32(struct bcmn_device *cc, u16 offset, u32 mask)
{
	bcmn_write32(cc, offset, bcmn_read32(cc, offset) & mask);
}
static inline void bcmn_set32(struct bcmn_device *cc, u16 offset, u32 set)
{
	bcmn_write32(cc, offset, bcmn_read32(cc, offset) | set);
}
static inline void bcmn_maskset32(struct bcmn_device *cc,
				  u16 offset, u32 mask, u32 set)
{
	bcmn_write32(cc, offset, (bcmn_read32(cc, offset) & mask) | set);
}
static inline void bcmn_mask16(struct bcmn_device *cc, u16 offset, u16 mask)
{
	bcmn_write16(cc, offset, bcmn_read16(cc, offset) & mask);
}
static inline void bcmn_set16(struct bcmn_device *cc, u16 offset, u16 set)
{
	bcmn_write16(cc, offset, bcmn_read16(cc, offset) | set);
}
static inline void bcmn_maskset16(struct bcmn_device *cc,
				  u16 offset, u16 mask, u16 set)
{
	bcmn_write16(cc, offset, (bcmn_read16(cc, offset) & mask) | set);
}

extern struct bcmn_device *bcmn_find_core_unit(struct bcmn_bus *bus, u16 coreid,
					       u8 unit);
static inline struct bcmn_device *bcmn_find_core(struct bcmn_bus *bus,
						 u16 coreid)
{
	return bcmn_find_core_unit(bus, coreid, 0);
}

#ifdef CONFIG_BCMN_HOST_PCI
extern void bcmn_host_pci_up(struct bcmn_bus *bus);
extern void bcmn_host_pci_down(struct bcmn_bus *bus);
extern int bcmn_host_pci_irq_ctl(struct bcmn_bus *bus,
				 struct bcmn_device *core, bool enable);
#else
static inline void bcmn_host_pci_up(struct bcmn_bus *bus)
{
}
static inline void bcmn_host_pci_down(struct bcmn_bus *bus)
{
}
static inline int bcmn_host_pci_irq_ctl(struct bcmn_bus *bus,
					struct bcmn_device *core, bool enable)
{
	if (bus->hosttype == BCMN_HOSTTYPE_PCI)
		return -ENOTSUPP;
	return 0;
}
#endif

extern bool bcmn_core_is_enabled(struct bcmn_device *core);
extern void bcmn_core_disable(struct bcmn_device *core, u32 flags);
extern int bcmn_core_enable(struct bcmn_device *core, u32 flags);
extern void bcmn_core_set_clockmode(struct bcmn_device *core,
				    enum bcmn_clkmode clkmode);
extern void bcmn_core_pll_ctl(struct bcmn_device *core, u32 req, u32 status,
			      bool on);
extern u32 bcmn_chipco_pll_read(struct bcmn_drv_cc *cc, u32 offset);
#define BCMN_DMA_TRANSLATION_MASK	0xC0000000
#define  BCMN_DMA_TRANSLATION_NONE	0x00000000
#define  BCMN_DMA_TRANSLATION_DMA32_CMT	0x40000000 /* Client Mode Translation for 32-bit DMA */
#define  BCMN_DMA_TRANSLATION_DMA64_CMT	0x80000000 /* Client Mode Translation for 64-bit DMA */
extern u32 bcmn_core_dma_translation(struct bcmn_device *core);

extern unsigned int bcmn_core_irq(struct bcmn_device *core, int num);

#endif /* LINUX_BCMN_H_ */
