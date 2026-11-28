# SPDX-License-Identifier: GPL-2.0
#
# The sources of b43-next/, for Kbuild and for the test suites.

b43n-core	:= main.c beacon.c bus.c fw_layout.c sysfs.c xmit.c dma.c pio.c \
		   rfkill.c ampdu.c fw.c
# What every PHY links against
b43n-phy	:= phy_common.c ppr.c qmath.c calibrate.c

b43n-phy-g	:= phy_g.c tables.c lo.c wa.c
b43n-phy-n	:= phy_n.c tables_nphy.c radio_2055.c radio_2056.c radio_2057.c
b43n-phy-lp	:= phy_lp.c tables_lpphy.c
b43n-phy-ht	:= phy_ht.c tables_phy_ht.c radio_2059.c
b43n-phy-lcn	:= phy_lcn.c tables_phy_lcn.c
b43n-phy-ac	:= phy_ac.c tables_phy_ac.c radio_2069.c radar_ac.c

b43n-leds	:= leds.c
b43n-sdio	:= sdio.c
b43n-debugfs	:= debugfs.c
b43n-calstat	:= calstat.c
