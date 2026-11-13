# SPDX-License-Identifier: GPL-2.0
#
# b43 intero su AC, contro gli header kernel veri.
#
# Il punto: in b43 l'ordine degli eventi non si legge -- 42 gate su core_rev,
# fw.rev, phy->type e phy->rev con rami else liberi -- quindi lo si esegue.
# Qui si compilano i sorgenti di b43-next/ e bcma-next/ dell'albero senza
# modificarli e si stubba il livello sotto, cosi' l'ordine e' output e non
# esegesi.
#
# Gli header kernel sono quelli VERI, da /usr/src: non serve stubbare
# linux/*.h come fa ../unit. Su Ubuntu:
#     apt-get install linux-headers-generic
#
# Gli header devono essere di un kernel che il modulo supporta, da 6.18
# (compat/compat.h). K e KG si possono dare a mano: l'albero dei sorgenti e
# quello di build preparato con modules_prepare.
#
# -O2 non e' un'ottimizzazione, e' un requisito: senza, gli header kernel
# danno "impossible constraint in 'asm'" su asm_inline.
#
# -mno-red-zone come nel kernel: gli header fanno `call` dentro inline asm
# (arch_hweight.h), e la call scrive l'indirizzo di ritorno sotto %rsp, dove
# senza il flag gcc tiene le variabili locali delle funzioni foglia.
#
# Niente -w: spegne anche i -Werror=, e una dichiarazione implicita passa
# `make check` per morire al link. L'unico avviso che gli header danno e'
# address-of-packed-member, che il kernel spegne allo stesso modo
# (scripts/Makefile.extrawarn).

include ../paths.mk

KVER    ?= $(shell ls /usr/src/ | grep -oE '^linux-headers-[0-9.]+-[0-9]+$$' | head -1 | sed 's/linux-headers-//')
K       ?= /usr/src/linux-headers-$(KVER)
KG      ?= /usr/src/linux-headers-$(KVER)-generic
B       := build

# include/ dell'albero: gli header di bcma-next e ssb-next, con i campi della
# SROM rev 11 e i bit di chipstatus del 4360. Il resto di bcma-next/, sprom.c
# compreso, non serve: qui il bus e' finto.
KINC := -I$(REPO)/include -I$(KG)/include -I$(K)/include -I$(K)/include/uapi \
        -I$(KG)/include/generated/uapi \
        -I$(K)/arch/x86/include -I$(K)/arch/x86/include/uapi \
        -I$(KG)/arch/x86/include/generated \
        -I$(KG)/arch/x86/include/generated/uapi

KFLAGS := -nostdinc -I$(REPO)/b43-next -I$(REPO) $(KINC) -D__KERNEL__ -DMODULE -DKBUILD_MODNAME=\"b43\" \
          -include $(K)/include/linux/kconfig.h \
          -include $(K)/include/linux/compiler_types.h \
          -DCONFIG_B43_NEXT_BCMA=1 -DCONFIG_B43_NEXT_PHY_AC=1 -DCONFIG_B43_NEXT_LEDS=1 \
          -DCONFIG_BCM63XX=1 \
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

# CONFIG_BCM63XX: il bersaglio e le board della collezione sono router BCM63xx
# con la radio su PCIe, e su quegli host b43 rilegge gli indirizzi dei
# registri come il driver stock (flush_writes in b43/bus.c). Gli header non
# lo usano.
#
# Build per il backtrace: -O2 resta, perche' lo chiedono gli header; -fno-inline
# -g tengono i frame, cosi' gdb dice in quale funzione di b43 la probe muore
# invece che in b43_one_core_attach dove tutto e' inlinato.
ifeq ($(DEBUG),1)
KFLAGS += -fno-inline -g
endif

# I file di b43 che si compilano come sono. Tutti e tre a zero errori.
UPSTREAM := main.c phy_common.c bus.c xmit.c dma.c leds.c rfkill.c \
            phy_ac.c tables_phy_ac.c radio_2069.c \
            ppr.c radar_ac.c

# La gestione del core di bcma, anche lei com'e': reset, clock e PLL del
# core passano dal wrapper e da clk_ctl_st, che lo stub traccia.
BCMA := core.c driver_chipcommon.c driver_chipcommon_pmu.c driver_pcie2.c

# I nostri: il bus finto che emette le op. L'aggancio e' la vtable
# bcmn_host_ops, non i simboli bcmn_* che il link chiede.
LOCAL := bcma_stub.c subsystem_stub.c dma_stub.c main.c

# trace_out.c e' codice UTENTE: gli servono stdio e getenv, e non e' codice di
# driver. Si compila coi flag normali, non con -nostdinc.
USER := trace_out.c kernel_shim.c chip_link.c
UFLAGS := -O2 -Wall -std=gnu11
ifeq ($(DEBUG),1)
UFLAGS += -g

# ASAN=1 (con DEBUG=1): il sanitizer sulle due meta', kernel e userspace. Serve
# a far dire *chi* corrompe la memoria; gli alias __hwasan_* stanno in
# kernel_shim.c, perche' gli header del kernel rimappano memcpy e memset
# quando vedono __SANITIZE_ADDRESS__.
ifeq ($(ASAN),1)
KFLAGS += -fsanitize=address
UFLAGS += -fsanitize=address
LDEXTRA := -fsanitize=address
endif
endif

# Cio' che trace_out.c chiede a libc: si risolve al link finale, quindi va
# tenuto fuori dal conto del lavoro rimasto o il numero mente.
LIBC := pipe|fork|execl|dup2|close|waitpid|fdopen|fflush|fputs|perror|_exit|exit|atexit|memmove|fprintf|fopen|fclose|fgets|fwrite|getenv|memset|memcpy|stdout|stderr|strcmp|strstr|strnlen|snprintf|sscanf|__isoc99_sscanf|calloc|realloc|free|abort|_GLOBAL_OFFSET_TABLE_|__stack_chk_fail|__printf_chk|__fprintf_chk|__vfprintf_chk|setvbuf|strtol|rewind|fseek|ftell|fread|__fread_chk|malloc|strlen|__snprintf_chk

DRV_OBJS := $(patsubst %.c,$(B)/b43-next/%.o,$(UPSTREAM)) \
            $(patsubst %.c,$(B)/bcma-next/%.o,$(BCMA))

.PHONY: all check link run clean
all: b43-trace

# Traguardo 1: i sorgenti di b43 compilano contro gli header veri.
check:
	@test -d $(K) || { echo "header kernel assenti: apt-get install linux-headers-generic"; exit 1; }
	@for f in $(addprefix b43-next/,$(UPSTREAM)) $(addprefix bcma-next/,$(BCMA)); do \
	    n=$$(gcc -c $(REPO)/$$f -o /dev/null $(KFLAGS) 2>&1 | grep -c 'error:'); \
	    echo "  $$f: $$n errori"; \
	done

# Traguardo 2: quanto manca al link, per famiglia. Il numero e' la mappa del
# lavoro rimasto, e il README lo spiega voce per voce.
link:
	@mkdir -p $(B)/b43-next $(B)/bcma-next
	@for f in $(addprefix b43-next/,$(UPSTREAM)) $(addprefix bcma-next/,$(BCMA)); do \
	    gcc -c $(REPO)/$$f -o $(B)/$${f%.c}.o $(KFLAGS) || exit 1; done
	@for f in $(LOCAL); do gcc -c $$f -o $${f%.c}.o $(KFLAGS) -I.; done
	@for f in $(USER); do gcc -c $$f -o $${f%.c}.o $(UFLAGS) -I.; done
	@ld -r $(DRV_OBJS) \
	      $(patsubst %.c,%.o,$(LOCAL)) $(patsubst %.c,%.o,$(USER)) \
	      -o b43_partial.o
	@nm -u b43_partial.o | awk '{print $$2}' | sort -u > residual.txt
	@echo "  simboli non risolti: $$(wc -l < residual.txt)"
	@for p in bcmn_ ssbn_ ieee80211_ b43_; do \
	    printf "    %-12s %s\n" $$p "$$(grep -c "^$$p" residual.txt)"; \
	done
	@printf "    %-12s %s\n" kernel \
	    "$$(grep -vE '^(bcmn_|ssbn_|ieee80211_|b43_)' residual.txt | \
	        grep -vwE '$(LIBC)' | wc -l)"
	@printf "    %-12s %s  (si risolvono su libc, non sono lavoro)\n" libc \
	    "$$(grep -wcE '$(LIBC)' residual.txt)"

# Traguardo 4: l'eseguibile, e la traccia.
b43-trace: link
	@gcc -o b43-trace b43_partial.o $(LDEXTRA)
	@echo "  $$(ls -la b43-trace | awk '{print $$5}') byte"

# La traccia. Senza oracolo ogni lettura e' zero e NON e' una misura: si passa
# una cattura ripiegata da reverse-tools/trace_filter.py --retvals.
# La traccia va su stdout insieme all'output di make: per un file da dare a
# compare.py si usa TRACE_OUT, che diventa B43_TRACE_OUT per il binario.
# Due tracce dalla stessa build: quella nuda per il confronto e, accanto
# (TRACE_OUT.fn), quella con i marcatori di funzione per attribuire. Uscire da
# una sola corsa toglie il modo di ragionare su una traccia annotata di una
# build precedente, che non si distingue a occhio da quella corrente.
# VCHIP: the command that starts b43-tools' vchipd.py, to run the 802.11 core
# on the virtual chip (chip_link.h); FW_DIR: the directory b43-fwcutter -w
# wrote the firmware to.
CHIP_ENV = $(if $(VCHIP),B43_VCHIP="$(VCHIP)") $(if $(FW_DIR),B43_FW_DIR=$(FW_DIR)) \
	   $(if $(B43_BOARD),B43_BOARD=$(B43_BOARD))

run: b43-trace
	@test -n "$(ORACLE)" || { \
	    echo "uso: make run ORACLE=<cattura ripiegata> [TRACE_OUT=<file>]"; exit 1; }
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
