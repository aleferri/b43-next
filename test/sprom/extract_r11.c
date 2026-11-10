/*
 * extract_r11.c — the tree's bcmn_sprom_extract_r11() and what it needs of
 * bcma-next/sprom.c (extract_r11_gen.c, cut out by the Makefile), compiled
 * as they are, behind a function test.c can call.
 */
#include "kernel_shim.h"
#include "ssbn_sprom.h"
#include <ssb-next/ssbn_regs.h>

#include "extract_r11_gen.c"

void run_extract_r11(struct bcmn_bus *bus, const u16 *sprom)
{
	bcmn_sprom_extract_r11(bus, sprom);
}
