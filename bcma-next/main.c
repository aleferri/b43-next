/*
 * Broadcom specific AMBA
 * Bus subsystem
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"
#include <linux/module.h>
#include <linux/mmc/sdio_func.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <bcma-next/bcmn.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>


/* contains the number the next bus should get. */
static unsigned int bcmn_bus_next_num;

/* bcmn_buses_mutex locks the bcmn_bus_next_num */
static DEFINE_MUTEX(bcmn_buses_mutex);

static int bcmn_bus_match(struct device *dev, const struct device_driver *drv);
static int bcmn_device_probe(struct device *dev);
static void bcmn_device_remove(struct device *dev);
static int bcmn_device_uevent(const struct device *dev, struct kobj_uevent_env *env);

static ssize_t manuf_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	return sprintf(buf, "0x%03X\n", core->id.manuf);
}
static DEVICE_ATTR_RO(manuf);

static ssize_t id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	return sprintf(buf, "0x%03X\n", core->id.id);
}
static DEVICE_ATTR_RO(id);

static ssize_t rev_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	return sprintf(buf, "0x%02X\n", core->id.rev);
}
static DEVICE_ATTR_RO(rev);

static ssize_t class_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	return sprintf(buf, "0x%X\n", core->id.class);
}
static DEVICE_ATTR_RO(class);

static struct attribute *bcmn_device_attrs[] = {
	&dev_attr_manuf.attr,
	&dev_attr_id.attr,
	&dev_attr_rev.attr,
	&dev_attr_class.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bcmn_device);

static const struct bus_type bcmn_bus_type = {
	.name		= "bcma-next",
	.match		= bcmn_bus_match,
	.probe		= bcmn_device_probe,
	.remove		= bcmn_device_remove,
	.uevent		= bcmn_device_uevent,
	.dev_groups	= bcmn_device_groups,
};

static u16 bcmn_cc_core_id(struct bcmn_bus *bus)
{
	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4706)
		return BCMN_CORE_4706_CHIPCOMMON;
	return BCMN_CORE_CHIPCOMMON;
}

struct bcmn_device *bcmn_find_core_unit(struct bcmn_bus *bus, u16 coreid,
					u8 unit)
{
	struct bcmn_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		if (core->id.id == coreid && core->core_unit == unit)
			return core;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(bcmn_find_core_unit);

bool bcmn_wait_value(struct bcmn_device *core, u16 reg, u32 mask, u32 value,
		     int timeout)
{
	unsigned long deadline = jiffies + timeout;
	u32 val;

	do {
		val = bcmn_read32(core, reg);
		if ((val & mask) == value)
			return true;
		cpu_relax();
		udelay(10);
	} while (!time_after_eq(jiffies, deadline));

	bcmn_warn(core->bus, "Timeout waiting for register 0x%04X!\n", reg);

	return false;
}

static void bcmn_release_core_dev(struct device *dev)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	if (core->io_addr)
		iounmap(core->io_addr);
	if (core->io_wrap)
		iounmap(core->io_wrap);
	kfree(core);
}

static bool bcmn_is_core_needed_early(u16 core_id)
{
	switch (core_id) {
	case BCMN_CORE_NS_NAND:
	case BCMN_CORE_NS_QSPI:
		return true;
	}

	return false;
}

static struct device_node *bcmn_of_find_child_device(struct device *parent,
						     struct bcmn_device *core)
{
	struct device_node *node;
	int ret;

	if (!parent->of_node)
		return NULL;

	for_each_child_of_node(parent->of_node, node) {
		struct resource res;
		ret = of_address_to_resource(node, 0, &res);
		if (ret)
			continue;
		if (res.start == core->addr)
			return node;
	}
	return NULL;
}

static int bcmn_of_irq_parse(struct device *parent,
			     struct bcmn_device *core,
			     struct of_phandle_args *out_irq, int num)
{
	__be32 laddr[1];
	int rc;

	if (core->dev.of_node) {
		rc = of_irq_parse_one(core->dev.of_node, num, out_irq);
		if (!rc)
			return rc;
	}

	out_irq->np = parent->of_node;
	out_irq->args_count = 1;
	out_irq->args[0] = num;

	laddr[0] = cpu_to_be32(core->addr);
	return of_irq_parse_raw(laddr, out_irq);
}

static unsigned int bcmn_of_get_irq(struct device *parent,
				    struct bcmn_device *core, int num)
{
	struct of_phandle_args out_irq;
	int ret;

	if (!IS_ENABLED(CONFIG_OF_IRQ) || !parent->of_node)
		return 0;

	ret = bcmn_of_irq_parse(parent, core, &out_irq, num);
	if (ret) {
		bcmn_debug(core->bus, "bcmn_of_get_irq() failed with rc=%d\n",
			   ret);
		return 0;
	}

	return irq_create_of_mapping(&out_irq);
}

static void bcmn_of_fill_device(struct device *parent,
				struct bcmn_device *core)
{
	struct device_node *node;

	node = bcmn_of_find_child_device(parent, core);
	if (node)
		core->dev.of_node = node;

	core->irq = bcmn_of_get_irq(parent, core, 0);

	of_dma_configure(&core->dev, node, false);
}

unsigned int bcmn_core_irq(struct bcmn_device *core, int num)
{
	struct bcmn_bus *bus = core->bus;
	unsigned int mips_irq;

	switch (bus->hosttype) {
	case BCMN_HOSTTYPE_PCI:
		return bus->host_pci->irq;
	case BCMN_HOSTTYPE_SOC:
		if (bus->drv_mips.core && num == 0) {
			mips_irq = bcmn_core_mips_irq(core);
			return mips_irq <= 4 ? mips_irq + 2 : 0;
		}
		if (bus->dev)
			return bcmn_of_get_irq(bus->dev, core, num);
		return 0;
	case BCMN_HOSTTYPE_SDIO:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(bcmn_core_irq);

void bcmn_prepare_core(struct bcmn_bus *bus, struct bcmn_device *core)
{
	device_initialize(&core->dev);
	core->dev.release = bcmn_release_core_dev;
	core->dev.bus = &bcmn_bus_type;
	dev_set_name(&core->dev, "bcmn%d:%d", bus->num, core->core_index);
	core->dev.parent = bus->dev;
	if (bus->dev)
		bcmn_of_fill_device(bus->dev, core);

	switch (bus->hosttype) {
	case BCMN_HOSTTYPE_PCI:
		core->dma_dev = bus->dev;
		core->irq = bus->host_pci->irq;
		break;
	case BCMN_HOSTTYPE_SOC:
		if (IS_ENABLED(CONFIG_OF) && bus->dev) {
			core->dma_dev = bus->dev;
		} else {
			core->dev.dma_mask = &core->dev.coherent_dma_mask;
			core->dma_dev = &core->dev;
		}
		break;
	case BCMN_HOSTTYPE_SDIO:
		break;
	}
}

void bcmn_init_bus(struct bcmn_bus *bus)
{
	mutex_lock(&bcmn_buses_mutex);
	bus->num = bcmn_bus_next_num++;
	mutex_unlock(&bcmn_buses_mutex);

	INIT_LIST_HEAD(&bus->cores);
	bus->nr_cores = 0;

	bcmn_detect_chip(bus);
}

static void bcmn_register_core(struct bcmn_bus *bus, struct bcmn_device *core)
{
	int err;

	err = device_add(&core->dev);
	if (err) {
		bcmn_err(bus, "Could not register dev for core 0x%03X\n",
			 core->id.id);
		return;
	}
	core->dev_registered = true;
}

static int bcmn_register_devices(struct bcmn_bus *bus)
{
	struct bcmn_device *core;
	int err;

	list_for_each_entry(core, &bus->cores, list) {
		struct device_node *np;

		/* We support that core ourselves */
		switch (core->id.id) {
		case BCMN_CORE_4706_CHIPCOMMON:
		case BCMN_CORE_CHIPCOMMON:
		case BCMN_CORE_NS_CHIPCOMMON_B:
		case BCMN_CORE_PCI:
		case BCMN_CORE_PCIE:
		case BCMN_CORE_PCIE2:
		case BCMN_CORE_MIPS_74K:
		case BCMN_CORE_4706_MAC_GBIT_COMMON:
			continue;
		}

		/* Early cores were already registered */
		if (bcmn_is_core_needed_early(core->id.id))
			continue;

		np = core->dev.of_node;
		if (np && !of_device_is_available(np))
			continue;

		/* Only first GMAC core on BCM4706 is connected and working */
		if (core->id.id == BCMN_CORE_4706_MAC_GBIT &&
		    core->core_unit > 0)
			continue;

		bcmn_register_core(bus, core);
	}

#ifdef CONFIG_BCMN_PFLASH
	if (bus->drv_cc.pflash.present) {
		err = platform_device_register(&bcmn_pflash_dev);
		if (err)
			bcmn_err(bus, "Error registering parallel flash\n");
	}
#endif

#ifdef CONFIG_BCMN_SFLASH
	if (bus->drv_cc.sflash.present) {
		err = platform_device_register(&bcmn_sflash_dev);
		if (err)
			bcmn_err(bus, "Error registering serial flash\n");
	}
#endif

#ifdef CONFIG_BCMN_NFLASH
	if (bus->drv_cc.nflash.present) {
		err = platform_device_register(&bcmn_nflash_dev);
		if (err)
			bcmn_err(bus, "Error registering NAND flash\n");
	}
#endif
	err = bcmn_gpio_init(&bus->drv_cc);
	if (err == -ENOTSUPP)
		bcmn_debug(bus, "GPIO driver not activated\n");
	else if (err) {
		bcmn_err(bus, "Error registering GPIO driver: %i\n", err);
		return err;
	}

	if (bus->hosttype == BCMN_HOSTTYPE_SOC) {
		err = bcmn_chipco_watchdog_register(&bus->drv_cc);
		if (err)
			bcmn_err(bus, "Error registering watchdog driver\n");
	}

	return 0;
}

void bcmn_unregister_cores(struct bcmn_bus *bus)
{
	struct bcmn_device *core, *tmp;

	list_for_each_entry_safe(core, tmp, &bus->cores, list) {
		if (!core->dev_registered)
			continue;
		list_del(&core->list);
		device_unregister(&core->dev);
	}
	if (bus->hosttype == BCMN_HOSTTYPE_SOC)
		platform_device_unregister(bus->drv_cc.watchdog);

	/* Now no one uses internally-handled cores, we can free them */
	list_for_each_entry_safe(core, tmp, &bus->cores, list) {
		list_del(&core->list);
		put_device(&core->dev);
	}
}

int bcmn_bus_register(struct bcmn_bus *bus)
{
	int err;
	struct bcmn_device *core;

	/* Scan for devices (cores) */
	err = bcmn_bus_scan(bus);
	if (err) {
		bcmn_err(bus, "Failed to scan: %d\n", err);
		return err;
	}

	/* Early init CC core */
	core = bcmn_find_core(bus, bcmn_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcmn_core_chipcommon_early_init(&bus->drv_cc);
	}

	/* Early init PCIE core */
	core = bcmn_find_core(bus, BCMN_CORE_PCIE);
	if (core) {
		bus->drv_pci[0].core = core;
		bcmn_core_pci_early_init(&bus->drv_pci[0]);
	}

	if (bus->dev)
		of_platform_default_populate(bus->dev->of_node, NULL, bus->dev);

	/* Cores providing flash access go before SPROM init */
	list_for_each_entry(core, &bus->cores, list) {
		if (bcmn_is_core_needed_early(core->id.id))
			bcmn_register_core(bus, core);
	}

	/* Try to get SPROM */
	err = bcmn_sprom_get(bus);
	if (err == -ENOENT) {
		bcmn_err(bus, "No SPROM available\n");
	} else if (err)
		bcmn_err(bus, "Failed to get SPROM: %d\n", err);

	/* Init CC core */
	core = bcmn_find_core(bus, bcmn_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcmn_core_chipcommon_init(&bus->drv_cc);
	}

	/* Init CC core */
	core = bcmn_find_core(bus, BCMN_CORE_NS_CHIPCOMMON_B);
	if (core) {
		bus->drv_cc_b.core = core;
		bcmn_core_chipcommon_b_init(&bus->drv_cc_b);
	}

	/* Init MIPS core */
	core = bcmn_find_core(bus, BCMN_CORE_MIPS_74K);
	if (core) {
		bus->drv_mips.core = core;
		bcmn_core_mips_init(&bus->drv_mips);
	}

	/* Init PCIE core */
	core = bcmn_find_core_unit(bus, BCMN_CORE_PCIE, 0);
	if (core) {
		bus->drv_pci[0].core = core;
		bcmn_core_pci_init(&bus->drv_pci[0]);
	}

	/* Init PCIE core */
	core = bcmn_find_core_unit(bus, BCMN_CORE_PCIE, 1);
	if (core) {
		bus->drv_pci[1].core = core;
		bcmn_core_pci_init(&bus->drv_pci[1]);
	}

	/* Init PCIe Gen 2 core */
	core = bcmn_find_core_unit(bus, BCMN_CORE_PCIE2, 0);
	if (core) {
		bus->drv_pcie2.core = core;
		bcmn_core_pcie2_init(&bus->drv_pcie2);
	}

	/* Init GBIT MAC COMMON core */
	core = bcmn_find_core(bus, BCMN_CORE_4706_MAC_GBIT_COMMON);
	if (core) {
		bus->drv_gmac_cmn.core = core;
		bcmn_core_gmac_cmn_init(&bus->drv_gmac_cmn);
	}

	/* Register found cores */
	bcmn_register_devices(bus);

	bcmn_info(bus, "Bus registered\n");

	return 0;
}

void bcmn_bus_unregister(struct bcmn_bus *bus)
{
	int err;

	err = bcmn_gpio_unregister(&bus->drv_cc);
	if (err == -EBUSY)
		bcmn_err(bus, "Some GPIOs are still in use.\n");
	else if (err)
		bcmn_err(bus, "Can not unregister GPIO driver: %i\n", err);

	bcmn_core_chipcommon_b_free(&bus->drv_cc_b);

	bcmn_unregister_cores(bus);
}

/*
 * This is a special version of bus registration function designed for SoCs.
 * It scans bus and performs basic initialization of main cores only.
 * Please note it requires memory allocation, however it won't try to sleep.
 */
int __init bcmn_bus_early_register(struct bcmn_bus *bus)
{
	int err;
	struct bcmn_device *core;

	/* Scan for devices (cores) */
	err = bcmn_bus_scan(bus);
	if (err) {
		bcmn_err(bus, "Failed to scan bus: %d\n", err);
		return -1;
	}

	/* Early init CC core */
	core = bcmn_find_core(bus, bcmn_cc_core_id(bus));
	if (core) {
		bus->drv_cc.core = core;
		bcmn_core_chipcommon_early_init(&bus->drv_cc);
	}

	/* Early init MIPS core */
	core = bcmn_find_core(bus, BCMN_CORE_MIPS_74K);
	if (core) {
		bus->drv_mips.core = core;
		bcmn_core_mips_early_init(&bus->drv_mips);
	}

	bcmn_info(bus, "Early bus registered\n");

	return 0;
}

#ifdef CONFIG_PM
int bcmn_bus_suspend(struct bcmn_bus *bus)
{
	struct bcmn_device *core;

	list_for_each_entry(core, &bus->cores, list) {
		struct device_driver *drv = core->dev.driver;
		if (drv) {
			struct bcmn_driver *adrv = container_of(drv, struct bcmn_driver, drv);
			if (adrv->suspend)
				adrv->suspend(core);
		}
	}
	return 0;
}

int bcmn_bus_resume(struct bcmn_bus *bus)
{
	struct bcmn_device *core;

	/* Init CC core */
	if (bus->drv_cc.core) {
		bus->drv_cc.setup_done = false;
		bcmn_core_chipcommon_init(&bus->drv_cc);
	}

	list_for_each_entry(core, &bus->cores, list) {
		struct device_driver *drv = core->dev.driver;
		if (drv) {
			struct bcmn_driver *adrv = container_of(drv, struct bcmn_driver, drv);
			if (adrv->resume)
				adrv->resume(core);
		}
	}

	return 0;
}
#endif

int __bcmn_driver_register(struct bcmn_driver *drv, struct module *owner)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &bcmn_bus_type;
	drv->drv.owner = owner;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(__bcmn_driver_register);

void bcmn_driver_unregister(struct bcmn_driver *drv)
{
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL_GPL(bcmn_driver_unregister);

static int bcmn_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	const struct bcmn_driver *adrv = container_of_const(drv, struct bcmn_driver, drv);
	const struct bcmn_device_id *cid = &core->id;
	const struct bcmn_device_id *did;

	for (did = adrv->id_table; did->manuf || did->id || did->rev; did++) {
	    if ((did->manuf == cid->manuf || did->manuf == BCMN_ANY_MANUF) &&
		(did->id == cid->id || did->id == BCMN_ANY_ID) &&
		(did->rev == cid->rev || did->rev == BCMN_ANY_REV) &&
		(did->class == cid->class || did->class == BCMN_ANY_CLASS))
			return 1;
	}
	return 0;
}

static int bcmn_device_probe(struct device *dev)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	struct bcmn_driver *adrv = container_of(dev->driver, struct bcmn_driver,
					       drv);
	int err = 0;

	get_device(dev);
	if (adrv->probe)
		err = adrv->probe(core);
	if (err)
		put_device(dev);

	return err;
}

static void bcmn_device_remove(struct device *dev)
{
	struct bcmn_device *core = container_of(dev, struct bcmn_device, dev);
	struct bcmn_driver *adrv = container_of(dev->driver, struct bcmn_driver,
					       drv);

	if (adrv->remove)
		adrv->remove(core);
	put_device(dev);
}

static int bcmn_device_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct bcmn_device *core = container_of_const(dev, struct bcmn_device, dev);

	return add_uevent_var(env,
			      "MODALIAS=bcmn:m%04Xid%04Xrev%02Xcl%02X",
			      core->id.manuf, core->id.id,
			      core->id.rev, core->id.class);
}

static unsigned int bcmn_bus_registered;

/*
 * If built-in, bus has to be registered early, before any driver calls
 * bcmn_driver_register.
 * Otherwise registering driver would trigger BUG in driver_register.
 */
static int __init bcmn_init_bus_register(void)
{
	int err;

	if (bcmn_bus_registered)
		return 0;

	err = bus_register(&bcmn_bus_type);
	if (!err)
		bcmn_bus_registered = 1;

	return err;
}
#ifndef MODULE
fs_initcall(bcmn_init_bus_register);
#endif

/* Main initialization has to be done with SPI/mtd/NAND/SPROM available */
int __init bcmn_modinit(void)
{
	int err;

	err = bcmn_init_bus_register();
	if (err)
		return err;

	err = bcmn_host_soc_register_driver();
	if (err) {
		pr_err("SoC host initialization failed\n");
		err = 0;
	}
#ifdef CONFIG_BCMN_HOST_PCI
	err = bcmn_host_pci_init();
	if (err) {
		pr_err("PCI host initialization failed\n");
		err = 0;
	}
#endif

	return err;
}

void bcmn_modexit(void)
{
#ifdef CONFIG_BCMN_HOST_PCI
	bcmn_host_pci_exit();
#endif
	bcmn_host_soc_unregister_driver();
	bus_unregister(&bcmn_bus_type);
}
