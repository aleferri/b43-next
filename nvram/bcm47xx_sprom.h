/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_NEXT_BCM47XX_SPROM_H_
#define B43_NEXT_BCM47XX_SPROM_H_

#include <linux/errno.h>
#include <linux/types.h>

struct bcmn_bus;
struct ssbn_bus;
struct ssbn_sprom;

#if IS_ENABLED(CONFIG_BCM47XX_NVRAM)
/* The SPROM of the board's variables under prefix */
void bcm47xx_fill_sprom(struct ssbn_sprom *sprom, const char *prefix,
			bool fallback);

/* The SPROM from the board's NVRAM, for a bus whose device has none. */
int bcm47xx_get_sprom_ssb(struct ssbn_bus *bus, struct ssbn_sprom *out);
int bcm47xx_get_sprom_bcma(struct bcmn_bus *bus, struct ssbn_sprom *out);

/*
 * The NVRAM-only variables of a bus whose SPROM was read from the device:
 * what bcm47xx_fill_sprom() would read for it that the SPROM has no word
 * for.
 */
int bcm47xx_sprom_fill_bcma_nvram(struct bcmn_bus *bus, struct ssbn_sprom *out);
#else
static inline int bcm47xx_get_sprom_ssb(struct ssbn_bus *bus,
					struct ssbn_sprom *out)
{
	return -ENOENT;
}

static inline int bcm47xx_get_sprom_bcma(struct bcmn_bus *bus,
					 struct ssbn_sprom *out)
{
	return -ENOENT;
}

static inline int bcm47xx_sprom_fill_bcma_nvram(struct bcmn_bus *bus,
						struct ssbn_sprom *out)
{
	return -ENOTSUPP;
}
#endif

#endif /* B43_NEXT_BCM47XX_SPROM_H_ */
