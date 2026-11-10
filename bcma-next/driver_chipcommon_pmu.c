/*
 * Broadcom specific AMBA
 * ChipCommon Power Management Unit driver
 *
 * Copyright 2009, Michael Buesch <m@bues.ch>
 * Copyright 2007, 2011, Broadcom Corporation
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/export.h>
#include <bcma-next/bcmn.h>

u32 bcmn_chipco_pll_read(struct bcmn_drv_cc *cc, u32 offset)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR, offset);
	bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_ADDR);
	return bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_DATA);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_pll_read);

void bcmn_chipco_pll_write(struct bcmn_drv_cc *cc, u32 offset, u32 value)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR, offset);
	bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_ADDR);
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_DATA, value);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_pll_write);

void bcmn_chipco_pll_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
			     u32 set)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR, offset);
	bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_ADDR);
	bcmn_pmu_maskset32(cc, BCMN_CC_PMU_PLLCTL_DATA, mask, set);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_pll_maskset);

void bcmn_chipco_chipctl_maskset(struct bcmn_drv_cc *cc,
				 u32 offset, u32 mask, u32 set)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_CHIPCTL_ADDR, offset);
	bcmn_pmu_read32(cc, BCMN_CC_PMU_CHIPCTL_ADDR);
	bcmn_pmu_maskset32(cc, BCMN_CC_PMU_CHIPCTL_DATA, mask, set);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_chipctl_maskset);

void bcmn_chipco_regctl_maskset(struct bcmn_drv_cc *cc, u32 offset, u32 mask,
				u32 set)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_REGCTL_ADDR, offset);
	bcmn_pmu_read32(cc, BCMN_CC_PMU_REGCTL_ADDR);
	bcmn_pmu_maskset32(cc, BCMN_CC_PMU_REGCTL_DATA, mask, set);
}
EXPORT_SYMBOL_GPL(bcmn_chipco_regctl_maskset);

static u32 bcmn_pmu_xtalfreq(struct bcmn_drv_cc *cc)
{
	u32 ilp_ctl, alp_hz;

	if (!(bcmn_pmu_read32(cc, BCMN_CC_PMU_STAT) &
	      BCMN_CC_PMU_STAT_EXT_LPO_AVAIL))
		return 0;

	bcmn_pmu_write32(cc, BCMN_CC_PMU_XTAL_FREQ,
			 BIT(BCMN_CC_PMU_XTAL_FREQ_MEASURE_SHIFT));
	usleep_range(1000, 2000);

	ilp_ctl = bcmn_pmu_read32(cc, BCMN_CC_PMU_XTAL_FREQ);
	ilp_ctl &= BCMN_CC_PMU_XTAL_FREQ_ILPCTL_MASK;

	bcmn_pmu_write32(cc, BCMN_CC_PMU_XTAL_FREQ, 0);

	alp_hz = ilp_ctl * 32768 / 4;
	return (alp_hz + 50000) / 100000 * 100;
}

static void bcmn_pmu2_pll_init0(struct bcmn_drv_cc *cc, u32 xtalfreq)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 freq_tgt_target = 0, freq_tgt_current;
	u32 pll0, mask;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM43142:
		/* pmu2_xtaltab0_adfll_485 */
		switch (xtalfreq) {
		case 12000:
			freq_tgt_target = 0x50D52;
			break;
		case 20000:
			freq_tgt_target = 0x307FE;
			break;
		case 26000:
			freq_tgt_target = 0x254EA;
			break;
		case 37400:
			freq_tgt_target = 0x19EF8;
			break;
		case 52000:
			freq_tgt_target = 0x12A75;
			break;
		}
		break;
	}

	if (!freq_tgt_target) {
		bcmn_err(bus, "Unknown TGT frequency for xtalfreq %d\n",
			 xtalfreq);
		return;
	}

	pll0 = bcmn_chipco_pll_read(cc, BCMN_CC_PMU15_PLL_PLLCTL0);
	freq_tgt_current = (pll0 & BCMN_CC_PMU15_PLL_PC0_FREQTGT_MASK) >>
		BCMN_CC_PMU15_PLL_PC0_FREQTGT_SHIFT;

	if (freq_tgt_current == freq_tgt_target) {
		bcmn_debug(bus, "Target TGT frequency already set\n");
		return;
	}

	/* Turn off PLL */
	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM43142:
		mask = (u32)~(BCMN_RES_4314_HT_AVAIL |
			      BCMN_RES_4314_MACPHY_CLK_AVAIL);

		bcmn_pmu_mask32(cc, BCMN_CC_PMU_MINRES_MSK, mask);
		bcmn_pmu_mask32(cc, BCMN_CC_PMU_MAXRES_MSK, mask);
		bcmn_wait_value(cc->core, BCMN_CLKCTLST,
				BCMN_CLKCTLST_HAVEHT, 0, 20000);
		break;
	}

	pll0 &= ~BCMN_CC_PMU15_PLL_PC0_FREQTGT_MASK;
	pll0 |= freq_tgt_target << BCMN_CC_PMU15_PLL_PC0_FREQTGT_SHIFT;
	bcmn_chipco_pll_write(cc, BCMN_CC_PMU15_PLL_PLLCTL0, pll0);

	/* Flush */
	if (cc->pmu.rev >= 2)
		bcmn_pmu_set32(cc, BCMN_CC_PMU_CTL, BCMN_CC_PMU_CTL_PLL_UPD);

	/* TODO: Do we need to update OTP? */
}

/* BCM4352/BCM4360: only chip rev 3 has been captured. */
static void bcmn_pmu_4360_pll_init(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 ctl2, ctl3;

	if (bus->chipinfo.rev != 3)
		return;
	/* No capture has the USB strap. */
	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4360 &&
	    cc->status & BCMN_CC_CHIPST_4360_MODE_USB)
		return;

	/* Fractional VCO, 960.0098 MHz from a 40 MHz crystal. */
	ctl2 = bcmn_chipco_pll_read(cc, BCMN_CC_PMU_PLL_CTL2);
	ctl3 = bcmn_chipco_pll_read(cc, BCMN_CC_PMU_PLL_CTL3);
	bcmn_info(bus, "PMU PLL pre-write: ctl2=0x%08x ctl3=0x%08x\n",
		  ctl2, ctl3);
	bcmn_chipco_pll_write(cc, BCMN_CC_PMU_PLL_CTL2, 0x0c31);
	bcmn_chipco_pll_write(cc, BCMN_CC_PMU_PLL_CTL3, 0x100e);
	bcmn_pmu_set32(cc, BCMN_CC_PMU_CTL, BCMN_CC_PMU_CTL_PLL_UPD);
}

/* Returns the max resource mask to program, 0 to leave it alone. */
static u32 bcmn_pmu_4360_resources_init(struct bcmn_drv_cc *cc)
{
	if (cc->core->bus->chipinfo.rev != 3)
		return 0;

	bcmn_pmu_write32(cc, BCMN_CC_PMU_RES_TABSEL, 6);
	bcmn_pmu_write32(cc, BCMN_CC_PMU_RES_UPDNTM, 0x00200001);

	/* min_res_mask keeps its reset value in both cases. */
	if (cc->status & BCMN_CC_CHIPST_4360_RSRC_INIT_MODE0)
		return 0;
	bcmn_chipco_pll_write(cc, BCMN_CC_PMU_PLL_CTL6, 0x09048562);
	bcmn_chipco_pll_write(cc, BCMN_CC_PMU_PLL_CTL14, 0x09048562);
	bcmn_pmu_set32(cc, BCMN_CC_PMU_CTL, BCMN_CC_PMU_CTL_PLL_UPD);
	return 0x1ff;
}

static void bcmn_pmu_pll_init(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 xtalfreq = bcmn_pmu_xtalfreq(cc);

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM43142:
		if (xtalfreq == 0)
			xtalfreq = 20000;
		bcmn_pmu2_pll_init0(cc, xtalfreq);
		break;
	case BCMN_CHIP_ID_BCM4352:
	case BCMN_CHIP_ID_BCM4360:
		bcmn_pmu_4360_pll_init(cc);
		break;
	}
}

static void bcmn_pmu_resources_init(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 min_msk = 0, max_msk = 0;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4313:
		min_msk = 0x200D;
		max_msk = 0xFFFF;
		break;
	case BCMN_CHIP_ID_BCM43142:
		min_msk = BCMN_RES_4314_LPLDO_PU |
			  BCMN_RES_4314_PMU_SLEEP_DIS |
			  BCMN_RES_4314_PMU_BG_PU |
			  BCMN_RES_4314_CBUCK_LPOM_PU |
			  BCMN_RES_4314_CBUCK_PFM_PU |
			  BCMN_RES_4314_CLDO_PU |
			  BCMN_RES_4314_LPLDO2_LVM |
			  BCMN_RES_4314_WL_PMU_PU |
			  BCMN_RES_4314_LDO3P3_PU |
			  BCMN_RES_4314_OTP_PU |
			  BCMN_RES_4314_WL_PWRSW_PU |
			  BCMN_RES_4314_LQ_AVAIL |
			  BCMN_RES_4314_LOGIC_RET |
			  BCMN_RES_4314_MEM_SLEEP |
			  BCMN_RES_4314_MACPHY_RET |
			  BCMN_RES_4314_WL_CORE_READY;
		max_msk = 0x3FFFFFFF;
		break;
	case BCMN_CHIP_ID_BCM4352:
	case BCMN_CHIP_ID_BCM4360:
		max_msk = bcmn_pmu_4360_resources_init(cc);
		break;
	default:
		bcmn_debug(bus, "PMU resource config unknown or not needed for device 0x%04X\n",
			   bus->chipinfo.id);
	}

	bcmn_info(bus,
		  "PMU res mask pre-write: min=0x%08x max=0x%08x (writing min=0x%08x max=0x%08x)\n",
		  bcmn_pmu_read32(cc, BCMN_CC_PMU_MINRES_MSK),
		  bcmn_pmu_read32(cc, BCMN_CC_PMU_MAXRES_MSK),
		  min_msk, max_msk);

	/* Set the resource masks. */
	if (min_msk)
		bcmn_pmu_write32(cc, BCMN_CC_PMU_MINRES_MSK, min_msk);
	if (max_msk)
		bcmn_pmu_write32(cc, BCMN_CC_PMU_MAXRES_MSK, max_msk);

	/*
	 * Add some delay; allow resources to come up and settle.
	 * Delay is required for SoC (early init).
	 */
	usleep_range(2000, 2500);
}

/* Disable to allow reading SPROM. Don't know the advantages of enabling it. */
void bcmn_chipco_bcm4331_ext_pa_lines_ctl(struct bcmn_drv_cc *cc, bool enable)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 val;

	val = bcmn_cc_read32(cc, BCMN_CC_CHIPCTL);
	if (enable) {
		val |= BCMN_CHIPCTL_4331_EXTPA_EN;
		if (bus->chipinfo.pkg == 9 || bus->chipinfo.pkg == 11)
			val |= BCMN_CHIPCTL_4331_EXTPA_ON_GPIO2_5;
		else if (bus->chipinfo.rev > 0)
			val |= BCMN_CHIPCTL_4331_EXTPA_EN2;
	} else {
		val &= ~BCMN_CHIPCTL_4331_EXTPA_EN;
		val &= ~BCMN_CHIPCTL_4331_EXTPA_EN2;
		val &= ~BCMN_CHIPCTL_4331_EXTPA_ON_GPIO2_5;
	}
	bcmn_cc_write32(cc, BCMN_CC_CHIPCTL, val);
}

static void bcmn_pmu_workarounds(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4313:
		/*
		 * enable 12 mA drive strength for 4313 and set chipControl
		 * register bit 1
		 */
		bcmn_chipco_chipctl_maskset(cc, 0,
					    ~BCMN_CCTRL_4313_12MA_LED_DRIVE,
					    BCMN_CCTRL_4313_12MA_LED_DRIVE);
		break;
	case BCMN_CHIP_ID_BCM4331:
	case BCMN_CHIP_ID_BCM43431:
		/* Ext PA lines must be enabled for tx on BCM4331 */
		bcmn_chipco_bcm4331_ext_pa_lines_ctl(cc, true);
		break;
	case BCMN_CHIP_ID_BCM43224:
	case BCMN_CHIP_ID_BCM43421:
		/*
		 * enable 12 mA drive strength for 43224 and set chipControl
		 * register bit 15
		 */
		if (bus->chipinfo.rev == 0) {
			bcmn_cc_maskset32(cc, BCMN_CC_CHIPCTL,
					  ~BCMN_CCTRL_43224_GPIO_TOGGLE,
					  BCMN_CCTRL_43224_GPIO_TOGGLE);
			bcmn_chipco_chipctl_maskset(cc, 0,
						    ~BCMN_CCTRL_43224A0_12MA_LED_DRIVE,
						    BCMN_CCTRL_43224A0_12MA_LED_DRIVE);
		} else {
			bcmn_chipco_chipctl_maskset(cc, 0,
						    ~BCMN_CCTRL_43224B0_12MA_LED_DRIVE,
						    BCMN_CCTRL_43224B0_12MA_LED_DRIVE);
		}
		break;
	default:
		bcmn_debug(bus, "Workarounds unknown or not needed for device 0x%04X\n",
			   bus->chipinfo.id);
	}
}

void bcmn_pmu_early_init(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;
	u32 pmucap;

	if (cc->core->id.rev >= 35 &&
	    cc->capabilities_ext & BCMN_CC_CAP_EXT_AOB_PRESENT) {
		cc->pmu.core = bcmn_find_core(bus, BCMN_CORE_PMU);
		if (!cc->pmu.core)
			bcmn_warn(bus, "Couldn't find expected PMU core");
	}
	if (!cc->pmu.core)
		cc->pmu.core = cc->core;

	pmucap = bcmn_pmu_read32(cc, BCMN_CC_PMU_CAP);
	cc->pmu.rev = (pmucap & BCMN_CC_PMU_CAP_REVISION);

	bcmn_info(bus, "Found rev %u PMU (capabilities 0x%08X)\n", cc->pmu.rev,
		  pmucap);
}

void bcmn_pmu_init(struct bcmn_drv_cc *cc)
{
	if (cc->pmu.rev == 1)
		bcmn_pmu_mask32(cc, BCMN_CC_PMU_CTL,
				~BCMN_CC_PMU_CTL_NOILPONW);
	else
		bcmn_pmu_set32(cc, BCMN_CC_PMU_CTL,
			       BCMN_CC_PMU_CTL_NOILPONW);

	bcmn_pmu_pll_init(cc);
	bcmn_pmu_resources_init(cc);
	bcmn_pmu_workarounds(cc);
}

u32 bcmn_pmu_get_alp_clock(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4313:
	case BCMN_CHIP_ID_BCM43224:
	case BCMN_CHIP_ID_BCM43225:
	case BCMN_CHIP_ID_BCM43227:
	case BCMN_CHIP_ID_BCM43228:
	case BCMN_CHIP_ID_BCM4331:
	case BCMN_CHIP_ID_BCM43421:
	case BCMN_CHIP_ID_BCM43428:
	case BCMN_CHIP_ID_BCM43431:
	case BCMN_CHIP_ID_BCM4716:
	case BCMN_CHIP_ID_BCM47162:
	case BCMN_CHIP_ID_BCM4748:
	case BCMN_CHIP_ID_BCM4749:
	case BCMN_CHIP_ID_BCM5357:
	case BCMN_CHIP_ID_BCM53572:
	case BCMN_CHIP_ID_BCM6362:
		/* always 20Mhz */
		return 20000 * 1000;
	case BCMN_CHIP_ID_BCM4706:
	case BCMN_CHIP_ID_BCM5356:
		/* always 25Mhz */
		return 25000 * 1000;
	case BCMN_CHIP_ID_BCM43460:
	case BCMN_CHIP_ID_BCM4352:
	case BCMN_CHIP_ID_BCM4360:
		if (cc->status & BCMN_CC_CHIPST_4360_XTAL_40MZ)
			return 40000 * 1000;
		else
			return 20000 * 1000;
	default:
		bcmn_warn(bus, "No ALP clock specified for %04X device, pmu rev. %d, using default %d Hz\n",
			  bus->chipinfo.id, cc->pmu.rev, BCMN_CC_PMU_ALP_CLOCK);
	}
	return BCMN_CC_PMU_ALP_CLOCK;
}

/* Find the output of the "m" pll divider given pll controls that start with
 * pllreg "pll0" i.e. 12 for main 6 for phy, 0 for misc.
 */
static u32 bcmn_pmu_pll_clock(struct bcmn_drv_cc *cc, u32 pll0, u32 m)
{
	u32 tmp, div, ndiv, p1, p2, fc;
	struct bcmn_bus *bus = cc->core->bus;

	BUG_ON((pll0 & 3) || (pll0 > BCMN_CC_PMU4716_MAINPLL_PLL0));

	BUG_ON(!m || m > 4);

	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM5357 ||
	    bus->chipinfo.id == BCMN_CHIP_ID_BCM4749) {
		/* Detect failure in clock setting */
		tmp = bcmn_cc_read32(cc, BCMN_CC_CHIPSTAT);
		if (tmp & 0x40000)
			return 133 * 1000000;
	}

	tmp = bcmn_chipco_pll_read(cc, pll0 + BCMN_CC_PPL_P1P2_OFF);
	p1 = (tmp & BCMN_CC_PPL_P1_MASK) >> BCMN_CC_PPL_P1_SHIFT;
	p2 = (tmp & BCMN_CC_PPL_P2_MASK) >> BCMN_CC_PPL_P2_SHIFT;

	tmp = bcmn_chipco_pll_read(cc, pll0 + BCMN_CC_PPL_M14_OFF);
	div = (tmp >> ((m - 1) * BCMN_CC_PPL_MDIV_WIDTH)) &
		BCMN_CC_PPL_MDIV_MASK;

	tmp = bcmn_chipco_pll_read(cc, pll0 + BCMN_CC_PPL_NM5_OFF);
	ndiv = (tmp & BCMN_CC_PPL_NDIV_MASK) >> BCMN_CC_PPL_NDIV_SHIFT;

	/* Do calculation in Mhz */
	fc = bcmn_pmu_get_alp_clock(cc) / 1000000;
	fc = (p1 * ndiv * fc) / p2;

	/* Return clock in Hertz */
	return (fc / div) * 1000000;
}

static u32 bcmn_pmu_pll_clock_bcm4706(struct bcmn_drv_cc *cc, u32 pll0, u32 m)
{
	u32 tmp, ndiv, p1div, p2div;
	u32 clock;

	BUG_ON(!m || m > 4);

	/* Get N, P1 and P2 dividers to determine CPU clock */
	tmp = bcmn_chipco_pll_read(cc, pll0 + BCMN_CC_PMU6_4706_PROCPLL_OFF);
	ndiv = (tmp & BCMN_CC_PMU6_4706_PROC_NDIV_INT_MASK)
		>> BCMN_CC_PMU6_4706_PROC_NDIV_INT_SHIFT;
	p1div = (tmp & BCMN_CC_PMU6_4706_PROC_P1DIV_MASK)
		>> BCMN_CC_PMU6_4706_PROC_P1DIV_SHIFT;
	p2div = (tmp & BCMN_CC_PMU6_4706_PROC_P2DIV_MASK)
		>> BCMN_CC_PMU6_4706_PROC_P2DIV_SHIFT;

	tmp = bcmn_cc_read32(cc, BCMN_CC_CHIPSTAT);
	if (tmp & BCMN_CC_CHIPST_4706_PKG_OPTION)
		/* Low cost bonding: Fixed reference clock 25MHz and m = 4 */
		clock = (25000000 / 4) * ndiv * p2div / p1div;
	else
		/* Fixed reference clock 25MHz and m = 2 */
		clock = (25000000 / 2) * ndiv * p2div / p1div;

	if (m == BCMN_CC_PMU5_MAINPLL_SSB)
		clock = clock / 4;

	return clock;
}

/* query bus clock frequency for PMU-enabled chipcommon */
u32 bcmn_pmu_get_bus_clock(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4716:
	case BCMN_CHIP_ID_BCM4748:
	case BCMN_CHIP_ID_BCM47162:
		return bcmn_pmu_pll_clock(cc, BCMN_CC_PMU4716_MAINPLL_PLL0,
					  BCMN_CC_PMU5_MAINPLL_SSB);
	case BCMN_CHIP_ID_BCM5356:
		return bcmn_pmu_pll_clock(cc, BCMN_CC_PMU5356_MAINPLL_PLL0,
					  BCMN_CC_PMU5_MAINPLL_SSB);
	case BCMN_CHIP_ID_BCM5357:
	case BCMN_CHIP_ID_BCM4749:
		return bcmn_pmu_pll_clock(cc, BCMN_CC_PMU5357_MAINPLL_PLL0,
					  BCMN_CC_PMU5_MAINPLL_SSB);
	case BCMN_CHIP_ID_BCM4706:
		return bcmn_pmu_pll_clock_bcm4706(cc,
						  BCMN_CC_PMU4706_MAINPLL_PLL0,
						  BCMN_CC_PMU5_MAINPLL_SSB);
	case BCMN_CHIP_ID_BCM53572:
		return 75000000;
	default:
		bcmn_warn(bus, "No bus clock specified for %04X device, pmu rev. %d, using default %d Hz\n",
			  bus->chipinfo.id, cc->pmu.rev, BCMN_CC_PMU_HT_CLOCK);
	}
	return BCMN_CC_PMU_HT_CLOCK;
}
EXPORT_SYMBOL_GPL(bcmn_pmu_get_bus_clock);

/* query cpu clock frequency for PMU-enabled chipcommon */
u32 bcmn_pmu_get_cpu_clock(struct bcmn_drv_cc *cc)
{
	struct bcmn_bus *bus = cc->core->bus;

	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM53572)
		return 300000000;

	/* New PMUs can have different clock for bus and CPU */
	if (cc->pmu.rev >= 5) {
		u32 pll;
		switch (bus->chipinfo.id) {
		case BCMN_CHIP_ID_BCM4706:
			return bcmn_pmu_pll_clock_bcm4706(cc,
						BCMN_CC_PMU4706_MAINPLL_PLL0,
						BCMN_CC_PMU5_MAINPLL_CPU);
		case BCMN_CHIP_ID_BCM5356:
			pll = BCMN_CC_PMU5356_MAINPLL_PLL0;
			break;
		case BCMN_CHIP_ID_BCM5357:
		case BCMN_CHIP_ID_BCM4749:
			pll = BCMN_CC_PMU5357_MAINPLL_PLL0;
			break;
		default:
			pll = BCMN_CC_PMU4716_MAINPLL_PLL0;
			break;
		}

		return bcmn_pmu_pll_clock(cc, pll, BCMN_CC_PMU5_MAINPLL_CPU);
	}

	/* On old PMUs CPU has the same clock as the bus */
	return bcmn_pmu_get_bus_clock(cc);
}

static void bcmn_pmu_spuravoid_pll_write(struct bcmn_drv_cc *cc, u32 offset,
					 u32 value)
{
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR, offset);
	bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_DATA, value);
}

void bcmn_pmu_spuravoid_pllupdate(struct bcmn_drv_cc *cc, int spuravoid)
{
	u32 tmp = 0;
	u8 phypll_offset = 0;
	u8 bcm5357_bcm43236_p1div[] = {0x1, 0x5, 0x5};
	u8 bcm5357_bcm43236_ndiv[] = {0x30, 0xf6, 0xfc};
	struct bcmn_bus *bus = cc->core->bus;

	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM5357:
	case BCMN_CHIP_ID_BCM4749:
	case BCMN_CHIP_ID_BCM53572:
		/* 5357[ab]0, 43236[ab]0, and 6362b0 */

		/*
		 * BCM5357 needs to touch PLL1_PLLCTL[02],
		 * so offset PLL0_PLLCTL[02] by 6
		 */
		phypll_offset = (bus->chipinfo.id == BCMN_CHIP_ID_BCM5357 ||
		       bus->chipinfo.id == BCMN_CHIP_ID_BCM4749 ||
		       bus->chipinfo.id == BCMN_CHIP_ID_BCM53572) ? 6 : 0;

		/* RMW only the P1 divider */
		bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR,
				BCMN_CC_PMU_PLL_CTL0 + phypll_offset);
		tmp = bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_DATA);
		tmp &= (~(BCMN_CC_PMU1_PLL0_PC0_P1DIV_MASK));
		tmp |= (bcm5357_bcm43236_p1div[spuravoid] << BCMN_CC_PMU1_PLL0_PC0_P1DIV_SHIFT);
		bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_DATA, tmp);

		/* RMW only the int feedback divider */
		bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_ADDR,
				BCMN_CC_PMU_PLL_CTL2 + phypll_offset);
		tmp = bcmn_pmu_read32(cc, BCMN_CC_PMU_PLLCTL_DATA);
		tmp &= ~(BCMN_CC_PMU1_PLL0_PC2_NDIV_INT_MASK);
		tmp |= (bcm5357_bcm43236_ndiv[spuravoid]) << BCMN_CC_PMU1_PLL0_PC2_NDIV_INT_SHIFT;
		bcmn_pmu_write32(cc, BCMN_CC_PMU_PLLCTL_DATA, tmp);

		tmp = BCMN_CC_PMU_CTL_PLL_UPD;
		break;

	case BCMN_CHIP_ID_BCM4331:
	case BCMN_CHIP_ID_BCM43431:
		if (spuravoid == 2) {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11500014);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x0FC00a08);
		} else if (spuravoid == 1) {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11500014);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x0F600a08);
		} else {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11100014);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x03000a08);
		}
		tmp = BCMN_CC_PMU_CTL_PLL_UPD;
		break;

	case BCMN_CHIP_ID_BCM43224:
	case BCMN_CHIP_ID_BCM43225:
	case BCMN_CHIP_ID_BCM43421:
		if (spuravoid == 1) {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11500010);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x000C0C06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x0F600a08);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x2001E920);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		} else {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11100010);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x000c0c06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x03000a08);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x200005c0);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		}
		tmp = BCMN_CC_PMU_CTL_PLL_UPD;
		break;

	case BCMN_CHIP_ID_BCM4716:
	case BCMN_CHIP_ID_BCM4748:
	case BCMN_CHIP_ID_BCM47162:
		if (spuravoid == 1) {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11500060);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x080C0C06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x0F600000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x2001E924);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		} else {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11100060);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x080c0c06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x03000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x200005c0);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		}

		tmp = BCMN_CC_PMU_CTL_PLL_UPD | BCMN_CC_PMU_CTL_NOILPONW;
		break;

	case BCMN_CHIP_ID_BCM43131:
	case BCMN_CHIP_ID_BCM43217:
	case BCMN_CHIP_ID_BCM43227:
	case BCMN_CHIP_ID_BCM43228:
	case BCMN_CHIP_ID_BCM43428:
		/* LCNXN */
		/*
		 * PLL Settings for spur avoidance on/off mode,
		 * no on2 support for 43228A0
		 */
		if (spuravoid == 1) {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x01100014);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x040C0C06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x03140A08);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00333333);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x202C2820);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		} else {
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL0,
						     0x11100014);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL1,
						     0x040c0c06);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL2,
						     0x03000a08);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL3,
						     0x00000000);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL4,
						     0x200005c0);
			bcmn_pmu_spuravoid_pll_write(cc, BCMN_CC_PMU_PLL_CTL5,
						     0x88888815);
		}
		tmp = BCMN_CC_PMU_CTL_PLL_UPD;
		break;
	default:
		bcmn_err(bus, "Unknown spuravoidance settings for chip 0x%04X, not changing PLL\n",
			 bus->chipinfo.id);
		break;
	}

	tmp |= bcmn_pmu_read32(cc, BCMN_CC_PMU_CTL);
	bcmn_pmu_write32(cc, BCMN_CC_PMU_CTL, tmp);
}
EXPORT_SYMBOL_GPL(bcmn_pmu_spuravoid_pllupdate);
