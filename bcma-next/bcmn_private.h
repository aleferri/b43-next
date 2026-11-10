/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMN_PRIVATE_H_
#define LINUX_BCMN_PRIVATE_H_

#ifndef pr_fmt
#define pr_fmt(fmt)		"bcma-next: " fmt
#endif

#include <bcma-next/bcmn.h>
#include <linux/delay.h>

#define bcmn_err(bus, fmt, ...) \
	dev_err((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcmn_warn(bus, fmt, ...) \
	dev_warn((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcmn_info(bus, fmt, ...) \
	dev_info((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)
#define bcmn_debug(bus, fmt, ...) \
	dev_dbg((bus)->dev, "bus%d: " fmt, (bus)->num, ##__VA_ARGS__)

struct bcmn_bus;

/* main.c */
bool bcmn_wait_value(struct bcmn_device *core, u16 reg, u32 mask, u32 value,
		     int timeout);
void bcmn_prepare_core(struct bcmn_bus *bus, struct bcmn_device *core);
void bcmn_init_bus(struct bcmn_bus *bus);
void bcmn_unregister_cores(struct bcmn_bus *bus);
int bcmn_bus_register(struct bcmn_bus *bus);
void bcmn_bus_unregister(struct bcmn_bus *bus);
int __init bcmn_bus_early_register(struct bcmn_bus *bus);
#ifdef CONFIG_PM
int bcmn_bus_suspend(struct bcmn_bus *bus);
int bcmn_bus_resume(struct bcmn_bus *bus);
#endif

/* scan.c */
void bcmn_detect_chip(struct bcmn_bus *bus);
int bcmn_bus_scan(struct bcmn_bus *bus);

/* sprom.c */
int bcmn_sprom_get(struct bcmn_bus *bus);

/* driver_chipcommon.c */
void bcmn_core_chipcommon_early_init(struct bcmn_drv_cc *cc);
void bcmn_core_chipcommon_init(struct bcmn_drv_cc *cc);
void bcmn_chipco_bcm4331_ext_pa_lines_ctl(struct bcmn_drv_cc *cc, bool enable);
#ifdef CONFIG_BCMN_DRIVER_MIPS
void bcmn_chipco_serial_init(struct bcmn_drv_cc *cc);
#endif /* CONFIG_BCMN_DRIVER_MIPS */

/* driver_chipcommon_b.c */
int bcmn_core_chipcommon_b_init(struct bcmn_drv_cc_b *ccb);
void bcmn_core_chipcommon_b_free(struct bcmn_drv_cc_b *ccb);

/* driver_chipcommon_pmu.c */
void bcmn_pmu_early_init(struct bcmn_drv_cc *cc);
void bcmn_pmu_init(struct bcmn_drv_cc *cc);
u32 bcmn_pmu_get_alp_clock(struct bcmn_drv_cc *cc);
u32 bcmn_pmu_get_cpu_clock(struct bcmn_drv_cc *cc);

/**************************************************
 * driver_chipcommon_sflash.c
 **************************************************/

#ifdef CONFIG_BCMN_PFLASH
extern struct platform_device bcmn_pflash_dev;
int bcmn_pflash_init(struct bcmn_drv_cc *cc);
#else
static inline int bcmn_pflash_init(struct bcmn_drv_cc *cc)
{
	bcmn_err(cc->core->bus, "Parallel flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMN_PFLASH */

#ifdef CONFIG_BCMN_SFLASH
/* driver_chipcommon_sflash.c */
int bcmn_sflash_init(struct bcmn_drv_cc *cc);
extern struct platform_device bcmn_sflash_dev;
#else
static inline int bcmn_sflash_init(struct bcmn_drv_cc *cc)
{
	bcmn_err(cc->core->bus, "Serial flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMN_SFLASH */

#ifdef CONFIG_BCMN_NFLASH
/* driver_chipcommon_nflash.c */
int bcmn_nflash_init(struct bcmn_drv_cc *cc);
extern struct platform_device bcmn_nflash_dev;
#else
static inline int bcmn_nflash_init(struct bcmn_drv_cc *cc)
{
	bcmn_err(cc->core->bus, "NAND flash not supported\n");
	return 0;
}
#endif /* CONFIG_BCMN_NFLASH */

#ifdef CONFIG_BCMN_HOST_PCI
/* host_pci.c */
extern int __init bcmn_host_pci_init(void);
extern void bcmn_host_pci_exit(void);
#endif /* CONFIG_BCMN_HOST_PCI */

/* host_soc.c */
#if defined(CONFIG_BCMN_HOST_SOC) && defined(CONFIG_OF)
extern int __init bcmn_host_soc_register_driver(void);
extern void bcmn_host_soc_unregister_driver(void);
#else
static inline int __init bcmn_host_soc_register_driver(void)
{
	return 0;
}
static inline void bcmn_host_soc_unregister_driver(void)
{
}
#endif /* CONFIG_BCMN_HOST_SOC && CONFIG_OF */

/* driver_pci.c */
#ifdef CONFIG_BCMN_DRIVER_PCI
u32 bcmn_pcie_read(struct bcmn_drv_pci *pc, u32 address);
void bcmn_core_pci_early_init(struct bcmn_drv_pci *pc);
void bcmn_core_pci_init(struct bcmn_drv_pci *pc);
void bcmn_core_pci_up(struct bcmn_drv_pci *pc);
void bcmn_core_pci_down(struct bcmn_drv_pci *pc);
#else
static inline void bcmn_core_pci_early_init(struct bcmn_drv_pci *pc)
{
	WARN_ON(pc->core->bus->hosttype == BCMN_HOSTTYPE_PCI);
}
static inline void bcmn_core_pci_init(struct bcmn_drv_pci *pc)
{
	/* Initialization is required for PCI hosted bus */
	WARN_ON(pc->core->bus->hosttype == BCMN_HOSTTYPE_PCI);
}
#endif

/* driver_pcie2.c */
#ifdef CONFIG_BCMN_DRIVER_PCI
void bcmn_core_pcie2_init(struct bcmn_drv_pcie2 *pcie2);
void bcmn_core_pcie2_up(struct bcmn_drv_pcie2 *pcie2);
#else
static inline void bcmn_core_pcie2_init(struct bcmn_drv_pcie2 *pcie2)
{
	/* Initialization is required for PCI hosted bus */
	WARN_ON(pcie2->core->bus->hosttype == BCMN_HOSTTYPE_PCI);
}
#endif

extern int bcmn_chipco_watchdog_register(struct bcmn_drv_cc *cc);

#ifdef CONFIG_BCMN_DRIVER_PCI_HOSTMODE
bool bcmn_core_pci_is_in_hostmode(struct bcmn_drv_pci *pc);
void bcmn_core_pci_hostmode_init(struct bcmn_drv_pci *pc);
#else
static inline bool bcmn_core_pci_is_in_hostmode(struct bcmn_drv_pci *pc)
{
	return false;
}
static inline void bcmn_core_pci_hostmode_init(struct bcmn_drv_pci *pc)
{
}
#endif /* CONFIG_BCMN_DRIVER_PCI_HOSTMODE */

/**************************************************
 * driver_mips.c
 **************************************************/

#ifdef CONFIG_BCMN_DRIVER_MIPS
unsigned int bcmn_core_mips_irq(struct bcmn_device *dev);
void bcmn_core_mips_early_init(struct bcmn_drv_mips *mcore);
void bcmn_core_mips_init(struct bcmn_drv_mips *mcore);
#else
static inline unsigned int bcmn_core_mips_irq(struct bcmn_device *dev)
{
	return 0;
}
static inline void bcmn_core_mips_early_init(struct bcmn_drv_mips *mcore)
{
}
static inline void bcmn_core_mips_init(struct bcmn_drv_mips *mcore)
{
}
#endif

/**************************************************
 * driver_gmac_cmn.c
 **************************************************/

#ifdef CONFIG_BCMN_DRIVER_GMAC_CMN
void bcmn_core_gmac_cmn_init(struct bcmn_drv_gmac_cmn *gc);
#else
static inline void bcmn_core_gmac_cmn_init(struct bcmn_drv_gmac_cmn *gc)
{
}
#endif

#ifdef CONFIG_BCMN_DRIVER_GPIO
/* driver_gpio.c */
int bcmn_gpio_init(struct bcmn_drv_cc *cc);
int bcmn_gpio_unregister(struct bcmn_drv_cc *cc);
#else
static inline int bcmn_gpio_init(struct bcmn_drv_cc *cc)
{
	return -ENOTSUPP;
}
static inline int bcmn_gpio_unregister(struct bcmn_drv_cc *cc)
{
	return 0;
}
#endif /* CONFIG_BCMN_DRIVER_GPIO */

#endif
