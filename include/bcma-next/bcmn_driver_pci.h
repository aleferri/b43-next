/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMN_DRIVER_PCI_H_
#define LINUX_BCMN_DRIVER_PCI_H_

#include <linux/types.h>

struct pci_dev;

/** PCI core registers. **/
#define BCMN_CORE_PCI_CTL			0x0000	/* PCI Control */
#define  BCMN_CORE_PCI_CTL_RST_OE		0x00000001 /* PCI_RESET Output Enable */
#define  BCMN_CORE_PCI_CTL_RST			0x00000002 /* PCI_RESET driven out to pin */
#define  BCMN_CORE_PCI_CTL_CLK_OE		0x00000004 /* Clock gate Output Enable */
#define  BCMN_CORE_PCI_CTL_CLK			0x00000008 /* Gate for clock driven out to pin */
#define BCMN_CORE_PCI_ARBCTL			0x0010	/* PCI Arbiter Control */
#define  BCMN_CORE_PCI_ARBCTL_INTERN		0x00000001 /* Use internal arbiter */
#define  BCMN_CORE_PCI_ARBCTL_EXTERN		0x00000002 /* Use external arbiter */
#define  BCMN_CORE_PCI_ARBCTL_PARKID		0x00000006 /* Mask, selects which agent is parked on an idle bus */
#define   BCMN_CORE_PCI_ARBCTL_PARKID_LAST	0x00000000 /* Last requestor */
#define   BCMN_CORE_PCI_ARBCTL_PARKID_4710	0x00000002 /* 4710 */
#define   BCMN_CORE_PCI_ARBCTL_PARKID_EXT0	0x00000004 /* External requestor 0 */
#define   BCMN_CORE_PCI_ARBCTL_PARKID_EXT1	0x00000006 /* External requestor 1 */
#define BCMN_CORE_PCI_ISTAT			0x0020	/* Interrupt status */
#define  BCMN_CORE_PCI_ISTAT_INTA		0x00000001 /* PCI INTA# */
#define  BCMN_CORE_PCI_ISTAT_INTB		0x00000002 /* PCI INTB# */
#define  BCMN_CORE_PCI_ISTAT_SERR		0x00000004 /* PCI SERR# (write to clear) */
#define  BCMN_CORE_PCI_ISTAT_PERR		0x00000008 /* PCI PERR# (write to clear) */
#define  BCMN_CORE_PCI_ISTAT_PME		0x00000010 /* PCI PME# */
#define BCMN_CORE_PCI_IMASK			0x0024	/* Interrupt mask */
#define  BCMN_CORE_PCI_IMASK_INTA		0x00000001 /* PCI INTA# */
#define  BCMN_CORE_PCI_IMASK_INTB		0x00000002 /* PCI INTB# */
#define  BCMN_CORE_PCI_IMASK_SERR		0x00000004 /* PCI SERR# */
#define  BCMN_CORE_PCI_IMASK_PERR		0x00000008 /* PCI PERR# */
#define  BCMN_CORE_PCI_IMASK_PME		0x00000010 /* PCI PME# */
#define BCMN_CORE_PCI_MBOX			0x0028	/* Backplane to PCI Mailbox */
#define  BCMN_CORE_PCI_MBOX_F0_0		0x00000100 /* PCI function 0, INT 0 */
#define  BCMN_CORE_PCI_MBOX_F0_1		0x00000200 /* PCI function 0, INT 1 */
#define  BCMN_CORE_PCI_MBOX_F1_0		0x00000400 /* PCI function 1, INT 0 */
#define  BCMN_CORE_PCI_MBOX_F1_1		0x00000800 /* PCI function 1, INT 1 */
#define  BCMN_CORE_PCI_MBOX_F2_0		0x00001000 /* PCI function 2, INT 0 */
#define  BCMN_CORE_PCI_MBOX_F2_1		0x00002000 /* PCI function 2, INT 1 */
#define  BCMN_CORE_PCI_MBOX_F3_0		0x00004000 /* PCI function 3, INT 0 */
#define  BCMN_CORE_PCI_MBOX_F3_1		0x00008000 /* PCI function 3, INT 1 */
#define BCMN_CORE_PCI_BCAST_ADDR		0x0050	/* Backplane Broadcast Address */
#define  BCMN_CORE_PCI_BCAST_ADDR_MASK		0x000000FF
#define BCMN_CORE_PCI_BCAST_DATA		0x0054	/* Backplane Broadcast Data */
#define BCMN_CORE_PCI_GPIO_IN			0x0060	/* rev >= 2 only */
#define BCMN_CORE_PCI_GPIO_OUT			0x0064	/* rev >= 2 only */
#define BCMN_CORE_PCI_GPIO_ENABLE		0x0068	/* rev >= 2 only */
#define BCMN_CORE_PCI_GPIO_CTL			0x006C	/* rev >= 2 only */
#define BCMN_CORE_PCI_SBTOPCI0			0x0100	/* Backplane to PCI translation 0 (sbtopci0) */
#define  BCMN_CORE_PCI_SBTOPCI0_MASK		0xFC000000
#define BCMN_CORE_PCI_SBTOPCI1			0x0104	/* Backplane to PCI translation 1 (sbtopci1) */
#define  BCMN_CORE_PCI_SBTOPCI1_MASK		0xFC000000
#define BCMN_CORE_PCI_SBTOPCI2			0x0108	/* Backplane to PCI translation 2 (sbtopci2) */
#define  BCMN_CORE_PCI_SBTOPCI2_MASK		0xC0000000
#define BCMN_CORE_PCI_CONFIG_ADDR		0x0120	/* pcie config space access */
#define BCMN_CORE_PCI_CONFIG_DATA		0x0124	/* pcie config space access */
#define BCMN_CORE_PCI_MDIO_CONTROL		0x0128	/* controls the mdio access */
#define  BCMN_CORE_PCI_MDIOCTL_DIVISOR_MASK	0x7f	/* clock to be used on MDIO */
#define  BCMN_CORE_PCI_MDIOCTL_DIVISOR_VAL	0x2
#define  BCMN_CORE_PCI_MDIOCTL_PREAM_EN		0x80	/* Enable preamble sequnce */
#define  BCMN_CORE_PCI_MDIOCTL_ACCESS_DONE	0x100	/* Tranaction complete */
#define BCMN_CORE_PCI_MDIO_DATA			0x012c	/* Data to the mdio access */
#define  BCMN_CORE_PCI_MDIODATA_MASK		0x0000ffff /* data 2 bytes */
#define  BCMN_CORE_PCI_MDIODATA_TA		0x00020000 /* Turnaround */
#define  BCMN_CORE_PCI_MDIODATA_REGADDR_SHF_OLD	18	/* Regaddr shift (rev < 10) */
#define  BCMN_CORE_PCI_MDIODATA_REGADDR_MASK_OLD	0x003c0000 /* Regaddr Mask (rev < 10) */
#define  BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF_OLD	22	/* Physmedia devaddr shift (rev < 10) */
#define  BCMN_CORE_PCI_MDIODATA_DEVADDR_MASK_OLD	0x0fc00000 /* Physmedia devaddr Mask (rev < 10) */
#define  BCMN_CORE_PCI_MDIODATA_REGADDR_SHF	18	/* Regaddr shift */
#define  BCMN_CORE_PCI_MDIODATA_REGADDR_MASK	0x007c0000 /* Regaddr Mask */
#define  BCMN_CORE_PCI_MDIODATA_DEVADDR_SHF	23	/* Physmedia devaddr shift */
#define  BCMN_CORE_PCI_MDIODATA_DEVADDR_MASK	0x0f800000 /* Physmedia devaddr Mask */
#define  BCMN_CORE_PCI_MDIODATA_WRITE		0x10000000 /* write Transaction */
#define  BCMN_CORE_PCI_MDIODATA_READ		0x20000000 /* Read Transaction */
#define  BCMN_CORE_PCI_MDIODATA_START		0x40000000 /* start of Transaction */
#define  BCMN_CORE_PCI_MDIODATA_DEV_ADDR	0x0	/* dev address for serdes */
#define  BCMN_CORE_PCI_MDIODATA_BLK_ADDR	0x1F	/* blk address for serdes */
#define  BCMN_CORE_PCI_MDIODATA_DEV_PLL		0x1d	/* SERDES PLL Dev */
#define  BCMN_CORE_PCI_MDIODATA_DEV_TX		0x1e	/* SERDES TX Dev */
#define  BCMN_CORE_PCI_MDIODATA_DEV_RX		0x1f	/* SERDES RX Dev */
#define BCMN_CORE_PCI_PCIEIND_ADDR		0x0130	/* indirect access to the internal register */
#define BCMN_CORE_PCI_PCIEIND_DATA		0x0134	/* Data to/from the internal register */
#define BCMN_CORE_PCI_CLKREQENCTRL		0x0138	/*  >= rev 6, Clkreq rdma control */
#define BCMN_CORE_PCI_PCICFG0			0x0400	/* PCI config space 0 (rev >= 8) */
#define BCMN_CORE_PCI_PCICFG1			0x0500	/* PCI config space 1 (rev >= 8) */
#define BCMN_CORE_PCI_PCICFG2			0x0600	/* PCI config space 2 (rev >= 8) */
#define BCMN_CORE_PCI_PCICFG3			0x0700	/* PCI config space 3 (rev >= 8) */
#define BCMN_CORE_PCI_SPROM(wordoffset)		(0x0800 + ((wordoffset) * 2)) /* SPROM shadow area (72 bytes) */
#define  BCMN_CORE_PCI_SPROM_PI_OFFSET		0	/* first word */
#define   BCMN_CORE_PCI_SPROM_PI_MASK		0xf000	/* bit 15:12 */
#define   BCMN_CORE_PCI_SPROM_PI_SHIFT		12	/* bit 15:12 */
#define  BCMN_CORE_PCI_SPROM_MISC_CONFIG	5	/* word 5 */
#define   BCMN_CORE_PCI_SPROM_L23READY_EXIT_NOPERST	0x8000	/* bit 15 */
#define   BCMN_CORE_PCI_SPROM_CLKREQ_OFFSET_REV5	20	/* word 20 for srom rev <= 5 */
#define   BCMN_CORE_PCI_SPROM_CLKREQ_ENB	0x0800	/* bit 11 */

/* SBtoPCIx */
#define BCMN_CORE_PCI_SBTOPCI_MEM		0x00000000
#define BCMN_CORE_PCI_SBTOPCI_IO		0x00000001
#define BCMN_CORE_PCI_SBTOPCI_CFG0		0x00000002
#define BCMN_CORE_PCI_SBTOPCI_CFG1		0x00000003
#define BCMN_CORE_PCI_SBTOPCI_PREF		0x00000004 /* Prefetch enable */
#define BCMN_CORE_PCI_SBTOPCI_BURST		0x00000008 /* Burst enable */
#define BCMN_CORE_PCI_SBTOPCI_MRM		0x00000020 /* Memory Read Multiple */
#define BCMN_CORE_PCI_SBTOPCI_RC		0x00000030 /* Read Command mask (rev >= 11) */
#define  BCMN_CORE_PCI_SBTOPCI_RC_READ		0x00000000 /* Memory read */
#define  BCMN_CORE_PCI_SBTOPCI_RC_READL		0x00000010 /* Memory read line */
#define  BCMN_CORE_PCI_SBTOPCI_RC_READM		0x00000020 /* Memory read multiple */

/* PCIE protocol PHY diagnostic registers */
#define BCMN_CORE_PCI_PLP_MODEREG		0x200	/* Mode */
#define BCMN_CORE_PCI_PLP_STATUSREG		0x204	/* Status */
#define  BCMN_CORE_PCI_PLP_POLARITYINV_STAT	0x10	/* Status reg PCIE_PLP_STATUSREG */
#define BCMN_CORE_PCI_PLP_LTSSMCTRLREG		0x208	/* LTSSM control */
#define BCMN_CORE_PCI_PLP_LTLINKNUMREG		0x20c	/* Link Training Link number */
#define BCMN_CORE_PCI_PLP_LTLANENUMREG		0x210	/* Link Training Lane number */
#define BCMN_CORE_PCI_PLP_LTNFTSREG		0x214	/* Link Training N_FTS */
#define BCMN_CORE_PCI_PLP_ATTNREG		0x218	/* Attention */
#define BCMN_CORE_PCI_PLP_ATTNMASKREG		0x21C	/* Attention Mask */
#define BCMN_CORE_PCI_PLP_RXERRCTR		0x220	/* Rx Error */
#define BCMN_CORE_PCI_PLP_RXFRMERRCTR		0x224	/* Rx Framing Error */
#define BCMN_CORE_PCI_PLP_RXERRTHRESHREG	0x228	/* Rx Error threshold */
#define BCMN_CORE_PCI_PLP_TESTCTRLREG		0x22C	/* Test Control reg */
#define BCMN_CORE_PCI_PLP_SERDESCTRLOVRDREG	0x230	/* SERDES Control Override */
#define BCMN_CORE_PCI_PLP_TIMINGOVRDREG		0x234	/* Timing param override */
#define BCMN_CORE_PCI_PLP_RXTXSMDIAGREG		0x238	/* RXTX State Machine Diag */
#define BCMN_CORE_PCI_PLP_LTSSMDIAGREG		0x23C	/* LTSSM State Machine Diag */

/* PCIE protocol DLLP diagnostic registers */
#define BCMN_CORE_PCI_DLLP_LCREG		0x100	/* Link Control */
#define BCMN_CORE_PCI_DLLP_LSREG		0x104	/* Link Status */
#define BCMN_CORE_PCI_DLLP_LAREG		0x108	/* Link Attention */
#define  BCMN_CORE_PCI_DLLP_LSREG_LINKUP	(1 << 16)
#define BCMN_CORE_PCI_DLLP_LAMASKREG		0x10C	/* Link Attention Mask */
#define BCMN_CORE_PCI_DLLP_NEXTTXSEQNUMREG	0x110	/* Next Tx Seq Num */
#define BCMN_CORE_PCI_DLLP_ACKEDTXSEQNUMREG	0x114	/* Acked Tx Seq Num */
#define BCMN_CORE_PCI_DLLP_PURGEDTXSEQNUMREG	0x118	/* Purged Tx Seq Num */
#define BCMN_CORE_PCI_DLLP_RXSEQNUMREG		0x11C	/* Rx Sequence Number */
#define BCMN_CORE_PCI_DLLP_LRREG		0x120	/* Link Replay */
#define BCMN_CORE_PCI_DLLP_LACKTOREG		0x124	/* Link Ack Timeout */
#define BCMN_CORE_PCI_DLLP_PMTHRESHREG		0x128	/* Power Management Threshold */
#define  BCMN_CORE_PCI_ASPMTIMER_EXTEND		0x01000000 /* > rev7: enable extend ASPM timer */
#define BCMN_CORE_PCI_DLLP_RTRYWPREG		0x12C	/* Retry buffer write ptr */
#define BCMN_CORE_PCI_DLLP_RTRYRPREG		0x130	/* Retry buffer Read ptr */
#define BCMN_CORE_PCI_DLLP_RTRYPPREG		0x134	/* Retry buffer Purged ptr */
#define BCMN_CORE_PCI_DLLP_RTRRWREG		0x138	/* Retry buffer Read/Write */
#define BCMN_CORE_PCI_DLLP_ECTHRESHREG		0x13C	/* Error Count Threshold */
#define BCMN_CORE_PCI_DLLP_TLPERRCTRREG		0x140	/* TLP Error Counter */
#define BCMN_CORE_PCI_DLLP_ERRCTRREG		0x144	/* Error Counter */
#define BCMN_CORE_PCI_DLLP_NAKRXCTRREG		0x148	/* NAK Received Counter */
#define BCMN_CORE_PCI_DLLP_TESTREG		0x14C	/* Test */
#define BCMN_CORE_PCI_DLLP_PKTBIST		0x150	/* Packet BIST */
#define BCMN_CORE_PCI_DLLP_PCIE11		0x154	/* DLLP PCIE 1.1 reg */

/* SERDES RX registers */
#define BCMN_CORE_PCI_SERDES_RX_CTRL		1	/* Rx cntrl */
#define  BCMN_CORE_PCI_SERDES_RX_CTRL_FORCE	0x80	/* rxpolarity_force */
#define  BCMN_CORE_PCI_SERDES_RX_CTRL_POLARITY	0x40	/* rxpolarity_value */
#define BCMN_CORE_PCI_SERDES_RX_TIMER1		2	/* Rx Timer1 */
#define BCMN_CORE_PCI_SERDES_RX_CDR		6	/* CDR */
#define BCMN_CORE_PCI_SERDES_RX_CDRBW		7	/* CDR BW */

/* SERDES PLL registers */
#define BCMN_CORE_PCI_SERDES_PLL_CTRL		1	/* PLL control reg */
#define BCMN_CORE_PCI_PLL_CTRL_FREQDET_EN	0x4000	/* bit 14 is FREQDET on */

/* PCIcore specific boardflags */
#define BCMN_CORE_PCI_BFL_NOPCI			0x00000400 /* Board leaves PCI floating */

/* PCIE Config space accessing MACROS */
#define BCMN_CORE_PCI_CFG_BUS_SHIFT		24	/* Bus shift */
#define BCMN_CORE_PCI_CFG_SLOT_SHIFT		19	/* Slot/Device shift */
#define BCMN_CORE_PCI_CFG_FUN_SHIFT		16	/* Function shift */
#define BCMN_CORE_PCI_CFG_OFF_SHIFT		0	/* Register shift */

#define BCMN_CORE_PCI_CFG_BUS_MASK		0xff	/* Bus mask */
#define BCMN_CORE_PCI_CFG_SLOT_MASK		0x1f	/* Slot/Device mask */
#define BCMN_CORE_PCI_CFG_FUN_MASK		7	/* Function mask */
#define BCMN_CORE_PCI_CFG_OFF_MASK		0xfff	/* Register mask */

#define BCMN_CORE_PCI_CFG_DEVCTRL		0xd8

#define BCMN_CORE_PCI_

/* MDIO devices (SERDES modules) */
#define BCMN_CORE_PCI_MDIO_IEEE0		0x000
#define BCMN_CORE_PCI_MDIO_IEEE1		0x001
#define BCMN_CORE_PCI_MDIO_BLK0			0x800
#define BCMN_CORE_PCI_MDIO_BLK1			0x801
#define  BCMN_CORE_PCI_MDIO_BLK1_MGMT0		0x16
#define  BCMN_CORE_PCI_MDIO_BLK1_MGMT1		0x17
#define  BCMN_CORE_PCI_MDIO_BLK1_MGMT2		0x18
#define  BCMN_CORE_PCI_MDIO_BLK1_MGMT3		0x19
#define  BCMN_CORE_PCI_MDIO_BLK1_MGMT4		0x1A
#define BCMN_CORE_PCI_MDIO_BLK2			0x802
#define BCMN_CORE_PCI_MDIO_BLK3			0x803
#define BCMN_CORE_PCI_MDIO_BLK4			0x804
#define BCMN_CORE_PCI_MDIO_TXPLL		0x808	/* TXPLL register block idx */
#define BCMN_CORE_PCI_MDIO_TXCTRL0		0x820
#define BCMN_CORE_PCI_MDIO_SERDESID		0x831
#define BCMN_CORE_PCI_MDIO_RXCTRL0		0x840

/* PCIE Root Capability Register bits (Host mode only) */
#define BCMN_CORE_PCI_RC_RRS_VISIBILITY		0x0001

struct bcmn_drv_pci;
struct bcmn_bus;

#ifdef CONFIG_BCMN_DRIVER_PCI_HOSTMODE
struct bcmn_drv_pci_host {
	struct bcmn_drv_pci *pdev;

	u32 host_cfg_addr;
	spinlock_t cfgspace_lock;

	struct pci_controller pci_controller;
	struct pci_ops pci_ops;
	struct resource mem_resource;
	struct resource io_resource;
};
#endif

struct bcmn_drv_pci {
	struct bcmn_device *core;
	u8 early_setup_done:1;
	u8 setup_done:1;
	u8 hostmode:1;

#ifdef CONFIG_BCMN_DRIVER_PCI_HOSTMODE
	struct bcmn_drv_pci_host *host_controller;
#endif
};

/* Register access */
#define pcicore_read16(pc, offset)		bcmn_read16((pc)->core, offset)
#define pcicore_read32(pc, offset)		bcmn_read32((pc)->core, offset)
#define pcicore_write16(pc, offset, val)	bcmn_write16((pc)->core, offset, val)
#define pcicore_write32(pc, offset, val)	bcmn_write32((pc)->core, offset, val)

#ifdef CONFIG_BCMN_DRIVER_PCI
extern void bcmn_core_pci_power_save(struct bcmn_bus *bus, bool up);
#else
static inline void bcmn_core_pci_power_save(struct bcmn_bus *bus, bool up)
{
}
#endif

#ifdef CONFIG_BCMN_DRIVER_PCI_HOSTMODE
extern int bcmn_core_pci_pcibios_map_irq(const struct pci_dev *dev);
extern int bcmn_core_pci_plat_dev_init(struct pci_dev *dev);
#else
static inline int bcmn_core_pci_pcibios_map_irq(const struct pci_dev *dev)
{
	return -ENOTSUPP;
}
static inline int bcmn_core_pci_plat_dev_init(struct pci_dev *dev)
{
	return -ENOTSUPP;
}
#endif

#endif /* LINUX_BCMN_DRIVER_PCI_H_ */
