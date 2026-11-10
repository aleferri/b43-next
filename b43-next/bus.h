/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_BUS_H_
#define B43_BUS_H_

enum b43_bus_type {
#ifdef CONFIG_B43_NEXT_BCMA
	B43_BUS_BCMA,
#endif
#ifdef CONFIG_B43_NEXT_SSB
	B43_BUS_SSB,
#endif
};

struct b43_bus_dev {
	enum b43_bus_type bus_type;
	union {
		struct bcmn_device *bdev;
		struct ssbn_device *sdev;
	};

	int (*bus_may_powerdown)(struct b43_bus_dev *dev);
	int (*bus_powerup)(struct b43_bus_dev *dev, bool dynamic_pctl);
	int (*device_is_enabled)(struct b43_bus_dev *dev);
	void (*device_enable)(struct b43_bus_dev *dev,
			      u32 core_specific_flags);
	void (*device_disable)(struct b43_bus_dev *dev,
			       u32 core_specific_flags);

	u16 (*read16)(struct b43_bus_dev *dev, u16 offset);
	u32 (*read32)(struct b43_bus_dev *dev, u16 offset);
	void (*write16)(struct b43_bus_dev *dev, u16 offset, u16 value);
	void (*write32)(struct b43_bus_dev *dev, u16 offset, u32 value);
	void (*block_read)(struct b43_bus_dev *dev, void *buffer,
			   size_t count, u16 offset, u8 reg_width);
	void (*block_write)(struct b43_bus_dev *dev, const void *buffer,
			    size_t count, u16 offset, u8 reg_width);
	bool flush_writes;

	struct device *dev;
	struct device *dma_dev;
	unsigned int irq;

	u16 board_vendor;
	u16 board_type;
	u16 board_rev;

	u16 chip_id;
	u8 chip_rev;
	u8 chip_pkg;

	struct ssbn_sprom *bus_sprom;

	u16 core_id;
	u8 core_rev;
};

static inline bool b43_bus_host_is_pcmcia(struct b43_bus_dev *dev)
{
#ifdef CONFIG_B43_NEXT_SSB
	return (dev->bus_type == B43_BUS_SSB &&
		dev->sdev->bus->bustype == SSBN_BUSTYPE_PCMCIA);
#else
	return false;
#endif
};

static inline bool b43_bus_host_is_pci(struct b43_bus_dev *dev)
{
#ifdef CONFIG_B43_NEXT_BCMA
	if (dev->bus_type == B43_BUS_BCMA)
		return (dev->bdev->bus->hosttype == BCMN_HOSTTYPE_PCI);
#endif
#ifdef CONFIG_B43_NEXT_SSB
	if (dev->bus_type == B43_BUS_SSB)
		return (dev->sdev->bus->bustype == SSBN_BUSTYPE_PCI);
#endif
	return false;
}

static inline bool b43_bus_host_is_sdio(struct b43_bus_dev *dev)
{
#ifdef CONFIG_B43_NEXT_SSB
	return (dev->bus_type == B43_BUS_SSB &&
		dev->sdev->bus->bustype == SSBN_BUSTYPE_SDIO);
#else
	return false;
#endif
}

struct b43_bus_dev *b43_bus_dev_bcma_init(struct bcmn_device *core);
struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssbn_device *sdev);

void *b43_bus_get_wldev(struct b43_bus_dev *dev);
void b43_bus_set_wldev(struct b43_bus_dev *dev, void *data);

#endif /* B43_BUS_H_ */
