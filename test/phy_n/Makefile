# SPDX-License-Identifier: GPL-2.0
#
# The N-PHY files of b43-next/, built as they are against the kernel
# headers (as ../integration does, see its Makefile for KVER, K, KG and the
# flags), with a bus that turns their accesses into mmio2ops ops.

include ../paths.mk

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
          -DCONFIG_B43_NEXT_BCMA=1 -DCONFIG_B43_NEXT_PHY_N=1 \
          -O2 -fno-strict-aliasing -fno-common -std=gnu11 \
          -Wall -Werror -Wno-pointer-sign -Wno-unused-function \
          -Wno-address-of-packed-member \
          -mno-red-zone -include $(REPO)/compat/compat.h

AUTOCONF := $(KG)/include/config/auto.conf
KFLAGS += $(shell sed -n 's/^CONFIG_CC_MS_EXTENSIONS=//p' $(AUTOCONF))
KFLAGS += $(if $(shell grep -s '^CONFIG_FUNCTION_TRACER=y' $(AUTOCONF)),-DCC_USING_FENTRY)

UFLAGS := -O2 -g -Wall -Werror -std=gnu11

# flows.c includes phy_n.c; the others are linked as they are.
DRIVER := tables_nphy.c radio_2057.c radio_2056.c radio_2055.c phy_common.c ppr.c

OBJS := $(patsubst %.c,$(B)/%.o,$(DRIVER)) $(B)/cordic.o $(B)/int_sqrt.o \
	$(B)/flows.o $(B)/trace.o

all: n-trace

$(B):
	mkdir -p $(B)

$(B)/%.o: $(REPO)/b43-next/%.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

# The kernel's CORDIC and int_sqrt, which the calibrations use
$(B)/cordic.o: $(K)/lib/math/cordic.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

$(B)/int_sqrt.o: $(K)/lib/math/int_sqrt.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS)

# N_FN=1 at build time instruments phy_n.c's functions (not the register
# accessors), for the function markers N_FN=1 prints at run time.
ifeq ($(N_FN),1)
FNFLAGS := -finstrument-functions \
	-finstrument-functions-exclude-file-list=include/,arch/,compat/,n_trace.h \
	-finstrument-functions-exclude-function-list=b43_nphy_op_maskset,b43_nphy_op_radio_read,b43_nphy_op_radio_write,__udelay,msleep,b43_phy_read,b43_phy_write,b43_phy_mask,b43_phy_set,b43_phy_maskset,b43_radio_read,b43_radio_write,b43_radio_mask,b43_radio_set,b43_radio_maskset,b43_read,b43_write,b43_ntab_read,b43_ntab_write,bus_,agent_,core_,b43_current_band,__cyg
endif

$(B)/flows.o: flows.c n_trace.h $(REPO)/b43-next/phy_n.c | $(B)
	$(CC) -c $< -o $@ $(KFLAGS) $(FNFLAGS) -I.

$(B)/trace.o: trace.c n_trace.h | $(B)
	$(CC) -c $< -o $@ $(UFLAGS)

n-trace: $(OBJS)
	$(CC) -no-pie $^ -o $@

check: n-trace
	./check.sh

clean:
	rm -rf $(B) n-trace

.PHONY: all check clean
