/*
 * Broadcom specific AMBA
 * ChipCommon B Unit driver
 *
 * Copyright 2014, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/export.h>
#include <bcma-next/bcmn.h>

static bool bcmn_wait_reg(struct bcmn_bus *bus, void __iomem *addr, u32 mask,
			  u32 value, int timeout)
{
	unsigned long deadline = jiffies + timeout;
	u32 val;

	do {
		val = readl(addr);
		if ((val & mask) == value)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	bcmn_err(bus, "Timeout waiting for register %p\n", addr);

	return false;
}

void bcmn_chipco_b_mii_write(struct bcmn_drv_cc_b *ccb, u32 offset, u32 value)
{
	struct bcmn_bus *bus = ccb->core->bus;
	void __iomem *mii = ccb->mii;

	writel(offset, mii + BCMN_CCB_MII_MNG_CTL);
	bcmn_wait_reg(bus, mii + BCMN_CCB_MII_MNG_CTL, 0x0100, 0x0000, 100);
	writel(value, mii + BCMN_CCB_MII_MNG_CMD_DATA);
	bcmn_wait_reg(bus, mii + BCMN_CCB_MII_MNG_CTL, 0x0100, 0x0000, 100);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_b_mii_write);

int bcmn_core_chipcommon_b_init(struct bcmn_drv_cc_b *ccb)
{
	if (ccb->setup_done)
		return 0;

	ccb->setup_done = 1;
	ccb->mii = ioremap(ccb->core->addr_s[1], BCMN_CORE_SIZE);
	if (!ccb->mii)
		return -ENOMEM;

	return 0;
}

void bcmn_core_chipcommon_b_free(struct bcmn_drv_cc_b *ccb)
{
	if (ccb->mii)
		iounmap(ccb->mii);
}
