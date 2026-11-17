# SPDX-License-Identifier: GPL-2.0
#
# b43-next: the driver and its own copies of ssb and bcma, in one module.
# The options that the kernel's Kconfig gives b43, ssb and bcma are decided
# here, from the target kernel's .config and from make variables: each one
# is a make variable for the object lists and a -D for the sources, from the
# same list.

# Without the kernel's configuration (make clean) there is nothing to check.
ifneq ($(CONFIG_MODULES),)
ifeq ($(CONFIG_PCI),)
$(error b43-next: the kernel has no PCI support)
endif
ifeq ($(CONFIG_MAC80211),)
$(error b43-next: the kernel has no mac80211)
endif
ifeq ($(CONFIG_CORDIC),)
$(error b43-next: the kernel has no CORDIC library (CONFIG_CORDIC))
endif
endif

ssbn-opts := SPROM BLOCKIO PCIHOST B43_PCI_BRIDGE DRIVER_PCICORE
bcmn-opts := HOST_PCI DRIVER_PCI BLOCKIO
# B43_NEXT_KERNEL_BUS=y: the driver alone, on the kernel's bcma and ssb (kbus/
# maps the bcma-next and ssb-next names), for the buses the kernel has.
ifeq ($(B43_NEXT_KERNEL_BUS),y)
b43n-opts := PCI_AUTOSELECT KERNEL_BUS
ifneq ($(CONFIG_SSB),)
b43n-opts += SSB
endif
ifneq ($(CONFIG_BCMA),)
b43n-opts += BCMA
endif
else
b43n-opts := SSB BCMA PCI_AUTOSELECT
endif
b43n-opts += PHY_G PHY_N PHY_LP PHY_HT PHY_LCN40 PHY_AC
# The LP-PHY is on SSB only (as b43's B43_PHY_LP depends on B43_SSB).
ifeq ($(filter SSB,$(b43n-opts)),)
b43n-opts := $(filter-out PHY_LP,$(b43n-opts))
endif

ifneq ($(CONFIG_MMC),)
ssbn-opts += SDIOHOST
b43n-opts += SDIO
endif
ifneq ($(CONFIG_BMIPS_GENERIC),)
ifneq ($(CONFIG_OF),)
bcmn-opts += HOST_SOC_BCM6362
endif
endif
ifneq ($(CONFIG_MAC80211_LEDS),)
ifneq ($(CONFIG_LEDS_CLASS),)
b43n-opts += LEDS
endif
endif
ifneq ($(CONFIG_HW_RANDOM),)
b43n-opts += HWRNG
endif
ifeq ($(B43_NEXT_DEBUG),y)
b43n-opts += DEBUG
endif
ifeq ($(B43_NEXT_CALSTAT),y)
ifneq ($(B43_NEXT_DEBUG),y)
$(error b43-next: B43_NEXT_CALSTAT=y needs B43_NEXT_DEBUG=y)
endif
b43n-opts += CALSTAT
endif
# Radar channels in AP mode on the AC-PHY: tested on the RT-AC66U B1 only,
# not a certified radar detector.
ifeq ($(B43_NEXT_DFS),y)
ifeq ($(CONFIG_CFG80211_CERTIFICATION_ONUS),)
$(error b43-next: B43_NEXT_DFS=y needs a kernel with CFG80211_CERTIFICATION_ONUS)
endif
b43n-opts += DFS
endif

$(foreach o,$(ssbn-opts),$(eval CONFIG_SSBN_$(o) := y))
$(foreach o,$(bcmn-opts),$(eval CONFIG_BCMN_$(o) := y))
$(foreach o,$(b43n-opts),$(eval CONFIG_B43_NEXT_$(o) := y))

ifeq ($(B43_NEXT_KERNEL_BUS),y)
ccflags-y += -I$(src)/kbus
endif
ccflags-y += -I$(src)/include -include $(src)/compat/compat.h
ccflags-y += $(foreach o,$(ssbn-opts),-DCONFIG_SSBN_$(o)=1)
ccflags-y += $(foreach o,$(bcmn-opts),-DCONFIG_BCMN_$(o)=1)
ccflags-y += $(foreach o,$(b43n-opts),-DCONFIG_B43_NEXT_$(o)=1)

# compat.h comes before slab.h, so the kernel's typed allocators are looked
# for here. 6.18.33-6.18.38 have them with a mandatory GFP and without
# kzalloc_flex(), and compat.h can't redefine them.
b43n-slab := $(srctree)/include/linux/slab.h
ifneq ($(shell grep -sw 'define kzalloc_flex' $(b43n-slab)),)
ccflags-y += -DB43_NEXT_HAVE_KZALLOC_OBJ
else ifneq ($(shell grep -sw 'define kzalloc_obj' $(b43n-slab)),)
$(error b43-next: kzalloc_obj() without kzalloc_flex(): 6.18.33-6.18.38 are not supported)
endif

obj-m += b43-next.o

ifneq ($(B43_NEXT_KERNEL_BUS),y)
b43-next-y := $(addprefix ssb-next/, \
	main.o scan.o sprom.o driver_chipcommon.o driver_chipcommon_pmu.o)
b43-next-$(CONFIG_SSBN_PCIHOST) += ssb-next/pci.o ssb-next/pcihost_wrapper.o
b43-next-$(CONFIG_SSBN_SDIOHOST) += ssb-next/sdio.o
b43-next-$(CONFIG_SSBN_DRIVER_PCICORE) += ssb-next/driver_pcicore.o
b43-next-$(CONFIG_SSBN_B43_PCI_BRIDGE) += ssb-next/b43_pci_bridge.o

b43-next-y += $(addprefix bcma-next/, \
	main.o scan.o core.o sprom.o \
	driver_chipcommon.o driver_chipcommon_pmu.o driver_chipcommon_b.o)
b43-next-$(CONFIG_BCMN_DRIVER_PCI) += bcma-next/driver_pci.o bcma-next/driver_pcie2.o
b43-next-$(CONFIG_BCMN_HOST_PCI) += bcma-next/host_pci.o
b43-next-$(CONFIG_BCMN_HOST_SOC_BCM6362) += bcma-next/host_soc_bcm6362.o

b43-next-$(CONFIG_BCM47XX_NVRAM) += nvram/bcm47xx_sprom.o
endif

include $(src)/b43-next/files.mk
b43n-obj = $(addprefix b43-next/,$(1:.c=.o))

b43-next-y += $(call b43n-obj,$(b43n-core) $(b43n-phy))
b43-next-$(CONFIG_B43_NEXT_PHY_G) += $(call b43n-obj,$(b43n-phy-g))
b43-next-$(CONFIG_B43_NEXT_PHY_N) += $(call b43n-obj,$(b43n-phy-n))
b43-next-$(CONFIG_B43_NEXT_PHY_LP) += $(call b43n-obj,$(b43n-phy-lp))
b43-next-$(CONFIG_B43_NEXT_PHY_HT) += $(call b43n-obj,$(b43n-phy-ht))
b43-next-$(CONFIG_B43_NEXT_PHY_LCN) += $(call b43n-obj,$(b43n-phy-lcn))
b43-next-$(CONFIG_B43_NEXT_PHY_LCN40) += $(call b43n-obj,$(b43n-phy-lcn40))
b43-next-$(CONFIG_B43_NEXT_PHY_AC) += $(call b43n-obj,$(b43n-phy-ac))
b43-next-$(CONFIG_B43_NEXT_LEDS) += $(call b43n-obj,$(b43n-leds))
b43-next-$(CONFIG_B43_NEXT_SDIO) += $(call b43n-obj,$(b43n-sdio))
b43-next-$(CONFIG_B43_NEXT_DEBUG) += $(call b43n-obj,$(b43n-debugfs))
b43-next-$(CONFIG_B43_NEXT_CALSTAT) += $(call b43n-obj,$(b43n-calstat))
