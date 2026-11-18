/* SPDX-License-Identifier: GPL-2.0 */
/*
 * B43_NEXT_KERNEL_BUS: b43-next on the kernel's ssb. The driver's ssb-next
 * names map to the kernel's; see kbus/bcma-next/bcmn.h.
 */
#ifndef B43_NEXT_KBUS_SSBN_H_
#define B43_NEXT_KBUS_SSBN_H_

#include <linux/ssb/ssb.h>

#ifndef SSB_BFL2_HI_ANAPACTRL_2G
#define SSB_BFL2_HI_ANAPACTRL_2G	0x0010	/* ssb-next's ssbn_regs.h */
#endif
#define SSBN_BFL2_HI_ANAPACTRL_2G	SSB_BFL2_HI_ANAPACTRL_2G

#define ssbn_block_read ssb_block_read
#define ssbn_block_write ssb_block_write
#define SSBN_BOARD_BCM4309G SSB_BOARD_BCM4309G
#define SSBN_BOARD_BU4306 SSB_BOARD_BU4306
#define SSBN_BOARD_BU4312 SSB_BOARD_BU4312
#define SSBN_BOARD_CB2_4321 SSB_BOARD_CB2_4321
#define SSBN_BOARDVENDOR_BCM SSB_BOARDVENDOR_BCM
#define SSBN_BOARDVENDOR_DELL SSB_BOARDVENDOR_DELL
#define ssbn_bus ssb_bus
#define ssbn_bus_may_powerdown ssb_bus_may_powerdown
#define ssbn_bus_powerup ssb_bus_powerup
#define ssbn_bus_sdiobus_register ssb_bus_sdiobus_register
#define SSBN_BUSTYPE_PCI SSB_BUSTYPE_PCI
#define SSBN_BUSTYPE_PCMCIA SSB_BUSTYPE_PCMCIA
#define SSBN_BUSTYPE_SDIO SSB_BUSTYPE_SDIO
#define ssbn_bus_unregister ssb_bus_unregister
#define ssbn_cc ssb_cc
#define SSBN_CHIPCO_CAP_PMU SSB_CHIPCO_CAP_PMU
#define SSBN_CHIPCO_CHIPCTL SSB_CHIPCO_CHIPCTL
#define SSBN_CHIPCO_CHIPCTL_ADDR SSB_CHIPCO_CHIPCTL_ADDR
#define SSBN_CHIPCO_CHIPCTL_DATA SSB_CHIPCO_CHIPCTL_DATA
#define ssbn_chipco_gpio_control ssb_chipco_gpio_control
#define ssbn_chipcommon ssb_chipcommon
#define SSBN_CHIPPACK_BCM4712S SSB_CHIPPACK_BCM4712S
#define ssbn_commit_settings ssb_commit_settings
#define ssbn_dev ssb_dev
#define SSBN_DEV_80211 SSB_DEV_80211
#define ssbn_device ssb_device
#define SSBN_DEVICE SSB_DEVICE
#define ssbn_device_disable ssb_device_disable
#define ssbn_device_enable ssb_device_enable
#define ssbn_device_id ssb_device_id
#define ssbn_device_is_enabled ssb_device_is_enabled
#define ssbn_dma_translation ssb_dma_translation
#define SSBN_DMA_TRANSLATION_MASK SSB_DMA_TRANSLATION_MASK
#define SSBN_DMA_TRANSLATION_SHIFT SSB_DMA_TRANSLATION_SHIFT
#define ssbn_driver ssb_driver
#define ssbn_driver_register ssb_driver_register
#define ssbn_driver_unregister ssb_driver_unregister
#define ssbn_get_devtypedata ssb_get_devtypedata
#define ssbn_get_drvdata ssb_get_drvdata
#define SSBN_IMCFGLO SSB_IMCFGLO
#define SSBN_IMCFGLO_REQTO SSB_IMCFGLO_REQTO
#define SSBN_IMCFGLO_SERTO SSB_IMCFGLO_SERTO
#define ssbn_pcicore_dev_irqvecs_enable ssb_pcicore_dev_irqvecs_enable
#define ssbn_pmu_set_ldo_paref ssb_pmu_set_ldo_paref
#define ssbn_pmu_set_ldo_voltage ssb_pmu_set_ldo_voltage
#define ssbn_pmu_spuravoid_pllupdate ssb_pmu_spuravoid_pllupdate
#define SSBN_QUIRK_SDIO_READ_AFTER_WRITE32 SSB_QUIRK_SDIO_READ_AFTER_WRITE32
#define ssbn_read16 ssb_read16
#define ssbn_read32 ssb_read32
#define ssbn_set_devtypedata ssb_set_devtypedata
#define ssbn_set_drvdata ssb_set_drvdata
#define ssbn_sprom ssb_sprom
#define SSBN_SPROM1CCODE_JAPAN SSB_SPROM1CCODE_JAPAN
#define ssbn_sprom_core_pwr_info ssb_sprom_core_pwr_info
#define ssbn_sprom_rxgains ssb_sprom_rxgains
#define SSBN_TMSHIGH SSB_TMSHIGH
#define SSBN_TMSHIGH_DMA64 SSB_TMSHIGH_DMA64
#define SSBN_TMSLOW SSB_TMSLOW
#define SSBN_TMSLOW_FGC SSB_TMSLOW_FGC
#define SSBN_VENDOR_BROADCOM SSB_VENDOR_BROADCOM
#define ssbn_write16 ssb_write16
#define ssbn_write32 ssb_write32

#endif
