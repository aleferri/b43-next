# SPDX-License-Identifier: GPL-2.0
#
# The TX headers of b43-next/xmit.c, built as it is against the kernel
# headers (as ../integration does, see its Makefile for KVER, K, KG and the
# flags), for the devices, rates and frames of cases.c.
#
#   make check     the headers against expected.txt
#   make expected  expected.txt from this tree's xmit.c
#   make dump      every header in full (TXHDR_FULL=1), to diff two trees

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
          -DCONFIG_B43_NEXT_BCMA=1 -DCONFIG_B43_NEXT_PHY_AC=1 \
          -DCONFIG_B43_NEXT_PHY_HT=1 \
          -O2 -fno-strict-aliasing -fno-common -std=gnu11 \
          -Wall -Werror -Wno-pointer-sign -Wno-unused-function \
          -Wno-address-of-packed-member \
          -mno-red-zone -mgeneral-regs-only -include $(REPO)/compat/compat.h \
          -ffunction-sections -fdata-sections -MMD

AUTOCONF := $(KG)/include/config/auto.conf
KFLAGS += $(shell sed -n 's/^CONFIG_CC_MS_EXTENSIONS=//p' $(AUTOCONF))
KFLAGS += $(if $(shell grep -s '^CONFIG_FUNCTION_TRACER=y' $(AUTOCONF)),-DCC_USING_FENTRY)

UFLAGS := -O2 -g -Wall -Werror -std=gnu11

OBJS := $(B)/xmit.o $(B)/cases.o $(B)/kstubs.o $(B)/out.o

.PHONY: check expected dump clean
check: txhdr
	@./txhdr | diff -u expected.txt - > $(B)/diff.txt && \
	    echo "  $$(wc -l < expected.txt) cases, none changed" || \
	    { echo "  changed: $$(grep -c '^+[^+]' $(B)/diff.txt) of $$(wc -l < expected.txt)"; \
	      grep '^+[^+]' $(B)/diff.txt | head -20; exit 1; }

expected: txhdr
	./txhdr > expected.txt

dump: txhdr
	@TXHDR_FULL=1 ./txhdr

txhdr: $(OBJS)
	gcc -no-pie -Wl,--gc-sections -o $@ $(OBJS)

$(B)/xmit.o: $(REPO)/b43-next/xmit.c | $(B)
	gcc -c $< -o $@ $(KFLAGS)

$(B)/cases.o $(B)/kstubs.o: $(B)/%.o: %.c | $(B)
	gcc -c $< -o $@ $(KFLAGS)

$(B)/out.o: out.c | $(B)
	gcc -c $< -o $@ $(UFLAGS)

$(B):
	mkdir -p $@

-include $(B)/*.d

clean:
	rm -rf $(B) txhdr
