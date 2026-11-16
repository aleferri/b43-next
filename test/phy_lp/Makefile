# SPDX-License-Identifier: GPL-2.0
#
# The LP-PHY files of b43-next/, built as they are against the kernel
# headers (as ../phy_n does, see its Makefile for KVER, K and KG), with an
# SSB bus that turns their accesses into mmio2ops ops.

include ../paths.mk
include $(REPO)/b43-next/files.mk

KVER    ?= $(shell ls /usr/src/ | grep -oE '^linux-headers-[0-9.]+-[0-9]+$$' | head -1 | sed 's/linux-headers-//')
K       ?= /usr/src/linux-headers-$(KVER)
KG      ?= /usr/src/linux-headers-$(KVER)-generic
B       := build

KINC := -I$(REPO)/include -I$(KG)/include -I$(K)/include -I$(K)/include/uapi \
        -I$(KG)/include/generated/uapi \
        -I$(K)/arch/x86/include -I$(K)/arch/x86/include/uapi \
        -I$(KG)/arch/x86/include/generated \
        -I$(KG)/arch/x86/include/generated/uapi

KFLAGS := -nostdinc -I$(REPO)/b43-next -I$(REPO) $(KINC) -D__KERNEL__ -DMODULE \
          -DKBUILD_MODNAME=\"b43\" \
          -include $(K)/include/linux/kconfig.h \
          -include $(K)/include/linux/compiler_types.h \
          -DCONFIG_B43_NEXT_SSB=1 -DCONFIG_B43_NEXT_PHY_LP=1 \
          -DCONFIG_B43_NEXT_CALSTAT=1 \
          -O2 -fno-strict-aliasing -fno-common -std=gnu11 \
          -Wall -Werror -Wno-pointer-sign -Wno-unused-function \
          -Wno-address-of-packed-member \
          -mno-red-zone -include $(REPO)/compat/compat.h

AUTOCONF := $(KG)/include/config/auto.conf
KFLAGS += $(shell sed -n 's/^CONFIG_CC_MS_EXTENSIONS=//p' $(AUTOCONF))
KFLAGS += $(if $(shell grep -s '^CONFIG_FUNCTION_TRACER=y' $(AUTOCONF)),-DCC_USING_FENTRY)

# As in Kbuild: the headers' own typed allocators, if they have kzalloc_flex().
KFLAGS += $(if $(shell grep -sw 'define kzalloc_flex' $(K)/include/linux/slab.h),-DB43_NEXT_HAVE_KZALLOC_OBJ)

UFLAGS := -O2 -g -Wall -Werror -std=gnu11

# flows.c includes phy_lp.c; the others are linked as they are.
DRIVER := $(filter-out phy_lp.c,$(b43n-phy-lp)) $(b43n-phy) $(b43n-calstat)

OBJS := $(patsubst %.c,$(B)/%.o,$(DRIVER)) $(B)/driver_chipcommon_pmu.o \
	$(B)/cordic.o $(B)/int_sqrt.o $(B)/flows.o $(B)/trace.o

all: lp-trace

$(B):
	mkdir -p $(B)

$(B)/%.o: $(REPO)/b43-next/%.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

# ssbn_pmu_set_ldo_voltage() and ssbn_pmu_set_ldo_paref(), on ChipCommon
$(B)/driver_chipcommon_pmu.o: $(REPO)/ssb-next/driver_chipcommon_pmu.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS) -I$(REPO)/ssb-next

# The kernel's CORDIC and int_sqrt, which the calibrations use
$(B)/cordic.o: $(K)/lib/math/cordic.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

$(B)/int_sqrt.o: $(K)/lib/math/int_sqrt.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

# The SROM rev 8 extractor of ssb-next/pci.c as it is in the tree, with the
# SPOFF/SPEX macros and the helpers it calls.
$(B)/sprom_r8.c: $(REPO)/ssb-next/pci.c | $(B)
	awk '/^#define SPOFF\(/ { p = 1 } /^static inline u8 ssbn_crc8\(/ { p = 0 } \
	     /^static (void|s8) sprom_(get_mac|extract_antgain|extract_r458|extract_r8)\(/ { p = 2 } \
	     p { print } p == 2 && /^\}/ { p = 0; print "" }' $< > $@

# LP_FN=1 at build time instruments phy_lp.c's functions (not the register
# accessors), for the function markers LP_FN=1 prints at run time.
ifeq ($(LP_FN),1)
FNFLAGS := -finstrument-functions \
	-finstrument-functions-exclude-file-list=include/,arch/,compat/,lp_trace.h,sprom_r8.c \
	-finstrument-functions-exclude-function-list=b43_lpphy_op_read,b43_lpphy_op_write,b43_lpphy_op_maskset,b43_lpphy_op_radio_read,b43_lpphy_op_radio_write,__udelay,msleep,b43_phy_read,b43_phy_write,b43_phy_mask,b43_phy_set,b43_phy_maskset,b43_phy_copy,b43_radio_read,b43_radio_write,b43_radio_mask,b43_radio_set,b43_radio_maskset,b43_read,b43_write,b43_lptab_read,b43_lptab_write,bus_,cc_,b43_current_band,__cyg
endif

$(B)/flows.o: flows.c lp_trace.h $(B)/sprom_r8.c $(REPO)/b43-next/phy_lp.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS) $(FNFLAGS) -I. -I$(B)

$(B)/trace.o: trace.c lp_trace.h | $(B)
	$(CC) -c $< -o $@ $(UFLAGS)

lp-trace: $(OBJS)
	$(CC) -no-pie $^ -o $@

clean:
	rm -rf $(B) lp-trace

.PHONY: all clean
