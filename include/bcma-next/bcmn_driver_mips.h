/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_BCMN_DRIVER_MIPS_H_
#define LINUX_BCMN_DRIVER_MIPS_H_

#define BCMN_MIPS_IPSFLAG		0x0F08
/* which sbflags get routed to mips interrupt 1 */
#define  BCMN_MIPS_IPSFLAG_IRQ1		0x0000003F
#define  BCMN_MIPS_IPSFLAG_IRQ1_SHIFT	0
/* which sbflags get routed to mips interrupt 2 */
#define  BCMN_MIPS_IPSFLAG_IRQ2		0x00003F00
#define  BCMN_MIPS_IPSFLAG_IRQ2_SHIFT	8
/* which sbflags get routed to mips interrupt 3 */
#define  BCMN_MIPS_IPSFLAG_IRQ3		0x003F0000
#define  BCMN_MIPS_IPSFLAG_IRQ3_SHIFT	16
/* which sbflags get routed to mips interrupt 4 */
#define  BCMN_MIPS_IPSFLAG_IRQ4		0x3F000000
#define  BCMN_MIPS_IPSFLAG_IRQ4_SHIFT	24

/* MIPS 74K core registers */
#define BCMN_MIPS_MIPS74K_CORECTL	0x0000
#define BCMN_MIPS_MIPS74K_EXCEPTBASE	0x0004
#define BCMN_MIPS_MIPS74K_BIST		0x000C
#define BCMN_MIPS_MIPS74K_INTMASK_INT0	0x0014
#define BCMN_MIPS_MIPS74K_INTMASK(int) \
	((int) * 4 + BCMN_MIPS_MIPS74K_INTMASK_INT0)
#define BCMN_MIPS_MIPS74K_NMIMASK	0x002C
#define BCMN_MIPS_MIPS74K_GPIOSEL	0x0040
#define BCMN_MIPS_MIPS74K_GPIOOUT	0x0044
#define BCMN_MIPS_MIPS74K_GPIOEN	0x0048
#define BCMN_MIPS_MIPS74K_CLKCTLST	0x01E0

#define BCMN_MIPS_OOBSELINA74		0x004
#define BCMN_MIPS_OOBSELOUTA30		0x100

struct bcmn_device;

struct bcmn_drv_mips {
	struct bcmn_device *core;
	u8 setup_done:1;
	u8 early_setup_done:1;
};

extern u32 bcmn_cpu_clock(struct bcmn_drv_mips *mcore);

#endif /* LINUX_BCMN_DRIVER_MIPS_H_ */
