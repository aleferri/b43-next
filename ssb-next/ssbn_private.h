/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_SSBN_PRIVATE_H_
#define LINUX_SSBN_PRIVATE_H_

#define PFX		"ssb-next: "
/* compat.h may have brought <linux/printk.h>, and its default, first */
#undef pr_fmt
#define pr_fmt(fmt)	PFX fmt

#include <ssb-next/ssbn.h>
#include <linux/types.h>
#include <linux/bcm47xx_wdt.h>


/* pci.c */
#ifdef CONFIG_SSBN_PCIHOST
extern int ssbn_pci_switch_core(struct ssbn_bus *bus,
			       struct ssbn_device *dev);
extern int ssbn_pci_switch_coreidx(struct ssbn_bus *bus,
				  u8 coreidx);
extern int ssbn_pci_xtal(struct ssbn_bus *bus, u32 what,
			int turn_on);
extern int ssbn_pci_get_invariants(struct ssbn_bus *bus,
				  struct ssbn_init_invariants *iv);
extern void ssbn_pci_exit(struct ssbn_bus *bus);
extern int ssbn_pci_init(struct ssbn_bus *bus);
extern const struct ssbn_bus_ops ssbn_pci_ops;

#else /* CONFIG_SSBN_PCIHOST */

static inline int ssbn_pci_switch_core(struct ssbn_bus *bus,
				      struct ssbn_device *dev)
{
	return 0;
}
static inline int ssbn_pci_switch_coreidx(struct ssbn_bus *bus,
					 u8 coreidx)
{
	return 0;
}
static inline int ssbn_pci_xtal(struct ssbn_bus *bus, u32 what,
			       int turn_on)
{
	return 0;
}
static inline void ssbn_pci_exit(struct ssbn_bus *bus)
{
}
static inline int ssbn_pci_init(struct ssbn_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSBN_PCIHOST */


/* pcmcia.c */
#ifdef CONFIG_SSBN_PCMCIAHOST
extern int ssbn_pcmcia_switch_coreidx(struct ssbn_bus *bus,
				     u8 coreidx);
extern int ssbn_pcmcia_switch_segment(struct ssbn_bus *bus,
				     u8 seg);
extern int ssbn_pcmcia_get_invariants(struct ssbn_bus *bus,
				     struct ssbn_init_invariants *iv);
extern int ssbn_pcmcia_hardware_setup(struct ssbn_bus *bus);
extern void ssbn_pcmcia_exit(struct ssbn_bus *bus);
extern int ssbn_pcmcia_init(struct ssbn_bus *bus);
extern int ssbn_host_pcmcia_init(void);
extern void ssbn_host_pcmcia_exit(void);
extern const struct ssbn_bus_ops ssbn_pcmcia_ops;
#else /* CONFIG_SSBN_PCMCIAHOST */
static inline int ssbn_pcmcia_switch_coreidx(struct ssbn_bus *bus,
					    u8 coreidx)
{
	return 0;
}
static inline int ssbn_pcmcia_switch_segment(struct ssbn_bus *bus,
					    u8 seg)
{
	return 0;
}
static inline int ssbn_pcmcia_hardware_setup(struct ssbn_bus *bus)
{
	return 0;
}
static inline void ssbn_pcmcia_exit(struct ssbn_bus *bus)
{
}
static inline int ssbn_pcmcia_init(struct ssbn_bus *bus)
{
	return 0;
}
static inline int ssbn_host_pcmcia_init(void)
{
	return 0;
}
static inline void ssbn_host_pcmcia_exit(void)
{
}
#endif /* CONFIG_SSBN_PCMCIAHOST */

/* sdio.c */
#ifdef CONFIG_SSBN_SDIOHOST
extern int ssbn_sdio_get_invariants(struct ssbn_bus *bus,
				     struct ssbn_init_invariants *iv);

extern u32 ssbn_sdio_scan_read32(struct ssbn_bus *bus, u16 offset);
extern int ssbn_sdio_scan_switch_coreidx(struct ssbn_bus *bus, u8 coreidx);
extern void ssbn_sdio_exit(struct ssbn_bus *bus);
extern int ssbn_sdio_init(struct ssbn_bus *bus);

extern const struct ssbn_bus_ops ssbn_sdio_ops;
#else /* CONFIG_SSBN_SDIOHOST */
static inline u32 ssbn_sdio_scan_read32(struct ssbn_bus *bus, u16 offset)
{
	return 0;
}
static inline int ssbn_sdio_scan_switch_coreidx(struct ssbn_bus *bus, u8 coreidx)
{
	return 0;
}
static inline void ssbn_sdio_exit(struct ssbn_bus *bus)
{
}
static inline int ssbn_sdio_init(struct ssbn_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSBN_SDIOHOST */

/**************************************************
 * host_soc.c
 **************************************************/

#ifdef CONFIG_SSBN_HOST_SOC
extern const struct ssbn_bus_ops ssbn_host_soc_ops;

extern int ssbn_host_soc_get_invariants(struct ssbn_bus *bus,
				       struct ssbn_init_invariants *iv);
#endif

/* scan.c */
extern const char *ssbn_core_name(u16 coreid);
extern int ssbn_bus_scan(struct ssbn_bus *bus,
			unsigned long baseaddr);
extern void ssbn_iounmap(struct ssbn_bus *ssb);


/* sprom.c */
extern
ssize_t ssbn_attr_sprom_show(struct ssbn_bus *bus, char *buf,
			    int (*sprom_read)(struct ssbn_bus *bus, u16 *sprom));
extern
ssize_t ssbn_attr_sprom_store(struct ssbn_bus *bus,
			     const char *buf, size_t count,
			     int (*sprom_check_crc)(const u16 *sprom, size_t size),
			     int (*sprom_write)(struct ssbn_bus *bus, const u16 *sprom));
extern int ssbn_fill_sprom_with_fallback(struct ssbn_bus *bus,
					struct ssbn_sprom *out);


/* core.c */
extern u32 ssbn_calc_clock_rate(u32 plltype, u32 n, u32 m);
extern struct ssbn_bus *ssbn_pci_dev_to_bus(struct pci_dev *pdev);
int ssbn_for_each_bus_call(unsigned long data,
			  int (*func)(struct ssbn_bus *bus, unsigned long data));
extern struct ssbn_bus *ssbn_pcmcia_dev_to_bus(struct pcmcia_device *pdev);

struct ssbn_freeze_context {
	/* Pointer to the bus */
	struct ssbn_bus *bus;
	/* Boolean list to indicate whether a device is frozen on this bus. */
	bool device_frozen[SSBN_MAX_NR_CORES];
};
extern int ssbn_devices_freeze(struct ssbn_bus *bus, struct ssbn_freeze_context *ctx);
extern int ssbn_devices_thaw(struct ssbn_freeze_context *ctx);



/* b43_pci_bridge.c */
#ifdef CONFIG_SSBN_B43_PCI_BRIDGE
extern int __init b43_pci_ssb_bridge_init(void);
extern void b43_pci_ssb_bridge_exit(void);
#else /* CONFIG_SSBN_B43_PCI_BRIDGE */
static inline int b43_pci_ssb_bridge_init(void)
{
	return 0;
}
static inline void b43_pci_ssb_bridge_exit(void)
{
}
#endif /* CONFIG_SSBN_B43_PCI_BRIDGE */

/* driver_chipcommon_pmu.c */
extern u32 ssbn_pmu_get_cpu_clock(struct ssbn_chipcommon *cc);
extern u32 ssbn_pmu_get_controlclock(struct ssbn_chipcommon *cc);
extern u32 ssbn_pmu_get_alp_clock(struct ssbn_chipcommon *cc);

extern u32 ssbn_chipco_watchdog_timer_set_wdt(struct bcm47xx_wdt *wdt,
					     u32 ticks);
extern u32 ssbn_chipco_watchdog_timer_set_ms(struct bcm47xx_wdt *wdt, u32 ms);

/* driver_chipcommon_sflash.c */
#ifdef CONFIG_SSBN_SFLASH
int ssbn_sflash_init(struct ssbn_chipcommon *cc);
#else
static inline int ssbn_sflash_init(struct ssbn_chipcommon *cc)
{
	pr_err("Serial flash not supported\n");
	return 0;
}
#endif /* CONFIG_SSBN_SFLASH */

#ifdef CONFIG_SSBN_DRIVER_MIPS
extern struct platform_device ssbn_pflash_dev;
#endif

#ifdef CONFIG_SSBN_SFLASH
extern struct platform_device ssbn_sflash_dev;
#endif

#ifdef CONFIG_SSBN_DRIVER_EXTIF
extern u32 ssbn_extif_watchdog_timer_set_wdt(struct bcm47xx_wdt *wdt, u32 ticks);
extern u32 ssbn_extif_watchdog_timer_set_ms(struct bcm47xx_wdt *wdt, u32 ms);
#else
static inline u32 ssbn_extif_watchdog_timer_set_wdt(struct bcm47xx_wdt *wdt,
						   u32 ticks)
{
	return 0;
}
static inline u32 ssbn_extif_watchdog_timer_set_ms(struct bcm47xx_wdt *wdt,
						  u32 ms)
{
	return 0;
}
#endif

#ifdef CONFIG_SSBN_EMBEDDED
extern int ssbn_watchdog_register(struct ssbn_bus *bus);
#else /* CONFIG_SSBN_EMBEDDED */
static inline int ssbn_watchdog_register(struct ssbn_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSBN_EMBEDDED */

#ifdef CONFIG_SSBN_DRIVER_EXTIF
extern void ssbn_extif_init(struct ssbn_extif *extif);
#else
static inline void ssbn_extif_init(struct ssbn_extif *extif)
{
}
#endif

#ifdef CONFIG_SSBN_DRIVER_GPIO
extern int ssbn_gpio_init(struct ssbn_bus *bus);
extern int ssbn_gpio_unregister(struct ssbn_bus *bus);
#else /* CONFIG_SSBN_DRIVER_GPIO */
static inline int ssbn_gpio_init(struct ssbn_bus *bus)
{
	return -ENOTSUPP;
}
static inline int ssbn_gpio_unregister(struct ssbn_bus *bus)
{
	return 0;
}
#endif /* CONFIG_SSBN_DRIVER_GPIO */

#endif /* LINUX_SSBN_PRIVATE_H_ */
