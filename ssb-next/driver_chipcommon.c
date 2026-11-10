/*
 * Sonics Silicon Backplane
 * Broadcom ChipCommon core driver
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 * Copyright 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssbn_private.h"

#include <ssb-next/ssbn.h>
#include <ssb-next/ssbn_regs.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/bcm47xx_wdt.h>


/* Clock sources */
enum ssbn_clksrc {
	/* PCI clock */
	SSBN_CHIPCO_CLKSRC_PCI,
	/* Crystal slow clock oscillator */
	SSBN_CHIPCO_CLKSRC_XTALOS,
	/* Low power oscillator */
	SSBN_CHIPCO_CLKSRC_LOPWROS,
};


static inline u32 chipco_write32_masked(struct ssbn_chipcommon *cc, u16 offset,
					u32 mask, u32 value)
{
	value &= mask;
	value |= chipco_read32(cc, offset) & ~mask;
	chipco_write32(cc, offset, value);

	return value;
}

void ssbn_chipco_set_clockmode(struct ssbn_chipcommon *cc,
			      enum ssbn_clkmode mode)
{
	struct ssbn_device *ccdev = cc->dev;
	struct ssbn_bus *bus;
	u32 tmp;

	if (!ccdev)
		return;
	bus = ccdev->bus;

	/* We support SLOW only on 6..9 */
	if (ccdev->id.revision >= 10 && mode == SSBN_CLKMODE_SLOW)
		mode = SSBN_CLKMODE_DYNAMIC;

	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU)
		return; /* PMU controls clockmode, separated function needed */
	WARN_ON(ccdev->id.revision >= 20);

	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (ccdev->id.revision < 6)
		return;

	/* ChipCommon cores rev10+ need testing */
	if (ccdev->id.revision >= 10)
		return;

	if (!(cc->capabilities & SSBN_CHIPCO_CAP_PCTL))
		return;

	switch (mode) {
	case SSBN_CLKMODE_SLOW: /* For revs 6..9 only */
		tmp = chipco_read32(cc, SSBN_CHIPCO_SLOWCLKCTL);
		tmp |= SSBN_CHIPCO_SLOWCLKCTL_FSLOW;
		chipco_write32(cc, SSBN_CHIPCO_SLOWCLKCTL, tmp);
		break;
	case SSBN_CLKMODE_FAST:
		if (ccdev->id.revision < 10) {
			ssbn_pci_xtal(bus, SSBN_GPIO_XTAL, 1); /* Force crystal on */
			tmp = chipco_read32(cc, SSBN_CHIPCO_SLOWCLKCTL);
			tmp &= ~SSBN_CHIPCO_SLOWCLKCTL_FSLOW;
			tmp |= SSBN_CHIPCO_SLOWCLKCTL_IPLL;
			chipco_write32(cc, SSBN_CHIPCO_SLOWCLKCTL, tmp);
		} else {
			chipco_write32(cc, SSBN_CHIPCO_SYSCLKCTL,
				(chipco_read32(cc, SSBN_CHIPCO_SYSCLKCTL) |
				 SSBN_CHIPCO_SYSCLKCTL_FORCEHT));
			/* udelay(150); TODO: not available in early init */
		}
		break;
	case SSBN_CLKMODE_DYNAMIC:
		if (ccdev->id.revision < 10) {
			tmp = chipco_read32(cc, SSBN_CHIPCO_SLOWCLKCTL);
			tmp &= ~SSBN_CHIPCO_SLOWCLKCTL_FSLOW;
			tmp &= ~SSBN_CHIPCO_SLOWCLKCTL_IPLL;
			tmp &= ~SSBN_CHIPCO_SLOWCLKCTL_ENXTAL;
			if ((tmp & SSBN_CHIPCO_SLOWCLKCTL_SRC) !=
			    SSBN_CHIPCO_SLOWCLKCTL_SRC_XTAL)
				tmp |= SSBN_CHIPCO_SLOWCLKCTL_ENXTAL;
			chipco_write32(cc, SSBN_CHIPCO_SLOWCLKCTL, tmp);

			/* For dynamic control, we have to release our xtal_pu
			 * "force on" */
			if (tmp & SSBN_CHIPCO_SLOWCLKCTL_ENXTAL)
				ssbn_pci_xtal(bus, SSBN_GPIO_XTAL, 0);
		} else {
			chipco_write32(cc, SSBN_CHIPCO_SYSCLKCTL,
				(chipco_read32(cc, SSBN_CHIPCO_SYSCLKCTL) &
				 ~SSBN_CHIPCO_SYSCLKCTL_FORCEHT));
		}
		break;
	default:
		WARN_ON(1);
	}
}

/* Get the Slow Clock Source */
static enum ssbn_clksrc chipco_pctl_get_slowclksrc(struct ssbn_chipcommon *cc)
{
	struct ssbn_bus *bus = cc->dev->bus;
	u32 tmp;

	if (cc->dev->id.revision < 6) {
		if (bus->bustype == SSBN_BUSTYPE_SSB ||
		    bus->bustype == SSBN_BUSTYPE_PCMCIA)
			return SSBN_CHIPCO_CLKSRC_XTALOS;
		if (bus->bustype == SSBN_BUSTYPE_PCI) {
			pci_read_config_dword(bus->host_pci, SSBN_GPIO_OUT, &tmp);
			if (tmp & 0x10)
				return SSBN_CHIPCO_CLKSRC_PCI;
			return SSBN_CHIPCO_CLKSRC_XTALOS;
		}
	}
	if (cc->dev->id.revision < 10) {
		tmp = chipco_read32(cc, SSBN_CHIPCO_SLOWCLKCTL);
		tmp &= 0x7;
		if (tmp == 0)
			return SSBN_CHIPCO_CLKSRC_LOPWROS;
		if (tmp == 1)
			return SSBN_CHIPCO_CLKSRC_XTALOS;
		if (tmp == 2)
			return SSBN_CHIPCO_CLKSRC_PCI;
	}

	return SSBN_CHIPCO_CLKSRC_XTALOS;
}

/* Get maximum or minimum (depending on get_max flag) slowclock frequency. */
static int chipco_pctl_clockfreqlimit(struct ssbn_chipcommon *cc, int get_max)
{
	int limit;
	enum ssbn_clksrc clocksrc;
	int divisor = 1;
	u32 tmp;

	clocksrc = chipco_pctl_get_slowclksrc(cc);
	if (cc->dev->id.revision < 6) {
		switch (clocksrc) {
		case SSBN_CHIPCO_CLKSRC_PCI:
			divisor = 64;
			break;
		case SSBN_CHIPCO_CLKSRC_XTALOS:
			divisor = 32;
			break;
		default:
			WARN_ON(1);
		}
	} else if (cc->dev->id.revision < 10) {
		switch (clocksrc) {
		case SSBN_CHIPCO_CLKSRC_LOPWROS:
			break;
		case SSBN_CHIPCO_CLKSRC_XTALOS:
		case SSBN_CHIPCO_CLKSRC_PCI:
			tmp = chipco_read32(cc, SSBN_CHIPCO_SLOWCLKCTL);
			divisor = (tmp >> 16) + 1;
			divisor *= 4;
			break;
		}
	} else {
		tmp = chipco_read32(cc, SSBN_CHIPCO_SYSCLKCTL);
		divisor = (tmp >> 16) + 1;
		divisor *= 4;
	}

	switch (clocksrc) {
	case SSBN_CHIPCO_CLKSRC_LOPWROS:
		if (get_max)
			limit = 43000;
		else
			limit = 25000;
		break;
	case SSBN_CHIPCO_CLKSRC_XTALOS:
		if (get_max)
			limit = 20200000;
		else
			limit = 19800000;
		break;
	case SSBN_CHIPCO_CLKSRC_PCI:
		if (get_max)
			limit = 34000000;
		else
			limit = 25000000;
		break;
	}
	limit /= divisor;

	return limit;
}

static void chipco_powercontrol_init(struct ssbn_chipcommon *cc)
{
	struct ssbn_bus *bus = cc->dev->bus;

	if (bus->chip_id == 0x4321) {
		if (bus->chip_rev == 0)
			chipco_write32(cc, SSBN_CHIPCO_CHIPCTL, 0x3A4);
		else if (bus->chip_rev == 1)
			chipco_write32(cc, SSBN_CHIPCO_CHIPCTL, 0xA4);
	}

	if (!(cc->capabilities & SSBN_CHIPCO_CAP_PCTL))
		return;

	if (cc->dev->id.revision >= 10) {
		/* Set Idle Power clock rate to 1Mhz */
		chipco_write32(cc, SSBN_CHIPCO_SYSCLKCTL,
			       (chipco_read32(cc, SSBN_CHIPCO_SYSCLKCTL) &
				0x0000FFFF) | 0x00040000);
	} else {
		int maxfreq;

		maxfreq = chipco_pctl_clockfreqlimit(cc, 1);
		chipco_write32(cc, SSBN_CHIPCO_PLLONDELAY,
			       (maxfreq * 150 + 999999) / 1000000);
		chipco_write32(cc, SSBN_CHIPCO_FREFSELDELAY,
			       (maxfreq * 15 + 999999) / 1000000);
	}
}

/* https://bcm-v4.sipsolutions.net/802.11/PmuFastPwrupDelay */
static u16 pmu_fast_powerup_delay(struct ssbn_chipcommon *cc)
{
	struct ssbn_bus *bus = cc->dev->bus;

	switch (bus->chip_id) {
	case 0x4312:
	case 0x4322:
	case 0x4328:
		return 7000;
	case 0x4325:
		/* TODO: */
	default:
		return 15000;
	}
}

/* https://bcm-v4.sipsolutions.net/802.11/ClkctlFastPwrupDelay */
static void calc_fast_powerup_delay(struct ssbn_chipcommon *cc)
{
	struct ssbn_bus *bus = cc->dev->bus;
	int minfreq;
	unsigned int tmp;
	u32 pll_on_delay;

	if (bus->bustype != SSBN_BUSTYPE_PCI)
		return;

	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU) {
		cc->fast_pwrup_delay = pmu_fast_powerup_delay(cc);
		return;
	}

	if (!(cc->capabilities & SSBN_CHIPCO_CAP_PCTL))
		return;

	minfreq = chipco_pctl_clockfreqlimit(cc, 0);
	pll_on_delay = chipco_read32(cc, SSBN_CHIPCO_PLLONDELAY);
	tmp = (((pll_on_delay + 2) * 1000000) + (minfreq - 1)) / minfreq;
	WARN_ON(tmp & ~0xFFFF);

	cc->fast_pwrup_delay = tmp;
}

static u32 ssbn_chipco_alp_clock(struct ssbn_chipcommon *cc)
{
	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU)
		return ssbn_pmu_get_alp_clock(cc);

	return 20000000;
}

static u32 ssbn_chipco_watchdog_get_max_timer(struct ssbn_chipcommon *cc)
{
	u32 nb;

	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU) {
		if (cc->dev->id.revision < 26)
			nb = 16;
		else
			nb = (cc->dev->id.revision >= 37) ? 32 : 24;
	} else {
		nb = 28;
	}
	if (nb == 32)
		return 0xffffffff;
	else
		return (1 << nb) - 1;
}

u32 ssbn_chipco_watchdog_timer_set_wdt(struct bcm47xx_wdt *wdt, u32 ticks)
{
	struct ssbn_chipcommon *cc = bcm47xx_wdt_get_drvdata(wdt);

	if (cc->dev->bus->bustype != SSBN_BUSTYPE_SSB)
		return 0;

	return ssbn_chipco_watchdog_timer_set(cc, ticks);
}

u32 ssbn_chipco_watchdog_timer_set_ms(struct bcm47xx_wdt *wdt, u32 ms)
{
	struct ssbn_chipcommon *cc = bcm47xx_wdt_get_drvdata(wdt);
	u32 ticks;

	if (cc->dev->bus->bustype != SSBN_BUSTYPE_SSB)
		return 0;

	ticks = ssbn_chipco_watchdog_timer_set(cc, cc->ticks_per_ms * ms);
	return ticks / cc->ticks_per_ms;
}

static int ssbn_chipco_watchdog_ticks_per_ms(struct ssbn_chipcommon *cc)
{
	struct ssbn_bus *bus = cc->dev->bus;

	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU) {
			/* based on 32KHz ILP clock */
			return 32;
	} else {
		if (cc->dev->id.revision < 18)
			return ssbn_clockspeed(bus) / 1000;
		else
			return ssbn_chipco_alp_clock(cc) / 1000;
	}
}

void ssbn_chipcommon_init(struct ssbn_chipcommon *cc)
{
	if (!cc->dev)
		return; /* We don't have a ChipCommon */

	spin_lock_init(&cc->gpio_lock);

	if (cc->dev->id.revision >= 11)
		cc->status = chipco_read32(cc, SSBN_CHIPCO_CHIPSTAT);
	dev_dbg(cc->dev->dev, "chipcommon status is 0x%x\n", cc->status);

	if (cc->dev->id.revision >= 20) {
		chipco_write32(cc, SSBN_CHIPCO_GPIOPULLUP, 0);
		chipco_write32(cc, SSBN_CHIPCO_GPIOPULLDOWN, 0);
	}

	ssbn_pmu_init(cc);
	chipco_powercontrol_init(cc);
	ssbn_chipco_set_clockmode(cc, SSBN_CLKMODE_FAST);
	calc_fast_powerup_delay(cc);

	if (cc->dev->bus->bustype == SSBN_BUSTYPE_SSB) {
		cc->ticks_per_ms = ssbn_chipco_watchdog_ticks_per_ms(cc);
		cc->max_timer_ms = ssbn_chipco_watchdog_get_max_timer(cc) / cc->ticks_per_ms;
	}
}

void ssbn_chipco_suspend(struct ssbn_chipcommon *cc)
{
	if (!cc->dev)
		return;
	ssbn_chipco_set_clockmode(cc, SSBN_CLKMODE_SLOW);
}

void ssbn_chipco_resume(struct ssbn_chipcommon *cc)
{
	if (!cc->dev)
		return;
	chipco_powercontrol_init(cc);
	ssbn_chipco_set_clockmode(cc, SSBN_CLKMODE_FAST);
}

/* Get the processor clock */
void ssbn_chipco_get_clockcpu(struct ssbn_chipcommon *cc,
                             u32 *plltype, u32 *n, u32 *m)
{
	*n = chipco_read32(cc, SSBN_CHIPCO_CLOCK_N);
	*plltype = (cc->capabilities & SSBN_CHIPCO_CAP_PLLT);
	switch (*plltype) {
	case SSBN_PLLTYPE_2:
	case SSBN_PLLTYPE_4:
	case SSBN_PLLTYPE_6:
	case SSBN_PLLTYPE_7:
		*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_MIPS);
		break;
	case SSBN_PLLTYPE_3:
		/* 5350 uses m2 to control mips */
		*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_M2);
		break;
	default:
		*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_SB);
		break;
	}
}

/* Get the bus clock */
void ssbn_chipco_get_clockcontrol(struct ssbn_chipcommon *cc,
				 u32 *plltype, u32 *n, u32 *m)
{
	*n = chipco_read32(cc, SSBN_CHIPCO_CLOCK_N);
	*plltype = (cc->capabilities & SSBN_CHIPCO_CAP_PLLT);
	switch (*plltype) {
	case SSBN_PLLTYPE_6: /* 100/200 or 120/240 only */
		*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_MIPS);
		break;
	case SSBN_PLLTYPE_3: /* 25Mhz, 2 dividers */
		if (cc->dev->bus->chip_id != 0x5365) {
			*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_M2);
			break;
		}
		fallthrough;
	default:
		*m = chipco_read32(cc, SSBN_CHIPCO_CLOCK_SB);
	}
}

void ssbn_chipco_timing_init(struct ssbn_chipcommon *cc,
			    unsigned long ns)
{
	struct ssbn_device *dev = cc->dev;
	struct ssbn_bus *bus = dev->bus;
	u32 tmp;

	/* set register for external IO to control LED. */
	chipco_write32(cc, SSBN_CHIPCO_PROG_CFG, 0x11);
	tmp = DIV_ROUND_UP(10, ns) << SSBN_PROG_WCNT_3_SHIFT;		/* Waitcount-3 = 10ns */
	tmp |= DIV_ROUND_UP(40, ns) << SSBN_PROG_WCNT_1_SHIFT;	/* Waitcount-1 = 40ns */
	tmp |= DIV_ROUND_UP(240, ns);				/* Waitcount-0 = 240ns */
	chipco_write32(cc, SSBN_CHIPCO_PROG_WAITCNT, tmp);	/* 0x01020a0c for a 100Mhz clock */

	/* Set timing for the flash */
	tmp = DIV_ROUND_UP(10, ns) << SSBN_FLASH_WCNT_3_SHIFT;	/* Waitcount-3 = 10nS */
	tmp |= DIV_ROUND_UP(10, ns) << SSBN_FLASH_WCNT_1_SHIFT;	/* Waitcount-1 = 10nS */
	tmp |= DIV_ROUND_UP(120, ns);				/* Waitcount-0 = 120nS */
	if ((bus->chip_id == 0x5365) ||
	    (dev->id.revision < 9))
		chipco_write32(cc, SSBN_CHIPCO_FLASH_WAITCNT, tmp);
	if ((bus->chip_id == 0x5365) ||
	    (dev->id.revision < 9) ||
	    ((bus->chip_id == 0x5350) && (bus->chip_rev == 0)))
		chipco_write32(cc, SSBN_CHIPCO_PCMCIA_MEMWAIT, tmp);

	if (bus->chip_id == 0x5350) {
		/* Enable EXTIF */
		tmp = DIV_ROUND_UP(10, ns) << SSBN_PROG_WCNT_3_SHIFT;	  /* Waitcount-3 = 10ns */
		tmp |= DIV_ROUND_UP(20, ns) << SSBN_PROG_WCNT_2_SHIFT;  /* Waitcount-2 = 20ns */
		tmp |= DIV_ROUND_UP(100, ns) << SSBN_PROG_WCNT_1_SHIFT; /* Waitcount-1 = 100ns */
		tmp |= DIV_ROUND_UP(120, ns);			  /* Waitcount-0 = 120ns */
		chipco_write32(cc, SSBN_CHIPCO_PROG_WAITCNT, tmp); /* 0x01020a0c for a 100Mhz clock */
	}
}

/* Set chip watchdog reset timer to fire in 'ticks' backplane cycles */
u32 ssbn_chipco_watchdog_timer_set(struct ssbn_chipcommon *cc, u32 ticks)
{
	u32 maxt;
	enum ssbn_clkmode clkmode;

	maxt = ssbn_chipco_watchdog_get_max_timer(cc);
	if (cc->capabilities & SSBN_CHIPCO_CAP_PMU) {
		if (ticks == 1)
			ticks = 2;
		else if (ticks > maxt)
			ticks = maxt;
		chipco_write32(cc, SSBN_CHIPCO_PMU_WATCHDOG, ticks);
	} else {
		clkmode = ticks ? SSBN_CLKMODE_FAST : SSBN_CLKMODE_DYNAMIC;
		ssbn_chipco_set_clockmode(cc, clkmode);
		if (ticks > maxt)
			ticks = maxt;
		/* instant NMI */
		chipco_write32(cc, SSBN_CHIPCO_WATCHDOG, ticks);
	}
	return ticks;
}

void ssbn_chipco_irq_mask(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	chipco_write32_masked(cc, SSBN_CHIPCO_IRQMASK, mask, value);
}

u32 ssbn_chipco_irq_status(struct ssbn_chipcommon *cc, u32 mask)
{
	return chipco_read32(cc, SSBN_CHIPCO_IRQSTAT) & mask;
}

u32 ssbn_chipco_gpio_in(struct ssbn_chipcommon *cc, u32 mask)
{
	return chipco_read32(cc, SSBN_CHIPCO_GPIOIN) & mask;
}

u32 ssbn_chipco_gpio_out(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOOUT, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 ssbn_chipco_gpio_outen(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOOUTEN, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 ssbn_chipco_gpio_control(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOCTL, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}
EXPORT_SYMBOL(ssbn_chipco_gpio_control);

u32 ssbn_chipco_gpio_intmask(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOIRQ, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 ssbn_chipco_gpio_polarity(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOPOL, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 ssbn_chipco_gpio_pullup(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	if (cc->dev->id.revision < 20)
		return 0xffffffff;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOPULLUP, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

u32 ssbn_chipco_gpio_pulldown(struct ssbn_chipcommon *cc, u32 mask, u32 value)
{
	unsigned long flags;
	u32 res = 0;

	if (cc->dev->id.revision < 20)
		return 0xffffffff;

	spin_lock_irqsave(&cc->gpio_lock, flags);
	res = chipco_write32_masked(cc, SSBN_CHIPCO_GPIOPULLDOWN, mask, value);
	spin_unlock_irqrestore(&cc->gpio_lock, flags);

	return res;
}

#ifdef CONFIG_SSBN_SERIAL
int ssbn_chipco_serial_init(struct ssbn_chipcommon *cc,
			   struct ssbn_serial_port *ports)
{
	struct ssbn_bus *bus = cc->dev->bus;
	int nr_ports = 0;
	u32 plltype;
	unsigned int irq;
	u32 baud_base, div;
	u32 i, n;
	unsigned int ccrev = cc->dev->id.revision;

	plltype = (cc->capabilities & SSBN_CHIPCO_CAP_PLLT);
	irq = ssbn_mips_irq(cc->dev);

	if (plltype == SSBN_PLLTYPE_1) {
		/* PLL clock */
		baud_base = ssbn_calc_clock_rate(plltype,
						chipco_read32(cc, SSBN_CHIPCO_CLOCK_N),
						chipco_read32(cc, SSBN_CHIPCO_CLOCK_M2));
		div = 1;
	} else {
		if (ccrev == 20) {
			/* BCM5354 uses constant 25MHz clock */
			baud_base = 25000000;
			div = 48;
			/* Set the override bit so we don't divide it */
			chipco_write32(cc, SSBN_CHIPCO_CORECTL,
				       chipco_read32(cc, SSBN_CHIPCO_CORECTL)
				       | SSBN_CHIPCO_CORECTL_UARTCLK0);
		} else if ((ccrev >= 11) && (ccrev != 15)) {
			baud_base = ssbn_chipco_alp_clock(cc);
			div = 1;
			if (ccrev >= 21) {
				/* Turn off UART clock before switching clocksource. */
				chipco_write32(cc, SSBN_CHIPCO_CORECTL,
					       chipco_read32(cc, SSBN_CHIPCO_CORECTL)
					       & ~SSBN_CHIPCO_CORECTL_UARTCLKEN);
			}
			/* Set the override bit so we don't divide it */
			chipco_write32(cc, SSBN_CHIPCO_CORECTL,
				       chipco_read32(cc, SSBN_CHIPCO_CORECTL)
				       | SSBN_CHIPCO_CORECTL_UARTCLK0);
			if (ccrev >= 21) {
				/* Re-enable the UART clock. */
				chipco_write32(cc, SSBN_CHIPCO_CORECTL,
					       chipco_read32(cc, SSBN_CHIPCO_CORECTL)
					       | SSBN_CHIPCO_CORECTL_UARTCLKEN);
			}
		} else if (ccrev >= 3) {
			/* Internal backplane clock */
			baud_base = ssbn_clockspeed(bus);
			div = chipco_read32(cc, SSBN_CHIPCO_CLKDIV)
			      & SSBN_CHIPCO_CLKDIV_UART;
		} else {
			/* Fixed internal backplane clock */
			baud_base = 88000000;
			div = 48;
		}

		/* Clock source depends on strapping if UartClkOverride is unset */
		if ((ccrev > 0) &&
		    !(chipco_read32(cc, SSBN_CHIPCO_CORECTL) & SSBN_CHIPCO_CORECTL_UARTCLK0)) {
			if ((cc->capabilities & SSBN_CHIPCO_CAP_UARTCLK) ==
			    SSBN_CHIPCO_CAP_UARTCLK_INT) {
				/* Internal divided backplane clock */
				baud_base /= div;
			} else {
				/* Assume external clock of 1.8432 MHz */
				baud_base = 1843200;
			}
		}
	}

	/* Determine the registers of the UARTs */
	n = (cc->capabilities & SSBN_CHIPCO_CAP_NRUART);
	for (i = 0; i < n; i++) {
		void __iomem *cc_mmio;
		void __iomem *uart_regs;

		cc_mmio = cc->dev->bus->mmio + (cc->dev->core_index * SSBN_CORE_SIZE);
		uart_regs = cc_mmio + SSBN_CHIPCO_UART0_DATA;
		/* Offset changed at after rev 0 */
		if (ccrev == 0)
			uart_regs += (i * 8);
		else
			uart_regs += (i * 256);

		nr_ports++;
		ports[i].regs = uart_regs;
		ports[i].irq = irq;
		ports[i].baud_base = baud_base;
		ports[i].reg_shift = 0;
	}

	return nr_ports;
}
#endif /* CONFIG_SSBN_SERIAL */
