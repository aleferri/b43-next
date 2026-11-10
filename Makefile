# SPDX-License-Identifier: GPL-2.0
#
#   make [KVER=...] [KSRC=...] [B43_NEXT_DEBUG=y]
#   make install          modules, then the modprobe and udev configuration
#   make uninstall
#
# The build itself is in Kbuild.

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
MODDIR := extra/b43-next

DESTDIR ?=
MODPROBE_DIR := $(DESTDIR)/etc/modprobe.d
UDEV_DIR := $(DESTDIR)/usr/lib/udev

all: modules

modules:
	$(MAKE) -C $(KSRC) M=$(CURDIR) modules

install: modules install-config
	$(MAKE) -C $(KSRC) M=$(CURDIR) INSTALL_MOD_DIR=$(MODDIR) modules_install
	depmod -a $(KVER)

install-config:
	install -D -m 644 modprobe.d/b43-next.conf $(MODPROBE_DIR)/b43-next.conf
	install -D -m 644 udev/70-b43-next.rules $(UDEV_DIR)/rules.d/70-b43-next.rules
	install -D -m 755 udev/b43-next-bind $(UDEV_DIR)/b43-next-bind

uninstall:
	rm -rf /lib/modules/$(KVER)/$(MODDIR)
	depmod -a $(KVER)
	rm -f $(MODPROBE_DIR)/b43-next.conf
	rm -f $(UDEV_DIR)/rules.d/70-b43-next.rules $(UDEV_DIR)/b43-next-bind

clean:
	$(MAKE) -C $(KSRC) M=$(CURDIR) clean

.PHONY: all modules install install-config uninstall clean
