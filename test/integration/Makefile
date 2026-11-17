# SPDX-License-Identifier: GPL-2.0
#
# The whole of b43 on AC, against the real kernel headers.
#
# The point: in b43 the order of events cannot be read off the source -- 42
# gates on core_rev, fw.rev, phy->type and phy->rev with free else branches --
# so it is executed. The tree's b43-next/ and bcma-next/ sources are compiled
# unmodified and the layer below is stubbed, so the order is output and not
# exegesis.
#
# The kernel headers are the REAL ones, from /usr/src: there is no need to
# stub linux/*.h as ../unit does. On Ubuntu:
#     apt-get install linux-headers-generic
#
# The headers must be of a kernel the module supports, from 6.18
# (compat/compat.h). K and KG can be given by hand: the source tree and the
# build tree prepared with modules_prepare.
#
# -O2 is not an optimization, it is a requirement: without it the kernel
# headers give "impossible constraint in 'asm'" on asm_inline.
#
# -mno-red-zone as in the kernel: the headers make a `call` inside inline asm
# (arch_hweight.h), and the call writes the return address below %rsp, where
# without the flag gcc keeps the leaf functions' locals.
#
# No -w: it also turns off the -Werror=, and an implicit declaration passes
# `make check` to die at link time. The only warning the headers give is
# address-of-packed-member, which the kernel turns off the same way
# (scripts/Makefile.extrawarn).

include ../paths.mk
include $(REPO)/b43-next/files.mk

KVER    ?= $(shell ls /usr/src/ | grep -oE '^linux-headers-[0-9.]+-[0-9]+$$' | head -1 | sed 's/linux-headers-//')
K       ?= /usr/src/linux-headers-$(KVER)
KG      ?= /usr/src/linux-headers-$(KVER)-generic
B       := build

# The tree's include/: the headers of bcma-next and ssb-next, with the rev 11
# SROM fields and the 4360's chipstatus bits. The rest of bcma-next/, sprom.c
# included, is not needed: the bus is fake here.
KINC := -I$(REPO)/include -I$(KG)/include -I$(K)/include -I$(K)/include/uapi \
        -I$(KG)/include/generated/uapi \
        -I$(K)/arch/x86/include -I$(K)/arch/x86/include/uapi \
        -I$(KG)/arch/x86/include/generated \
        -I$(KG)/arch/x86/include/generated/uapi

KFLAGS := -nostdinc -I$(REPO)/b43-next -I$(REPO) $(KINC) -D__KERNEL__ -DMODULE -DKBUILD_MODNAME=\"b43\" \
          -include $(K)/include/linux/kconfig.h \
          -include $(K)/include/linux/compiler_types.h \
          -DCONFIG_B43_NEXT_BCMA=1 -DCONFIG_B43_NEXT_PHY_AC=1 \
          -DCONFIG_B43_NEXT_PHY_HT=1 -DCONFIG_B43_NEXT_PHY_LCN=1 \
          -DCONFIG_B43_NEXT_LEDS=1 \
          -O2 -fno-strict-aliasing -fno-common -std=gnu11 \
          -Wno-address-of-packed-member \
          -mno-red-zone \
          -Werror=implicit-function-declaration -Werror=switch \
          -include $(REPO)/compat/compat.h \
          -include $(CURDIR)/../ac_trace.h

# What the kernel's own Makefile adds from the headers' configuration: the C
# dialect's extensions (CONFIG_CC_MS_EXTENSIONS, -fms-extensions from 6.19)
# and CC_USING_FENTRY, which asm/ftrace.h requires with the function tracer.
AUTOCONF := $(KG)/include/config/auto.conf
KFLAGS += $(shell sed -n 's/^CONFIG_CC_MS_EXTENSIONS=//p' $(AUTOCONF))
KFLAGS += $(if $(shell grep -s '^CONFIG_FUNCTION_TRACER=y' $(AUTOCONF)),-DCC_USING_FENTRY)

# The rest of b43's, bcma's and ssb's options as the headers' kernel has them,
# under this tree's names.
KFLAGS += $(shell sed -nE 's/^CONFIG_B43_([A-Z0-9_]+)=y$$/-DCONFIG_B43_NEXT_\1=1/p; \
		s/^CONFIG_BCMA_([A-Z0-9_]+)=y$$/-DCONFIG_BCMN_\1=1/p; \
		s/^CONFIG_SSB_([A-Z0-9_]+)=y$$/-DCONFIG_SSBN_\1=1/p' $(AUTOCONF) | sort -u)

# As in Kbuild: the headers' own typed allocators, if they have kzalloc_flex().
KFLAGS += $(if $(shell grep -sw 'define kzalloc_flex' $(K)/include/linux/slab.h),-DB43_NEXT_HAVE_KZALLOC_OBJ)

# CONFIG_BCM63XX: the target and the collection's boards are BCM63xx routers
# with the radio on PCIe, and on those hosts b43 reads the register addresses
# back as the stock driver does (flush_writes in b43/bus.c). The headers do
# not use it. BCM63XX=0 for a board on another host, like the R6400 v1 on its
# BCM4708, where neither b43 nor wl reads back.
BCM63XX ?= 1
KFLAGS += $(if $(filter 1,$(BCM63XX)),-DCONFIG_BCM63XX=1)
#
# Build for the backtrace: -O2 stays, because the headers require it;
# -fno-inline -g keep the frames, so gdb says in which b43 function the
# probe dies instead of in b43_one_core_attach where everything is inlined.
ifeq ($(DEBUG),1)
KFLAGS += -fno-inline -g
endif

# The b43 files compiled as they are.
UPSTREAM := $(filter-out sysfs.c pio.c,$(b43n-core)) $(b43n-phy) \
            $(b43n-leds) $(b43n-phy-ac) $(b43n-phy-ht) $(b43n-phy-lcn)

# bcma's core management, as it is too: the core's reset, clock and PLL go
# through the wrapper and clk_ctl_st, which the stub traces.
BCMA := core.c driver_chipcommon.c driver_chipcommon_pmu.c driver_pci.c \
        driver_pcie2.c sprom.c

# The SPROM of a board without one, from its NVRAM (B43_NVRAM), as on the
# Broadcom routers.
NVRAM := bcm47xx_sprom.c

# The kernel's CORDIC, which the HT-PHY's calibrations use.
KLIB := lib/math/cordic.c

# Ours: the fake bus that emits the ops. The hook is the bcmn_host_ops
# vtable, not the bcmn_* symbols the link asks for.
LOCAL := bcma_stub.c subsystem_stub.c dma_stub.c main.c

# trace_out.c is USER code: it needs stdio and getenv, and it is not driver
# code. It is compiled with the normal flags, not with -nostdinc.
USER := trace_out.c kernel_shim.c chip_link.c
UFLAGS := -O2 -Wall -std=gnu11
ifeq ($(DEBUG),1)
UFLAGS += -g

# ASAN=1 (with DEBUG=1): the sanitizer on both halves, kernel and userspace.
# It makes the sanitizer say *who* corrupts the memory; the __hwasan_*
# aliases are in kernel_shim.c, because the kernel headers remap memcpy and
# memset when they see __SANITIZE_ADDRESS__.
ifeq ($(ASAN),1)
KFLAGS += -fsanitize=address
UFLAGS += -fsanitize=address
LDEXTRA := -fsanitize=address
endif
endif

# What trace_out.c asks of libc: resolved at the final link, so it is kept
# out of the count of the remaining work or the number lies.
LIBC := pipe|fork|execl|dup2|close|waitpid|fdopen|fflush|fputs|perror|_exit|exit|atexit|memmove|fprintf|fopen|fclose|fgets|fwrite|getenv|memset|memcpy|stdout|stderr|strcmp|strstr|strnlen|snprintf|sscanf|__isoc99_sscanf|calloc|realloc|free|abort|_GLOBAL_OFFSET_TABLE_|__stack_chk_fail|__printf_chk|__fprintf_chk|__vfprintf_chk|setvbuf|strtol|strtoll|strtoull|strncmp|strcspn|strsep|rewind|fseek|ftell|fread|__fread_chk|malloc|strlen|__snprintf_chk

DRV_OBJS := $(patsubst %.c,$(B)/b43-next/%.o,$(UPSTREAM)) \
            $(patsubst %.c,$(B)/bcma-next/%.o,$(BCMA)) \
            $(patsubst %.c,$(B)/nvram/%.o,$(NVRAM)) \
            $(patsubst %.c,$(B)/klib/%.o,$(notdir $(KLIB)))

.PHONY: all check link run clean
all: b43-trace

# b43's sources compile against the real headers.
check:
	@test -d $(K) || { echo "kernel headers missing: apt-get install linux-headers-generic"; exit 1; }
	@for f in $(addprefix b43-next/,$(UPSTREAM)) $(addprefix bcma-next/,$(BCMA)); do \
	    n=$$(gcc -c $(REPO)/$$f -o /dev/null $(KFLAGS) 2>&1 | grep -c 'error:'); \
	    echo "  $$f: $$n errors"; \
	done

# What is missing for the link, per family. The number is the map of the
# remaining work.
link:
	@mkdir -p $(B)/b43-next $(B)/bcma-next $(B)/nvram $(B)/klib
	@for f in $(addprefix b43-next/,$(UPSTREAM)) $(addprefix bcma-next/,$(BCMA)); do \
	    gcc -c $(REPO)/$$f -o $(B)/$${f%.c}.o $(KFLAGS) || exit 1; done
	@for f in $(addprefix nvram/,$(NVRAM)); do \
	    gcc -c $(REPO)/$$f -o $(B)/$${f%.c}.o $(KFLAGS) \
	        -DCONFIG_BCM47XX_NVRAM=1 || exit 1; done
	@for f in $(KLIB); do \
	    gcc -c $(K)/$$f -o $(B)/klib/$$(basename $${f%.c}).o $(KFLAGS) \
	        || exit 1; done
	@for f in $(LOCAL); do gcc -c $$f -o $${f%.c}.o $(KFLAGS) -I.; done
	@for f in $(USER); do gcc -c $$f -o $${f%.c}.o $(UFLAGS) -I.; done
	@ld -r $(DRV_OBJS) \
	      $(patsubst %.c,%.o,$(LOCAL)) $(patsubst %.c,%.o,$(USER)) \
	      -o b43_partial.o
	@nm -u b43_partial.o | awk '{print $$2}' | sort -u > residual.txt
	@echo "  unresolved symbols: $$(wc -l < residual.txt)"
	@for p in bcmn_ ssbn_ ieee80211_ b43_; do \
	    printf "    %-12s %s\n" $$p "$$(grep -c "^$$p" residual.txt)"; \
	done
	@printf "    %-12s %s\n" kernel \
	    "$$(grep -vE '^(bcmn_|ssbn_|ieee80211_|b43_)' residual.txt | \
	        grep -vwE '$(LIBC)' | wc -l)"
	@printf "    %-12s %s  (resolved by libc, not work)\n" libc \
	    "$$(grep -wcE '$(LIBC)' residual.txt)"

# The executable, and the trace.
b43-trace: link
	@gcc -o b43-trace b43_partial.o $(LDEXTRA)
	@echo "  $$(ls -la b43-trace | awk '{print $$5}') byte"

# The trace. Without an oracle every read is zero and it is NOT a
# measurement: pass a capture folded by reverse-tools/trace_filter.py
# --retvals. The trace goes to stdout along with make's output: for a file to
# give compare.py use TRACE_OUT, which becomes B43_TRACE_OUT for the binary.
# Two traces from the same build: the bare one for the comparison and, next
# to it (TRACE_OUT.fn), the one with the function markers for attribution.
# Coming out of a single run leaves no way to reason on an annotated trace of
# an earlier build, which cannot be told apart from the current one by eye.
# VCHIP: the command that starts b43-tools' vchipd.py, to run the 802.11 core
# on the virtual chip (chip_link.h); FW_DIR: the directory b43-fwcutter -w
# wrote the firmware to.
CHIP_ENV = $(if $(VCHIP),B43_VCHIP="$(VCHIP)") $(if $(FW_DIR),B43_FW_DIR=$(FW_DIR)) \
	   $(if $(B43_BOARD),B43_BOARD=$(B43_BOARD))

run: b43-trace
	@test -n "$(ORACLE)" || { \
	    echo "usage: make run ORACLE=<folded capture> [TRACE_OUT=<file>]"; exit 1; }
	$(CHIP_ENV) B43_READ_ORACLE=$(ORACLE) $(if $(TRACE_OUT),B43_TRACE_OUT=$(TRACE_OUT)) \
	    $(if $(B43_CHANNEL),B43_CHANNEL=$(B43_CHANNEL)) \
	    $(if $(B43_BW),B43_BW=$(B43_BW)) \
	    $(if $(B43_TIMELINE),B43_TIMELINE=$(B43_TIMELINE)) ./b43-trace
	$(if $(TRACE_OUT),$(CHIP_ENV) B43_FN_MARKERS=1 B43_READ_ORACLE=$(ORACLE) \
	    B43_TRACE_OUT=$(TRACE_OUT).fn \
	    $(if $(B43_CHANNEL),B43_CHANNEL=$(B43_CHANNEL)) \
	    $(if $(B43_BW),B43_BW=$(B43_BW)) \
	    $(if $(B43_TIMELINE),B43_TIMELINE=$(B43_TIMELINE)) ./b43-trace 2>/dev/null)

clean:
	rm -rf $(B) *.o b43_partial.o residual.txt b43-trace
