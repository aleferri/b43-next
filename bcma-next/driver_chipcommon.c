/*
 * Broadcom specific AMBA
 * ChipCommon core driver
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 * Copyright 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/bcm47xx_wdt.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <bcma-next/bcmn.h>

static inline u32 bcmn_cc_write32_masked(struct bcmn_drv_cc *cc, u16 offset,
					 u32 mask, u32 value)
{
	value &= mask;
	value |= bcmn_cc_read32(cc, offset) & ~mask;
	bcmn_cc_write32(cc, offset, value);

	return value;
}

u32 bcmn_chipco_get_alp_clock(struct bcmn_drv_cc *cc)
{
	if (cc->capabilities & BCMN_CC_CAP_PMU)
		return bcmn_pmu_get_alp_clock(cc);

	return 20000000;
}
EXPORT_SYMBOL_GPL(bcmn_chipco_get_alp_clock);

static bool bcmn_core_cc_has_pmu_watchdog(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	if (cc->capabilities & BCMN_CC_CAP_PMU) {
		if (bus->chipinfo.id == BCMN_CHIP_ID_BCM53573) {
			WARN(bus->chipinfo.rev <= 1, "No watchdog available\n");
			/* 53573B0 and 53573B1 have bugged PMU watchdog. It can
			 * be enabled but timer can't be bumped. Use CC one
			 * instead.
			 */
			return false;
		}
		return true;
	} else {
		return false;
	}
}

static u32 bcmn_chipco_watchdog_get_max_timer(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 nb;

	if (bcmn_core_cc_has_pmu_watchdog(cc)) {
		if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4706)
			nb = 32;
		else if (cc->core->id.rev < 26)
			nb = 16;
		else
			nb = (cc->core->id.rev >= 37) ? 32 : 24;
	} else {
		nb = 28;
	}
	if (nb == 32)
		return 0xffffffff;
	else
		return (1 << nb) - 1;
}

static u32 bcmn_chipco_watchdog_timer_set_wdt(struct bcm47xx_wdt *wdt,
					      u32 ticks)
{
	struct bcmn_drv_cc *cc = bcm47xx_wdt_get_drvdata(wdt);

	return bcmn_chipco_watchdog_timer_set(cc, ticks);
}

static u32 bcmn_chipco_watchdog_timer_set_ms_wdt(struct bcm47xx_wdt *wdt,
						 u32 ms)
{
	struct bcmn_drv_cc *cc = bcm47xx_wdt_get_drvdata(wdt);
	u32 ticks;

	ticks = bcmn_chipco_watchdog_timer_set(cc, cc->ticks_per_ms * ms);
	return ticks / cc->ticks_per_ms;
}

static int bcmn_chipco_watchdog_ticks_per_ms(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	if (cc->capabilities & BCMN_CC_CAP_PMU) {
		if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4706)
			/* 4706 CC and PMU watchdogs are clocked at 1/4 of ALP
			 * clock
			 */
			return bcmn_chipco_get_alp_clock(cc) / 4000;
		else
			/* based on 32KHz ILP clock */
			return 32;
	} else {
		return bcmn_chipco_get_alp_clock(cc) / 1000;
	}
}

int bcmn_chipco_watchdog_register(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	struct bcm47xx_wdt wdt = {};
	struct platform_device *pdev;

	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM53573 &&
	    bus->chipinfo.rev <= 1) {
		pr_debug("No watchdog on 53573A0 / 53573A1\n");
		return 0;
	}

	wdt.driver_data = cc;
	wdt.timer_set = bcmn_chipco_watchdog_timer_set_wdt;
	wdt.timer_set_ms = bcmn_chipco_watchdog_timer_set_ms_wdt;
	wdt.max_timer_ms =
		bcmn_chipco_watchdog_get_max_timer(cc) / cc->ticks_per_ms;

	pdev = platform_device_register_data(NULL, "bcm47xx-wdt",
					     bus->num, &wdt,
					     sizeof(wdt));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	cc->watchdog = pdev;

	return 0;
}

static void bcmn_core_chipcommon_flash_detect(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	switch (cc->capabilities & BCMN_CC_CAP_FLASHT) {
	case BCMN_CC_FLASHT_STSER:
	case BCMN_CC_FLASHT_ATSER:
		bcmn_debug(bus, "Found serial flash\n");
		bcmn_sflash_init(cc);
		break;
	case BCMN_CC_FLASHT_PARA:
		bcmn_debug(bus, "Found parallel flash\n");
		bcmn_pflash_init(cc);
		break;
	default:
		bcmn_err(bus, "Flash type not supported\n");
	}

	if (cc->core->id.rev == 38 ||
	    bus->chipinfo.id == BCMN_CHIP_ID_BCM4706) {
		if (cc->capabilities & BCMN_CC_CAP_NFLASH) {
			bcmn_debug(bus, "Found NAND flash\n");
			bcmn_nflash_init(cc);
		}
	}
}

void bcmn_core_chipcommon_early_init(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	if (cc->early_setup_done)
		return;

	spin_lock_init(&cc->gpio_lock);

	if (cc->core->id.rev >= 11)
		cc->status = bcmn_cc_read32(cc, BCMN_CC_CHIPSTAT);
	cc->capabilities = bcmn_cc_read32(cc, BCMN_CC_CAP);
	if (cc->core->id.rev >= 35)
		cc->capabilities_ext = bcmn_cc_read32(cc, BCMN_CC_CAP_EXT);

	if (cc->capabilities & BCMN_CC_CAP_PMU)
		bcmn_pmu_early_init(cc);

	if (bus->hosttype == BCMN_HOSTTYPE_SOC)
		bcmn_core_chipcommon_flash_detect(cc);

	cc->early_setup_done = true;
}

void bcmn_core_chipcommon_init(struct bcmn_drv_cc *cc)
{
	u32 leddc_on = 10;
	u32 leddc_off = 90;

	if (cc->setup_done)
		return;

	bcmn_core_chipcommon_early_init(cc);

	if (cc->core->id.rev >= 20) {
		u32 pullup = 0, pulldown = 0;

		if (cc->core->bus->chipinfo.id == BCMN_CHIP_ID_BCM43142) {
			pullup = 0x402e0;
			pulldown = 0x20500;
		}

		bcmn_cc_write32(cc, BCMN_CC_GPIOPULLUP, pullup);
		bcmn_cc_write32(cc, BCMN_CC_GPIOPULLDOWN, pulldown);
	}

	if (cc->capabilities & BCMN_CC_CAP_PMU)
		bcmn_pmu_init(cc);
	if (cc->capabilities & BCMN_CC_CAP_PCTL)
		bcmn_err(cc->core->bus, "Power control not implemented!\n");

	if (cc->core->id.rev >= 16) {
		if (cc->core->bus->sprom.leddc_on_time &&
		    cc->core->bus->sprom.leddc_off_time) {
			leddc_on = cc->core->bus->sprom.leddc_on_time;
			leddc_off = cc->core->bus->sprom.leddc_off_time;
		}
		bcmn_cc_write32(cc, BCMN_CC_GPIOTIMER,
			((leddc_on << BCMN_CC_GPIOTIMER_ONTIME_SHIFT) |
			 (leddc_off << BCMN_CC_GPIOTIMER_OFFTIME_SHIFT)));
	}

	/*
	 * Bits 5:2 of corecontrol, which the stock driver sets next to the
	 * GPIO timer: wl 6.30.223 writes 0x3c on the Archer T5E, wl 6.30.102.7
	 * pulses the bits low and back high on the DSL-3580L. Bit 3 is
	 * UARTCLKEN; bits 2, 4 and 5 are not named here.
	 */
	if (cc->core->bus->chipinfo.id == BCMN_CHIP_ID_BCM4352 ||
	    cc->core->bus->chipinfo.id == BCMN_CHIP_ID_BCM4360)
		bcmn_cc_set32(cc, BCMN_CC_CORECTL, 0x3c);
	cc->ticks_per_ms = bcmn_chipco_watchdog_ticks_per_ms(cc);

	cc->setup_done = true;
}

/* Set chip watchdog reset timer to fire in 'ticks' backplane cycles */
u32 bcmn_chipco_watchdog_timer_set(struct bcmn_drv_cc *cc, u32 ticks)
{
	u32 maxt;

	maxt = bcmn_chipco_watchdog_get_max_timer(cc);
	if (bcmn_core_cc_has_pmu_watchdog(cc)) {
		if (ticks == 1)
			ticks = 2;
		else if (ticks > maxt)
			ticks = maxt;
		bcmn_pmu_write32(cc, BCMN_CC_PMU_WATCHDOG, ticks);
	} else {
		struct bcmn_bus *bus = cc->core->bus;

		if (bus->chipinfo.id != BCMN_CHIP_ID_BCM4707 &&
		    bus->chipinfo.id != BCMN_CHIP_ID_BCM47094 &&
		    bus->chipinfo.id != BCMN_CHIP_ID_BCM53018)
			bcmn_core_set_clockmode(cc->core,
						ticks ? BCMN_CLKMODE_FAST : BCMN_CLKMODE_DYNAMIC);

		if (ticks > maxt)
			ticks = maxt;
		/* instant NMI */
		bcmn_cc_write32(cc, BCMN_CC_WATCHDOG, ticks);
	}
	return ticks;
}

void bcmn_chipco_irq_mask(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	bcmn_cc_write32_masked(cc, BCMN_CC_IRQMASK, mask, value);
}

u32 bcmn_chipco_irq_status(struct bcmn_drv_cc *cc, u32 mask)
{
	return bcmn_cc_read32(cc, BCMN_CC_IRQSTAT) & mask;
}

u32 bcmn_chipco_gpio_in(struct bcmn_drv_cc *cc, u32 mask)
{
	return bcmn_cc_read32(cc, BCMN_CC_GPIOIN) & mask;
}

u32 bcmn_chipco_gpio_out(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOOUT, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL_GPL(bcmn_chipco_gpio_out);

u32 bcmn_chipco_gpio_outen(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOOUTEN, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL_GPL(bcmn_chipco_gpio_outen);

/*
 * If the bit is set to 0, chipcommon controls this GPIO,
 * if the bit is set to 1, it is used by some part of the chip and not our code.
 */
u32 bcmn_chipco_gpio_control(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOCTL, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL_GPL(bcmn_chipco_gpio_control);

u32 bcmn_chipco_gpio_intmask(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOIRQ, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 bcmn_chipco_gpio_polarity(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOPOL, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 bcmn_chipco_gpio_pullup(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	if (cc->core->id.rev < 20)
		return 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOPULLUP, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 bcmn_chipco_gpio_pulldown(struct bcmn_drv_cc *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res;

	if (cc->core->id.rev < 20)
		return 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = bcmn_cc_write32_masked(cc, BCMN_CC_GPIOPULLDOWN, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

#ifdef CONFIG_BCMN_DRIVER_MIPS
void bcmn_chipco_serial_init(struct bcmn_drv_cc *cc)
{
	unsigned int irq;
	u32 baud_base;
	u32 i;
	unsigned int ccrev = cc->core->id.rev;
	struct bcmn_serial_port *ports = cc->serial_ports;

	if (ccrev >= 11 && ccrev != 15) {
		baud_base = bcmn_chipco_get_alp_clock(cc);
		if (ccrev >= 21) {
			/* Turn off UART clock before switching clocksource. */
			bcmn_cc_write32(cc, BCMN_CC_CORECTL,
				       bcmn_cc_read32(cc, BCMN_CC_CORECTL)
				       & ~BCMN_CC_CORECTL_UARTCLKEN);
		}
		/* Set the override bit so we don't divide it */
		bcmn_cc_write32(cc, BCMN_CC_CORECTL,
			       bcmn_cc_read32(cc, BCMN_CC_CORECTL)
			       | BCMN_CC_CORECTL_UARTCLK0);
		if (ccrev >= 21) {
			/* Re-enable the UART clock. */
			bcmn_cc_write32(cc, BCMN_CC_CORECTL,
				       bcmn_cc_read32(cc, BCMN_CC_CORECTL)
				       | BCMN_CC_CORECTL_UARTCLKEN);
		}
	} else {
		bcmn_err(cc->core->bus, "serial not supported on this device ccrev: 0x%x\n",
			 ccrev);
		return;
	}

	irq = bcmn_core_irq(cc->core, 0);

	/* Determine the registers of the UARTs */
	cc->nr_serial_ports = (cc->capabilities & BCMN_CC_CAP_NRUART);
	for (i = 0; i < cc->nr_serial_ports; i++) {
		ports[i].regs = cc->core->io_addr + BCMN_CC_UART0_DATA +
				(i * 256);
		ports[i].irq = irq;
		ports[i].baud_base = baud_base;
		ports[i].reg_shift = 0;
	}
}
#endif /* CONFIG_BCMN_DRIVER_MIPS */
