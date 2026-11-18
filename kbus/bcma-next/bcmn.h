/* SPDX-License-Identifier: GPL-2.0 */
/*
 * B43_NEXT_KERNEL_BUS: b43-next on the kernel's bcma. The driver's bcma-next
 * names map to the kernel's; the kernel's bcma carries what bcma-next adds
 * (OpenWrt patches), but for the definitions below.
 */
#ifndef B43_NEXT_KBUS_BCMN_H_
#define B43_NEXT_KBUS_BCMN_H_

#include <linux/bcma/bcma.h>

#ifndef BCMA_CHIPCTL_4331_EXTPA_ANA_EN
#define BCMA_CHIPCTL_4331_EXTPA_ANA_EN	BIT(24)	/* bcma-next's bcmn_driver_chipcommon.h */
#endif
#define BCMN_CHIPCTL_4331_EXTPA_ANA_EN	BCMA_CHIPCTL_4331_EXTPA_ANA_EN

#define BCMN_ANY_CLASS BCMA_ANY_CLASS
#define bcmn_aread32 bcma_aread32
#define bcmn_awrite32 bcma_awrite32
#define bcmn_block_read bcma_block_read
#define bcmn_block_write bcma_block_write
#define BCMN_BOARD_TYPE_BCM943224M93 BCMA_BOARD_TYPE_BCM943224M93
#define bcmn_bus bcma_bus
#define bcmn_cc bcma_cc
#define BCMN_CC_CHIPCTL BCMA_CC_CHIPCTL
#define BCMN_CC_GPIOTOUTM BCMA_CC_GPIOTOUTM
#define bcmn_cc_mask32 bcma_cc_mask32
#define bcmn_cc_maskset32 bcma_cc_maskset32
#define BCMN_CC_PMU_CHIPCTL_ADDR BCMA_CC_PMU_CHIPCTL_ADDR
#define BCMN_CC_PMU_CHIPCTL_DATA BCMA_CC_PMU_CHIPCTL_DATA
#define BCMN_CC_PMU_CTL BCMA_CC_PMU_CTL
#define BCMN_CC_PMU_PLL_CTL3 BCMA_CC_PMU_PLL_CTL3
#define bcmn_cc_read32 bcma_cc_read32
#define bcmn_cc_set32 bcma_cc_set32
#define bcmn_cc_write32 bcma_cc_write32
#define bcmn_chipco_chipctl_maskset bcma_chipco_chipctl_maskset
#define bcmn_chipco_gpio_control bcma_chipco_gpio_control
#define bcmn_chipco_gpio_out bcma_chipco_gpio_out
#define bcmn_chipco_gpio_outen bcma_chipco_gpio_outen
#define bcmn_chipco_pll_maskset bcma_chipco_pll_maskset
#define bcmn_chipco_pll_read bcma_chipco_pll_read
#define bcmn_chipco_pll_write bcma_chipco_pll_write
#define bcmn_chipco_regctl_maskset bcma_chipco_regctl_maskset
#define BCMN_CHIPCTL_4331_EXT_LNA BCMA_CHIPCTL_4331_EXT_LNA
#define BCMN_CHIPCTL_4331_EXTPA_EN BCMA_CHIPCTL_4331_EXTPA_EN
#define BCMN_CHIPCTL_4331_EXTPA_EN2 BCMA_CHIPCTL_4331_EXTPA_EN2
#define BCMN_CHIP_ID_BCM43131 BCMA_CHIP_ID_BCM43131
#define BCMN_CHIP_ID_BCM43217 BCMA_CHIP_ID_BCM43217
#define BCMN_CHIP_ID_BCM43222 BCMA_CHIP_ID_BCM43222
#define BCMN_CHIP_ID_BCM43224 BCMA_CHIP_ID_BCM43224
#define BCMN_CHIP_ID_BCM43225 BCMA_CHIP_ID_BCM43225
#define BCMN_CHIP_ID_BCM43227 BCMA_CHIP_ID_BCM43227
#define BCMN_CHIP_ID_BCM43228 BCMA_CHIP_ID_BCM43228
#define BCMN_CHIP_ID_BCM4331 BCMA_CHIP_ID_BCM4331
#define BCMN_CHIP_ID_BCM43421 BCMA_CHIP_ID_BCM43421
#define BCMN_CHIP_ID_BCM4352 BCMA_CHIP_ID_BCM4352
#define BCMN_CHIP_ID_BCM4360 BCMA_CHIP_ID_BCM4360
#define BCMN_CHIP_ID_BCM4716 BCMA_CHIP_ID_BCM4716
#define BCMN_CHIP_ID_BCM47162 BCMA_CHIP_ID_BCM47162
#define BCMN_CLKCTLST BCMA_CLKCTLST
#define BCMN_CLKCTLST_EXTRESREQ BCMA_CLKCTLST_EXTRESREQ
#define BCMN_CLKCTLST_EXTRESST BCMA_CLKCTLST_EXTRESST
#define BCMN_CLKMODE_DYNAMIC BCMA_CLKMODE_DYNAMIC
#define BCMN_CLKMODE_FAST BCMA_CLKMODE_FAST
#define BCMN_CORE BCMA_CORE
#define BCMN_CORE_80211 BCMA_CORE_80211
#define bcmn_core_disable bcma_core_disable
#define bcmn_core_dma_translation bcma_core_dma_translation
#define bcmn_core_enable bcma_core_enable
#define bcmn_core_is_enabled bcma_core_is_enabled
#define bcmn_core_pll_ctl bcma_core_pll_ctl
#define bcmn_core_set_clockmode bcma_core_set_clockmode
#define bcmn_device bcma_device
#define bcmn_device_id bcma_device_id
#define bcmn_driver bcma_driver
#define bcmn_driver_register bcma_driver_register
#define bcmn_driver_unregister bcma_driver_unregister
#define bcmn_drv_cc bcma_drv_cc
#define bcmn_err_wireless_exit bcma_err_wireless_exit
#define bcmn_get_drvdata bcma_get_drvdata
#define bcmn_host_pci_down bcma_host_pci_down
#define bcmn_host_pci_irq_ctl bcma_host_pci_irq_ctl
#define bcmn_host_pci_up bcma_host_pci_up
#define BCMN_HOSTTYPE_PCI BCMA_HOSTTYPE_PCI
#define BCMN_IOCTL BCMA_IOCTL
#define BCMN_IOCTL_FGC BCMA_IOCTL_FGC
#define BCMN_IOST BCMA_IOST
#define BCMN_IOST_DMA64 BCMA_IOST_DMA64
#define BCMN_MANUF_BCM BCMA_MANUF_BCM
#define bcmn_out bcma_out
#define BCMN_PKG_ID_BCM43224_FAB_SMIC BCMA_PKG_ID_BCM43224_FAB_SMIC
#define bcmn_pmu_spuravoid_pllupdate bcma_pmu_spuravoid_pllupdate
#define bcmn_read16 bcma_read16
#define bcmn_read32 bcma_read32
#define bcmn_set_drvdata bcma_set_drvdata
#define bcmn_write16 bcma_write16
#define bcmn_write32 bcma_write32

#endif
