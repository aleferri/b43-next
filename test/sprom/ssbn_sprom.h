/*
 * ssbn_sprom.h — the tree's struct ssbn_sprom (ssbn_sprom_gen.h, cut out of
 * include/ssb-next/ssbn.h by the Makefile), and of struct bcmn_bus the one
 * member the extractor fills.
 */
#ifndef SSBN_SPROM_H
#define SSBN_SPROM_H

#include "kernel_shim.h"
#include "ssbn_sprom_gen.h"

struct bcmn_bus {
	struct ssbn_sprom sprom;
};

#endif
