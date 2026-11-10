#!/bin/sh
# Translate upstream b43, bcma and ssb sources, or patches against them, into
# the names of this tree, so that b43-next carries its own copies of the two
# buses and loads next to the kernel's ssb, bcma and b43:
#
#   bcma_*, BCMA_*, CONFIG_BCMA*      -> bcmn_*, BCMN_*, CONFIG_BCMN*
#   ssb_*, SSB_*, CONFIG_SSB*         -> ssbn_*, SSBN_*, CONFIG_SSBN*
#   <linux/bcma/bcma*.h>              -> <bcma-next/bcmn*.h>
#   <linux/ssb/ssb*.h>                -> <ssb-next/ssbn*.h>
#   CONFIG_B43_*                      -> CONFIG_B43_NEXT_*
#   kernel paths in patch headers     -> paths of this tree
#
# A name is renamed only where it starts a word: b43's own b43_bus_bcma_*,
# b43_ssb_* and B43_BUS_SSB stay as they are. The PCI device's ssb_sprom
# attribute keeps its name, which tools read.
#
# Usage: scripts/rename-upstream.sh FILE...    rewrites the files in place
#        scripts/rename-upstream.sh < IN > OUT
set -eu

script='
s{<linux/bcma/bcma\.h>}{<bcma-next/bcmn.h>}g;
s{<linux/bcma/bcma_(\w+)\.h>}{<bcma-next/bcmn_$1.h>}g;
s{<linux/ssb/ssb\.h>}{<ssb-next/ssbn.h>}g;
s{<linux/ssb/ssb_(\w+)\.h>}{<ssb-next/ssbn_$1.h>}g;
s{"bcma_private\.h"}{"bcmn_private.h"}g;
s{"ssb_private\.h"}{"ssbn_private.h"}g;
s{\bdrivers/bcma/bcma_private\.h\b}{bcma-next/bcmn_private.h}g;
s{\bdrivers/ssb/ssb_private\.h\b}{ssb-next/ssbn_private.h}g;
s{\bdrivers/bcma/}{bcma-next/}g;
s{\bdrivers/ssb/}{ssb-next/}g;
s{\bdrivers/net/wireless/broadcom/b43/}{b43-next/}g;
s{\binclude/linux/bcma/bcma\.h\b}{include/bcma-next/bcmn.h}g;
s{\binclude/linux/bcma/bcma_(\w+)\.h\b}{include/bcma-next/bcmn_$1.h}g;
s{\binclude/linux/ssb/ssb\.h\b}{include/ssb-next/ssbn.h}g;
s{\binclude/linux/ssb/ssb_(\w+)\.h\b}{include/ssb-next/ssbn_$1.h}g;
s{\bdrivers/firmware/broadcom/bcm47xx_sprom\.c\b}{nvram/bcm47xx_sprom.c}g;
s{<linux/bcm47xx_sprom\.h>}{"../nvram/bcm47xx_sprom.h"}g;
s{\bCONFIG_BCMA(?=\b|_)}{CONFIG_BCMN}g;
s{\bCONFIG_SSB(?=\b|_)}{CONFIG_SSBN}g;
s{\bCONFIG_B43_}{CONFIG_B43_NEXT_}g;
s{\bLINUX_BCMA_}{LINUX_BCMN_}g;
s{\bLINUX_SSB_}{LINUX_SSBN_}g;
s{(?<![A-Za-z0-9_])(_*)bcma_}{$1bcmn_}g;
s{(?<![A-Za-z0-9_])(_*)BCMA_}{$1BCMN_}g;
s{(?<![A-Za-z0-9_])(?<!DEVICE_ATTR_ADMIN_RW\()(_*)ssb_(?!sprom_(?:show|store)\b)}{$1ssbn_}g;
s{(?<![A-Za-z0-9_])(_*)SSB_}{$1SSBN_}g;
'

if [ $# -eq 0 ]; then
	exec perl -pe "$script"
fi
exec perl -pi -e "$script" "$@"
