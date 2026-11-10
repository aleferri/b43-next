/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_SSBN_MIPSCORE_H_
#define LINUX_SSBN_MIPSCORE_H_

#ifdef CONFIG_SSBN_DRIVER_MIPS

struct ssbn_device;

struct ssbn_serial_port {
	void *regs;
	unsigned long clockspeed;
	unsigned int irq;
	unsigned int baud_base;
	unsigned int reg_shift;
};

struct ssbn_pflash {
	bool present;
	u8 buswidth;
	u32 window;
	u32 window_size;
};

#ifdef CONFIG_SSBN_SFLASH
struct ssbn_sflash {
	bool present;
	u32 window;
	u32 blocksize;
	u16 numblocks;
	u32 size;

	void *priv;
};
#endif

struct ssbn_mipscore {
	struct ssbn_device *dev;

	int nr_serial_ports;
	struct ssbn_serial_port serial_ports[4];

	struct ssbn_pflash pflash;
#ifdef CONFIG_SSBN_SFLASH
	struct ssbn_sflash sflash;
#endif
};

extern void ssbn_mipscore_init(struct ssbn_mipscore *mcore);
extern u32 ssbn_cpu_clock(struct ssbn_mipscore *mcore);

extern unsigned int ssbn_mips_irq(struct ssbn_device *dev);


#else /* CONFIG_SSBN_DRIVER_MIPS */

struct ssbn_mipscore {
};

static inline
void ssbn_mipscore_init(struct ssbn_mipscore *mcore)
{
}

static inline unsigned int ssbn_mips_irq(struct ssbn_device *dev)
{
	return 0;
}

#endif /* CONFIG_SSBN_DRIVER_MIPS */

#endif /* LINUX_SSBN_MIPSCORE_H_ */
