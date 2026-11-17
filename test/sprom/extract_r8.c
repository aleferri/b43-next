/*
 * extract_r8.c — the tree's bcmn_sprom_extract_r8() and what it needs of
 * bcma-next/sprom.c (extract_r8_gen.c, cut out by the Makefile), compiled
 * as they are, behind a function decode_r8.c can call.
 */
#include "kernel_shim.h"
#include "ssbn_sprom.h"
#include <ssb-next/ssbn_regs.h>

#include "extract_r8_gen.c"

void run_extract_r8(struct bcmn_bus *bus, const u16 *sprom)
{
	bcmn_sprom_extract_r8(bus, sprom);
}
