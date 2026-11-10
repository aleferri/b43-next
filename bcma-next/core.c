/*
 * Broadcom specific AMBA
 * Core ops
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/export.h>
#include <bcma-next/bcmn.h>

static bool bcmn_core_wait_value(struct bcmn_device *core, u16 reg, u32 mask,
				 u32 value, int timeout)
{
	unsigned long deadline = jiffies + timeout;
	u32 val;

	do {
		val = bcmn_aread32(core, reg);
		if ((val & mask) == value)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	bcmn_warn(core->bus, "Timeout waiting for register 0x%04X!\n", reg);

	return false;
}

bool bcmn_core_is_enabled(struct bcmn_device *core)
{
	if ((bcmn_aread32(core, BCMN_IOCTL) & (BCMN_IOCTL_CLK | BCMN_IOCTL_FGC))
	    != BCMN_IOCTL_CLK)
		return false;
	if (bcmn_aread32(core, BCMN_RESET_CTL) & BCMN_RESET_CTL_RESET)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(bcmn_core_is_enabled);

void bcmn_core_disable(struct bcmn_device *core, u32 flags)
{
	if (bcmn_aread32(core, BCMN_RESET_CTL) & BCMN_RESET_CTL_RESET)
		return;

	bcmn_core_wait_value(core, BCMN_RESET_ST, ~0, 0, 300);

	bcmn_awrite32(core, BCMN_RESET_CTL, BCMN_RESET_CTL_RESET);
	bcmn_aread32(core, BCMN_RESET_CTL);
	udelay(1);

	bcmn_awrite32(core, BCMN_IOCTL, flags);
	bcmn_aread32(core, BCMN_IOCTL);
	udelay(10);
}
EXPORT_SYMBOL_GPL(bcmn_core_disable);

int bcmn_core_enable(struct bcmn_device *core, u32 flags)
{
	bcmn_core_disable(core, flags);

	bcmn_awrite32(core, BCMN_IOCTL, (BCMN_IOCTL_CLK | BCMN_IOCTL_FGC | flags));
	bcmn_aread32(core, BCMN_IOCTL);

	bcmn_awrite32(core, BCMN_RESET_CTL, 0);
	bcmn_aread32(core, BCMN_RESET_CTL);
	udelay(1);

	bcmn_awrite32(core, BCMN_IOCTL, (BCMN_IOCTL_CLK | flags));
	bcmn_aread32(core, BCMN_IOCTL);
	udelay(1);

	return 0;
}
EXPORT_SYMBOL_GPL(bcmn_core_enable);

void bcmn_core_set_clockmode(struct bcmn_device *core,
			     enum bcmn_clkmode clkmode)
{
	u16 i;

	WARN_ON(core->id.id != BCMN_CORE_CHIPCOMMON &&
		core->id.id != BCMN_CORE_PCIE &&
		core->id.id != BCMN_CORE_80211);

	switch (clkmode) {
	case BCMN_CLKMODE_FAST:
		bcmn_set32(core, BCMN_CLKCTLST, BCMN_CLKCTLST_FORCEHT);
		usleep_range(64, 300);
		for (i = 0; i < 1500; i++) {
			if (bcmn_read32(core, BCMN_CLKCTLST) &
			    BCMN_CLKCTLST_HAVEHT) {
				i = 0;
				break;
			}
			udelay(10);
		}
		if (i)
			bcmn_err(core->bus, "HT force timeout\n");
		break;
	case BCMN_CLKMODE_DYNAMIC:
		bcmn_mask32(core, BCMN_CLKCTLST, ~BCMN_CLKCTLST_FORCEHT);
		break;
	}
}
EXPORT_SYMBOL_GPL(bcmn_core_set_clockmode);

void bcmn_core_pll_ctl(struct bcmn_device *core, u32 req, u32 status, bool on)
{
	u16 i;

	WARN_ON(req & ~BCMN_CLKCTLST_EXTRESREQ);
	WARN_ON(status & ~BCMN_CLKCTLST_EXTRESST);

	if (on) {
		bcmn_set32(core, BCMN_CLKCTLST, req);
		for (i = 0; i < 10000; i++) {
			if ((bcmn_read32(core, BCMN_CLKCTLST) & status) ==
			    status) {
				i = 0;
				break;
			}
			udelay(10);
		}
		if (i)
			bcmn_err(core->bus, "PLL enable timeout\n");
	} else {
		/*
		 * Mask the PLL but don't wait for it to be disabled. PLL may be
		 * shared between cores and will be still up if there is another
		 * core using it.
		 */
		bcmn_mask32(core, BCMN_CLKCTLST, ~req);
		bcmn_read32(core, BCMN_CLKCTLST);
	}
}
EXPORT_SYMBOL_GPL(bcmn_core_pll_ctl);

u32 bcmn_core_dma_translation(struct bcmn_device *core)
{
	switch (core->bus->hosttype) {
	case BCMN_HOSTTYPE_SOC:
		return 0;
	case BCMN_HOSTTYPE_PCI:
		if (bcmn_aread32(core, BCMN_IOST) & BCMN_IOST_DMA64)
			return BCMN_DMA_TRANSLATION_DMA64_CMT;
		else
			return BCMN_DMA_TRANSLATION_DMA32_CMT;
	default:
		bcmn_err(core->bus, "DMA translation unknown for host %d\n",
			 core->bus->hosttype);
	}
	return BCMN_DMA_TRANSLATION_NONE;
}
EXPORT_SYMBOL(bcmn_core_dma_translation);
