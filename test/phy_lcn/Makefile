# SPDX-License-Identifier: GPL-2.0
#
# The LCN-PHY files of b43-next/, built as they are against the kernel
# headers (as ../integration does, see its Makefile for KVER, K, KG and the
# flags), with a bus that turns their accesses into mmio2ops ops.

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
          -DCONFIG_B43_NEXT_BCMA=1 -DCONFIG_B43_NEXT_PHY_LCN=1 \
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

# flows.c includes phy_lcn.c; the others are linked as they are.
DRIVER := $(filter-out phy_lcn.c,$(b43n-phy-lcn)) $(b43n-phy)

OBJS := $(patsubst %.c,$(B)/%.o,$(DRIVER)) $(B)/cordic.o $(B)/int_sqrt.o \
	$(B)/flows.o $(B)/trace.o

all: lcn-trace

$(B):
	mkdir -p $(B)

$(B)/%.o: $(REPO)/b43-next/%.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

# The kernel's CORDIC and int_sqrt, which the calibrations use
$(B)/cordic.o: $(K)/lib/math/cordic.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

$(B)/int_sqrt.o: $(K)/lib/math/int_sqrt.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

$(B)/flows.o: flows.c lcn_trace.h $(REPO)/b43-next/phy_lcn.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS) -I.

$(B)/trace.o: trace.c lcn_trace.h | $(B)
	$(CC) -c $< -o $@ $(UFLAGS)

lcn-trace: $(OBJS)
	$(CC) -no-pie $^ -o $@

clean:
	rm -rf $(B) lcn-trace

.PHONY: all clean
