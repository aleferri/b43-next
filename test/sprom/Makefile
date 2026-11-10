# Makefile for the bcma_sprom_extract_r11 offline harness.
#
# Build:   make
# Run:     make check       (or ./test)
# Clean:   make clean

include ../paths.mk

CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
CFLAGS  += -I$(REPO)/include
LDFLAGS ?=

OBJS = extract_r11.o data_load.o synth_srom.o test.o

test: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# The NVRAM path: nvram/bcm47xx_sprom.c as it is in the tree, against the
# tree's struct ssbn_sprom, cut out of ssbn.h.
NVRAM_OBJS = bcm47xx_sprom.o nvram_backend.o data_load.o test_nvram.o
NVRAM_CFLAGS = -I. -Invram_shim $(CFLAGS) -DCONFIG_BCM47XX_NVRAM=1

test_nvram: $(NVRAM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(NVRAM_OBJS)

ssbn_sprom_gen.h: $(REPO)/include/ssb-next/ssbn.h
	awk '/^struct ssbn_sprom(_core_pwr_info|_rxgains)? \{$$/ || \
	     /^static inline s8 ssbn_sprom_antgain_qdb\(/ { p = 1 } \
	     p { print } p && /^\}/ { p = 0; print "" }' $< > $@

bcm47xx_sprom.o: $(REPO)/nvram/bcm47xx_sprom.c ssbn_sprom_gen.h nvram_shim.h
	$(CC) $(NVRAM_CFLAGS) -c -o $@ $<

test_nvram.o: test_nvram.c ssbn_sprom_gen.h nvram_shim.h nvram_backend.h
	$(CC) $(NVRAM_CFLAGS) -c -o $@ $<

nvram_backend.o: nvram_backend.c nvram_backend.h data_load.h

# The raw path: the rev 11 extractor of bcma-next/sprom.c as it is in the
# tree, with the SPOFF/SPEX macros and the rxgains helper it uses.
extract_r11_gen.c: $(REPO)/bcma-next/sprom.c
	awk '/^#define SPOFF\(/ { p = 1 } /^static s8 sprom_extract_antgain\(/ { p = 0 } \
	     /^static void bcmn_sprom_(unpack_rxgains|extract_r11)\(/ { p = 2 } \
	     p { print } p == 2 && /^\}/ { p = 0; print "" }' $< > $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

extract_r11.o: extract_r11.c extract_r11_gen.c kernel_shim.h ssbn_sprom.h ssbn_sprom_gen.h
data_load.o:   data_load.c   data_load.h   kernel_shim.h
synth_srom.o:  synth_srom.c  synth_srom.h  data_load.h kernel_shim.h
test.o:        test.c        data_load.h   ssbn_sprom.h ssbn_sprom_gen.h synth_srom.h

.PHONY: check check-bcm4360usb check-d6220 check-agcombo check-nvram clean
check: test
	./test $(TRACES)/dsl3580l/wl1_srom_raw.txt $(TRACES)/dsl3580l/wl1_nvram.txt

# Second test vector: BCM4360 USB rev-11 reference NVRAM (no raw SROM
# available — runs the synth-based round-trip described in the patch
# series cover letter and in synth_srom.c).
check-bcm4360usb: test
	./test --synth vectors/bcm4360usb.nvram

# Third test vector: Netgear D6220 wl1 dumps. Same chip family as the
# DSL-3580L (BCM43b3, rev-11 SROM) but distinct boardrev, PA tables
# and asymmetric maxp5ga0. Hardware-real srdump+nvram_dump pair, not
# synthetic.
check-d6220: test
	./test $(TRACES)/d6220/wl1_srom_raw.txt \
	       $(TRACES)/d6220/wl1_nvram.txt

# Fourth test vector: agcombo (BCM4360 reference, 3x3, dual-band).
# srdump returns zero on this blob (OEM omits the raw path), so the
# vector runs in --synth mode against wl1_nvram.txt only. Exercises
# fields the BCM43b3 boards leave dormant: 2.4 GHz PA chain, aa2g=7,
# chain 2 rxgains.
check-agcombo: test
	./test --synth $(TRACES)/vd625-agcombo/wl1_nvram.txt

# The NVRAM path on every rev 11 dump in the repository.
check-nvram: test_nvram
	set -e; for b in dsl3580l d6220 vd625-agcombo tg789vac-v2; do \
		./test_nvram $(TRACES)/$$b/wl1_nvram.txt; done
	./test_nvram vectors/bcm4360usb.nvram

clean:
	rm -f $(OBJS) $(NVRAM_OBJS) ssbn_sprom_gen.h extract_r11_gen.c test test_nvram
