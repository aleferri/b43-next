/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_SSBN_PCICORE_H_
#define LINUX_SSBN_PCICORE_H_

#include <linux/types.h>

struct pci_dev;


#ifdef CONFIG_SSBN_DRIVER_PCICORE

/* PCI core registers. */
#define SSBN_PCICORE_CTL			0x0000	/* PCI Control */
#define  SSBN_PCICORE_CTL_RST_OE		0x00000001 /* PCI_RESET Output Enable */
#define  SSBN_PCICORE_CTL_RST		0x00000002 /* PCI_RESET driven out to pin */
#define  SSBN_PCICORE_CTL_CLK_OE		0x00000004 /* Clock gate Output Enable */
#define  SSBN_PCICORE_CTL_CLK		0x00000008 /* Gate for clock driven out to pin */
#define SSBN_PCICORE_ARBCTL		0x0010	/* PCI Arbiter Control */
#define  SSBN_PCICORE_ARBCTL_INTERN	0x00000001 /* Use internal arbiter */
#define  SSBN_PCICORE_ARBCTL_EXTERN	0x00000002 /* Use external arbiter */
#define  SSBN_PCICORE_ARBCTL_PARKID	0x00000006 /* Mask, selects which agent is parked on an idle bus */
#define   SSBN_PCICORE_ARBCTL_PARKID_LAST	0x00000000 /* Last requestor */
#define   SSBN_PCICORE_ARBCTL_PARKID_4710	0x00000002 /* 4710 */
#define   SSBN_PCICORE_ARBCTL_PARKID_EXT0	0x00000004 /* External requestor 0 */
#define   SSBN_PCICORE_ARBCTL_PARKID_EXT1	0x00000006 /* External requestor 1 */
#define SSBN_PCICORE_ISTAT		0x0020	/* Interrupt status */
#define  SSBN_PCICORE_ISTAT_INTA		0x00000001 /* PCI INTA# */
#define  SSBN_PCICORE_ISTAT_INTB		0x00000002 /* PCI INTB# */
#define  SSBN_PCICORE_ISTAT_SERR		0x00000004 /* PCI SERR# (write to clear) */
#define  SSBN_PCICORE_ISTAT_PERR		0x00000008 /* PCI PERR# (write to clear) */
#define  SSBN_PCICORE_ISTAT_PME		0x00000010 /* PCI PME# */
#define SSBN_PCICORE_IMASK		0x0024	/* Interrupt mask */
#define  SSBN_PCICORE_IMASK_INTA		0x00000001 /* PCI INTA# */
#define  SSBN_PCICORE_IMASK_INTB		0x00000002 /* PCI INTB# */
#define  SSBN_PCICORE_IMASK_SERR		0x00000004 /* PCI SERR# */
#define  SSBN_PCICORE_IMASK_PERR		0x00000008 /* PCI PERR# */
#define  SSBN_PCICORE_IMASK_PME		0x00000010 /* PCI PME# */
#define SSBN_PCICORE_MBOX		0x0028	/* Backplane to PCI Mailbox */
#define  SSBN_PCICORE_MBOX_F0_0		0x00000100 /* PCI function 0, INT 0 */
#define  SSBN_PCICORE_MBOX_F0_1		0x00000200 /* PCI function 0, INT 1 */
#define  SSBN_PCICORE_MBOX_F1_0		0x00000400 /* PCI function 1, INT 0 */
#define  SSBN_PCICORE_MBOX_F1_1		0x00000800 /* PCI function 1, INT 1 */
#define  SSBN_PCICORE_MBOX_F2_0		0x00001000 /* PCI function 2, INT 0 */
#define  SSBN_PCICORE_MBOX_F2_1		0x00002000 /* PCI function 2, INT 1 */
#define  SSBN_PCICORE_MBOX_F3_0		0x00004000 /* PCI function 3, INT 0 */
#define  SSBN_PCICORE_MBOX_F3_1		0x00008000 /* PCI function 3, INT 1 */
#define SSBN_PCICORE_BCAST_ADDR		0x0050	/* Backplane Broadcast Address */
#define  SSBN_PCICORE_BCAST_ADDR_MASK	0x000000FF
#define SSBN_PCICORE_BCAST_DATA		0x0054	/* Backplane Broadcast Data */
#define SSBN_PCICORE_GPIO_IN		0x0060	/* rev >= 2 only */
#define SSBN_PCICORE_GPIO_OUT		0x0064	/* rev >= 2 only */
#define SSBN_PCICORE_GPIO_ENABLE		0x0068	/* rev >= 2 only */
#define SSBN_PCICORE_GPIO_CTL		0x006C	/* rev >= 2 only */
#define SSBN_PCICORE_SBTOPCI0		0x0100	/* Backplane to PCI translation 0 (sbtopci0) */
#define  SSBN_PCICORE_SBTOPCI0_MASK	0xFC000000
#define SSBN_PCICORE_SBTOPCI1		0x0104	/* Backplane to PCI translation 1 (sbtopci1) */
#define  SSBN_PCICORE_SBTOPCI1_MASK	0xFC000000
#define SSBN_PCICORE_SBTOPCI2		0x0108	/* Backplane to PCI translation 2 (sbtopci2) */
#define  SSBN_PCICORE_SBTOPCI2_MASK	0xC0000000
#define SSBN_PCICORE_PCICFG0		0x0400	/* PCI config space 0 (rev >= 8) */
#define SSBN_PCICORE_PCICFG1		0x0500	/* PCI config space 1 (rev >= 8) */
#define SSBN_PCICORE_PCICFG2		0x0600	/* PCI config space 2 (rev >= 8) */
#define SSBN_PCICORE_PCICFG3		0x0700	/* PCI config space 3 (rev >= 8) */
#define SSBN_PCICORE_SPROM(wordoffset)	(0x0800 + ((wordoffset) * 2)) /* SPROM shadow area (72 bytes) */

/* SBtoPCIx */
#define SSBN_PCICORE_SBTOPCI_MEM		0x00000000
#define SSBN_PCICORE_SBTOPCI_IO		0x00000001
#define SSBN_PCICORE_SBTOPCI_CFG0	0x00000002
#define SSBN_PCICORE_SBTOPCI_CFG1	0x00000003
#define SSBN_PCICORE_SBTOPCI_PREF	0x00000004 /* Prefetch enable */
#define SSBN_PCICORE_SBTOPCI_BURST	0x00000008 /* Burst enable */
#define SSBN_PCICORE_SBTOPCI_MRM		0x00000020 /* Memory Read Multiple */
#define SSBN_PCICORE_SBTOPCI_RC		0x00000030 /* Read Command mask (rev >= 11) */
#define  SSBN_PCICORE_SBTOPCI_RC_READ	0x00000000 /* Memory read */
#define  SSBN_PCICORE_SBTOPCI_RC_READL	0x00000010 /* Memory read line */
#define  SSBN_PCICORE_SBTOPCI_RC_READM	0x00000020 /* Memory read multiple */


/* PCIcore specific boardflags */
#define SSBN_PCICORE_BFL_NOPCI		0x00000400 /* Board leaves PCI floating */


struct ssbn_pcicore {
	struct ssbn_device *dev;
	u8 setup_done:1;
	u8 hostmode:1;
	u8 cardbusmode:1;
};

extern void ssbn_pcicore_init(struct ssbn_pcicore *pc);

/* Enable IRQ routing for a specific device */
extern int ssbn_pcicore_dev_irqvecs_enable(struct ssbn_pcicore *pc,
					  struct ssbn_device *dev);

int ssbn_pcicore_plat_dev_init(struct pci_dev *d);
int ssbn_pcicore_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);


#else /* CONFIG_SSBN_DRIVER_PCICORE */


struct ssbn_pcicore {
};

static inline
void ssbn_pcicore_init(struct ssbn_pcicore *pc)
{
}

static inline
int ssbn_pcicore_dev_irqvecs_enable(struct ssbn_pcicore *pc,
				   struct ssbn_device *dev)
{
	return 0;
}

static inline
int ssbn_pcicore_plat_dev_init(struct pci_dev *d)
{
	return -ENODEV;
}
static inline
int ssbn_pcicore_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return -ENODEV;
}

#endif /* CONFIG_SSBN_DRIVER_PCICORE */
#endif /* LINUX_SSBN_PCICORE_H_ */
