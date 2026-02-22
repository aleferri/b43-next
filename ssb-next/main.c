/*
 * Sonics Silicon Backplane
 * Subsystem core
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssbn_private.h"

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <ssb-next/ssbn.h>
#include <ssb-next/ssbn_regs.h>
#include <ssb-next/ssbn_driver_gige.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/mmc/sdio_func.h>
#include <linux/slab.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>




/* Temporary list of yet-to-be-attached buses */
static LIST_HEAD(attach_queue);
/* List if running buses */
static LIST_HEAD(buses);
/* Software ID counter */
static unsigned int next_busnumber;
/* buses_mutes locks the two buslists and the next_busnumber.
 * Don't lock this directly, but use ssbn_buses_[un]lock() below.
 */
static DEFINE_MUTEX(buses_mutex);

/* There are differences in the codeflow, if the bus is
 * initialized from early boot, as various needed services
 * are not available early. This is a mechanism to delay
 * these initializations to after early boot has finished.
 * It's also used to avoid mutex locking, as that's not
 * available and needed early.
 */
static bool ssbn_is_early_boot = 1;

static void ssbn_buses_lock(void);
static void ssbn_buses_unlock(void);


#ifdef CONFIG_SSBN_PCIHOST
struct ssbn_bus *ssbn_pci_dev_to_bus(struct pci_dev *pdev)
{
	struct ssbn_bus *bus;

	ssbn_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		if (bus->bustype == SSBN_BUSTYPE_PCI &&
		    bus->host_pci == pdev)
			goto found;
	}
	bus = NULL;
found:
	ssbn_buses_unlock();

	return bus;
}
#endif /* CONFIG_SSBN_PCIHOST */

#ifdef CONFIG_SSBN_PCMCIAHOST
struct ssbn_bus *ssbn_pcmcia_dev_to_bus(struct pcmcia_device *pdev)
{
	struct ssbn_bus *bus;

	ssbn_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		if (bus->bustype == SSBN_BUSTYPE_PCMCIA &&
		    bus->host_pcmcia == pdev)
			goto found;
	}
	bus = NULL;
found:
	ssbn_buses_unlock();

	return bus;
}
#endif /* CONFIG_SSBN_PCMCIAHOST */

int ssbn_for_each_bus_call(unsigned long data,
			  int (*func)(struct ssbn_bus *bus, unsigned long data))
{
	struct ssbn_bus *bus;
	int res;

	ssbn_buses_lock();
	list_for_each_entry(bus, &buses, list) {
		res = func(bus, data);
		if (res >= 0) {
			ssbn_buses_unlock();
			return res;
		}
	}
	ssbn_buses_unlock();

	return -ENODEV;
}

static struct ssbn_device *ssbn_device_get(struct ssbn_device *dev)
{
	if (dev)
		get_device(dev->dev);
	return dev;
}

static void ssbn_device_put(struct ssbn_device *dev)
{
	if (dev)
		put_device(dev->dev);
}

static int ssbn_device_resume(struct device *dev)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	struct ssbn_driver *ssbn_drv;
	int err = 0;

	if (dev->driver) {
		ssbn_drv = drv_to_ssb_drv(dev->driver);
		if (ssbn_drv && ssbn_drv->resume)
			err = ssbn_drv->resume(ssbn_dev);
		if (err)
			goto out;
	}
out:
	return err;
}

static int ssbn_device_suspend(struct device *dev, pm_message_t state)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	struct ssbn_driver *ssbn_drv;
	int err = 0;

	if (dev->driver) {
		ssbn_drv = drv_to_ssb_drv(dev->driver);
		if (ssbn_drv && ssbn_drv->suspend)
			err = ssbn_drv->suspend(ssbn_dev, state);
		if (err)
			goto out;
	}
out:
	return err;
}

int ssbn_bus_resume(struct ssbn_bus *bus)
{
	int err;

	/* Reset HW state information in memory, so that HW is
	 * completely reinitialized.
	 */
	bus->mapped_device = NULL;
#ifdef CONFIG_SSBN_DRIVER_PCICORE
	bus->pcicore.setup_done = 0;
#endif

	err = ssbn_bus_powerup(bus, 0);
	if (err)
		return err;
	err = ssbn_pcmcia_hardware_setup(bus);
	if (err) {
		ssbn_bus_may_powerdown(bus);
		return err;
	}
	ssbn_chipco_resume(&bus->chipco);
	ssbn_bus_may_powerdown(bus);

	return 0;
}
EXPORT_SYMBOL(ssbn_bus_resume);

int ssbn_bus_suspend(struct ssbn_bus *bus)
{
	ssbn_chipco_suspend(&bus->chipco);
	ssbn_pci_xtal(bus, SSBN_GPIO_XTAL | SSBN_GPIO_PLL, 0);

	return 0;
}
EXPORT_SYMBOL(ssbn_bus_suspend);

#ifdef CONFIG_SSBN_SPROM
/** ssbn_devices_freeze - Freeze all devices on the bus.
 *
 * After freezing no device driver will be handling a device
 * on this bus anymore. ssbn_devices_thaw() must be called after
 * a successful freeze to reactivate the devices.
 *
 * @bus: The bus.
 * @ctx: Context structure. Pass this to ssbn_devices_thaw().
 */
int ssbn_devices_freeze(struct ssbn_bus *bus, struct ssbn_freeze_context *ctx)
{
	struct ssbn_device *sdev;
	struct ssbn_driver *sdrv;
	unsigned int i;

	memset(ctx, 0, sizeof(*ctx));
	ctx->bus = bus;
	WARN_ON(bus->nr_devices > ARRAY_SIZE(ctx->device_frozen));

	for (i = 0; i < bus->nr_devices; i++) {
		sdev = ssbn_device_get(&bus->devices[i]);

		if (!sdev->dev || !sdev->dev->driver ||
		    !device_is_registered(sdev->dev)) {
			ssbn_device_put(sdev);
			continue;
		}
		sdrv = drv_to_ssb_drv(sdev->dev->driver);
		if (WARN_ON(!sdrv->remove))
			continue;
		sdrv->remove(sdev);
		ctx->device_frozen[i] = 1;
	}

	return 0;
}

/** ssbn_devices_thaw - Unfreeze all devices on the bus.
 *
 * This will re-attach the device drivers and re-init the devices.
 *
 * @ctx: The context structure from ssbn_devices_freeze()
 */
int ssbn_devices_thaw(struct ssbn_freeze_context *ctx)
{
	struct ssbn_bus *bus = ctx->bus;
	struct ssbn_device *sdev;
	struct ssbn_driver *sdrv;
	unsigned int i;
	int err, result = 0;

	for (i = 0; i < bus->nr_devices; i++) {
		if (!ctx->device_frozen[i])
			continue;
		sdev = &bus->devices[i];

		if (WARN_ON(!sdev->dev || !sdev->dev->driver))
			continue;
		sdrv = drv_to_ssb_drv(sdev->dev->driver);
		if (WARN_ON(!sdrv || !sdrv->probe))
			continue;

		err = sdrv->probe(sdev, &sdev->id);
		if (err) {
			dev_err(sdev->dev,
				"Failed to thaw device %s\n",
				dev_name(sdev->dev));
			result = err;
		}
		ssbn_device_put(sdev);
	}

	return result;
}
#endif /* CONFIG_SSBN_SPROM */

static void ssbn_device_shutdown(struct device *dev)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	struct ssbn_driver *ssbn_drv;

	if (!dev->driver)
		return;
	ssbn_drv = drv_to_ssb_drv(dev->driver);
	if (ssbn_drv && ssbn_drv->shutdown)
		ssbn_drv->shutdown(ssbn_dev);
}

static void ssbn_device_remove(struct device *dev)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	struct ssbn_driver *ssbn_drv = drv_to_ssb_drv(dev->driver);

	if (ssbn_drv && ssbn_drv->remove)
		ssbn_drv->remove(ssbn_dev);
	ssbn_device_put(ssbn_dev);
}

static int ssbn_device_probe(struct device *dev)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	struct ssbn_driver *ssbn_drv = drv_to_ssb_drv(dev->driver);
	int err = 0;

	ssbn_device_get(ssbn_dev);
	if (ssbn_drv && ssbn_drv->probe)
		err = ssbn_drv->probe(ssbn_dev, &ssbn_dev->id);
	if (err)
		ssbn_device_put(ssbn_dev);

	return err;
}

static int ssbn_match_devid(const struct ssbn_device_id *tabid,
			   const struct ssbn_device_id *devid)
{
	if ((tabid->vendor != devid->vendor) &&
	    tabid->vendor != SSBN_ANY_VENDOR)
		return 0;
	if ((tabid->coreid != devid->coreid) &&
	    tabid->coreid != SSBN_ANY_ID)
		return 0;
	if ((tabid->revision != devid->revision) &&
	    tabid->revision != SSBN_ANY_REV)
		return 0;
	return 1;
}

static int ssbn_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct ssbn_device *ssbn_dev = dev_to_ssb_dev(dev);
	const struct ssbn_driver *ssbn_drv = drv_to_ssb_drv(drv);
	const struct ssbn_device_id *id;

	for (id = ssbn_drv->id_table;
	     id->vendor || id->coreid || id->revision;
	     id++) {
		if (ssbn_match_devid(id, &ssbn_dev->id))
			return 1; /* found */
	}

	return 0;
}

static int ssbn_device_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct ssbn_device *ssbn_dev;

	if (!dev)
		return -ENODEV;

	ssbn_dev = dev_to_ssb_dev(dev);

	return add_uevent_var(env,
			     "MODALIAS=ssbn:v%04Xid%04Xrev%02X",
			     ssbn_dev->id.vendor, ssbn_dev->id.coreid,
			     ssbn_dev->id.revision);
}

#define ssbn_config_attr(attrib, field, format_string) \
static ssize_t \
attrib##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	return sprintf(buf, format_string, dev_to_ssb_dev(dev)->field); \
} \
static DEVICE_ATTR_RO(attrib);

ssbn_config_attr(core_num, core_index, "%u\n")
ssbn_config_attr(coreid, id.coreid, "0x%04x\n")
ssbn_config_attr(vendor, id.vendor, "0x%04x\n")
ssbn_config_attr(revision, id.revision, "%u\n")
ssbn_config_attr(irq, irq, "%u\n")
static ssize_t
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n",
		       ssbn_core_name(dev_to_ssb_dev(dev)->id.coreid));
}
static DEVICE_ATTR_RO(name);

static struct attribute *ssbn_device_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_core_num.attr,
	&dev_attr_coreid.attr,
	&dev_attr_vendor.attr,
	&dev_attr_revision.attr,
	&dev_attr_irq.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ssbn_device);

static const struct bus_type ssbn_bustype = {
	.name		= "ssb-next",
	.match		= ssbn_bus_match,
	.probe		= ssbn_device_probe,
	.remove		= ssbn_device_remove,
	.shutdown	= ssbn_device_shutdown,
	.suspend	= ssbn_device_suspend,
	.resume		= ssbn_device_resume,
	.uevent		= ssbn_device_uevent,
	.dev_groups	= ssbn_device_groups,
};

static void ssbn_buses_lock(void)
{
	/* See the comment at the ssbn_is_early_boot definition */
	if (!ssbn_is_early_boot)
		mutex_lock(&buses_mutex);
}

static void ssbn_buses_unlock(void)
{
	/* See the comment at the ssbn_is_early_boot definition */
	if (!ssbn_is_early_boot)
		mutex_unlock(&buses_mutex);
}

static void ssbn_devices_unregister(struct ssbn_bus *bus)
{
	struct ssbn_device *sdev;
	int i;

	for (i = bus->nr_devices - 1; i >= 0; i--) {
		sdev = &(bus->devices[i]);
		if (sdev->dev)
			device_unregister(sdev->dev);
	}

#ifdef CONFIG_SSBN_EMBEDDED
	if (bus->bustype == SSBN_BUSTYPE_SSB)
		platform_device_unregister(bus->watchdog);
#endif
}

void ssbn_bus_unregister(struct ssbn_bus *bus)
{
	int err;

	err = ssbn_gpio_unregister(bus);
	if (err)
		pr_debug("Can not unregister GPIO driver: %i\n", err);

	ssbn_buses_lock();
	ssbn_devices_unregister(bus);
	list_del(&bus->list);
	ssbn_buses_unlock();

	ssbn_pcmcia_exit(bus);
	ssbn_pci_exit(bus);
	ssbn_iounmap(bus);
}
EXPORT_SYMBOL(ssbn_bus_unregister);

static void ssbn_release_dev(struct device *dev)
{
	struct __ssbn_dev_wrapper *devwrap;

	devwrap = container_of(dev, struct __ssbn_dev_wrapper, dev);
	kfree(devwrap);
}

static int ssbn_devices_register(struct ssbn_bus *bus)
{
	struct ssbn_device *sdev;
	struct device *dev;
	struct __ssbn_dev_wrapper *devwrap;
	int i, err = 0;
	int dev_idx = 0;

	for (i = 0; i < bus->nr_devices; i++) {
		sdev = &(bus->devices[i]);

		/* We don't register SSB-system devices to the kernel,
		 * as the drivers for them are built into SSB.
		 */
		switch (sdev->id.coreid) {
		case SSBN_DEV_CHIPCOMMON:
		case SSBN_DEV_PCI:
		case SSBN_DEV_PCIE:
		case SSBN_DEV_PCMCIA:
		case SSBN_DEV_MIPS:
		case SSBN_DEV_MIPS_3302:
		case SSBN_DEV_EXTIF:
			continue;
		}

		devwrap = kzalloc_obj(*devwrap);
		if (!devwrap) {
			err = -ENOMEM;
			goto error;
		}
		dev = &devwrap->dev;
		devwrap->sdev = sdev;

		dev->release = ssbn_release_dev;
		dev->bus = &ssbn_bustype;
		dev_set_name(dev, "ssbn%u:%d", bus->busnumber, dev_idx);

		switch (bus->bustype) {
		case SSBN_BUSTYPE_PCI:
#ifdef CONFIG_SSBN_PCIHOST
			sdev->irq = bus->host_pci->irq;
			dev->parent = &bus->host_pci->dev;
			sdev->dma_dev = dev->parent;
#endif
			break;
		case SSBN_BUSTYPE_PCMCIA:
#ifdef CONFIG_SSBN_PCMCIAHOST
			sdev->irq = bus->host_pcmcia->irq;
			dev->parent = &bus->host_pcmcia->dev;
#endif
			break;
		case SSBN_BUSTYPE_SDIO:
#ifdef CONFIG_SSBN_SDIOHOST
			dev->parent = &bus->host_sdio->dev;
#endif
			break;
		case SSBN_BUSTYPE_SSB:
			dev->dma_mask = &dev->coherent_dma_mask;
			sdev->dma_dev = dev;
			break;
		}

		sdev->dev = dev;
		err = device_register(dev);
		if (err) {
			pr_err("Could not register %s\n", dev_name(dev));
			/* Set dev to NULL to not unregister
			 * dev on error unwinding.
			 */
			sdev->dev = NULL;
			put_device(dev);
			goto error;
		}
		dev_idx++;
	}

#ifdef CONFIG_SSBN_DRIVER_MIPS
	if (bus->mipscore.pflash.present) {
		err = platform_device_register(&ssbn_pflash_dev);
		if (err)
			pr_err("Error registering parallel flash\n");
	}
#endif

#ifdef CONFIG_SSBN_SFLASH
	if (bus->mipscore.sflash.present) {
		err = platform_device_register(&ssbn_sflash_dev);
		if (err)
			pr_err("Error registering serial flash\n");
	}
#endif

	return 0;
error:
	/* Unwind the already registered devices. */
	ssbn_devices_unregister(bus);
	return err;
}

/* Needs ssbn_buses_lock() */
static int ssbn_attach_queued_buses(void)
{
	struct ssbn_bus *bus, *n;
	int err = 0;
	int drop_them_all = 0;

	list_for_each_entry_safe(bus, n, &attach_queue, list) {
		if (drop_them_all) {
			list_del(&bus->list);
			continue;
		}
		/* Can't init the PCIcore in ssbn_bus_register(), as that
		 * is too early in boot for embedded systems
		 * (no udelay() available). So do it here in attach stage.
		 */
		err = ssbn_bus_powerup(bus, 0);
		if (err)
			goto error;
		ssbn_pcicore_init(&bus->pcicore);
		if (bus->bustype == SSBN_BUSTYPE_SSB)
			ssbn_watchdog_register(bus);

		err = ssbn_gpio_init(bus);
		if (err == -ENOTSUPP)
			pr_debug("GPIO driver not activated\n");
		else if (err)
			pr_debug("Error registering GPIO driver: %i\n", err);

		ssbn_bus_may_powerdown(bus);

		err = ssbn_devices_register(bus);
error:
		if (err) {
			drop_them_all = 1;
			list_del(&bus->list);
			continue;
		}
		list_move_tail(&bus->list, &buses);
	}

	return err;
}

static int ssbn_fetch_invariants(struct ssbn_bus *bus,
				ssbn_invariants_func_t get_invariants)
{
	struct ssbn_init_invariants iv;
	int err;

	memset(&iv, 0, sizeof(iv));
	err = get_invariants(bus, &iv);
	if (err)
		goto out;
	memcpy(&bus->boardinfo, &iv.boardinfo, sizeof(iv.boardinfo));
	memcpy(&bus->sprom, &iv.sprom, sizeof(iv.sprom));
	bus->has_cardbus_slot = iv.has_cardbus_slot;
out:
	return err;
}

static int __maybe_unused
ssbn_bus_register(struct ssbn_bus *bus,
		 ssbn_invariants_func_t get_invariants,
		 unsigned long baseaddr)
{
	int err;

	spin_lock_init(&bus->bar_lock);
	INIT_LIST_HEAD(&bus->list);
#ifdef CONFIG_SSBN_EMBEDDED
	spin_lock_init(&bus->gpio_lock);
#endif

	/* Powerup the bus */
	err = ssbn_pci_xtal(bus, SSBN_GPIO_XTAL | SSBN_GPIO_PLL, 1);
	if (err)
		goto out;

	/* Init SDIO-host device (if any), before the scan */
	err = ssbn_sdio_init(bus);
	if (err)
		goto err_disable_xtal;

	ssbn_buses_lock();
	bus->busnumber = next_busnumber;
	/* Scan for devices (cores) */
	err = ssbn_bus_scan(bus, baseaddr);
	if (err)
		goto err_sdio_exit;

	/* Init PCI-host device (if any) */
	err = ssbn_pci_init(bus);
	if (err)
		goto err_unmap;
	/* Init PCMCIA-host device (if any) */
	err = ssbn_pcmcia_init(bus);
	if (err)
		goto err_pci_exit;

	/* Initialize basic system devices (if available) */
	err = ssbn_bus_powerup(bus, 0);
	if (err)
		goto err_pcmcia_exit;
	ssbn_chipcommon_init(&bus->chipco);
	ssbn_extif_init(&bus->extif);
	ssbn_mipscore_init(&bus->mipscore);
	err = ssbn_fetch_invariants(bus, get_invariants);
	if (err) {
		ssbn_bus_may_powerdown(bus);
		goto err_pcmcia_exit;
	}
	ssbn_bus_may_powerdown(bus);

	/* Queue it for attach.
	 * See the comment at the ssbn_is_early_boot definition.
	 */
	list_add_tail(&bus->list, &attach_queue);
	if (!ssbn_is_early_boot) {
		/* This is not early boot, so we must attach the bus now */
		err = ssbn_attach_queued_buses();
		if (err)
			goto err_dequeue;
	}
	next_busnumber++;
	ssbn_buses_unlock();

out:
	return err;

err_dequeue:
	list_del(&bus->list);
err_pcmcia_exit:
	ssbn_pcmcia_exit(bus);
err_pci_exit:
	ssbn_pci_exit(bus);
err_unmap:
	ssbn_iounmap(bus);
err_sdio_exit:
	ssbn_sdio_exit(bus);
err_disable_xtal:
	ssbn_buses_unlock();
	ssbn_pci_xtal(bus, SSBN_GPIO_XTAL | SSBN_GPIO_PLL, 0);
	return err;
}

#ifdef CONFIG_SSBN_PCIHOST
int ssbn_bus_pcibus_register(struct ssbn_bus *bus, struct pci_dev *host_pci)
{
	int err;

	bus->bustype = SSBN_BUSTYPE_PCI;
	bus->host_pci = host_pci;
	bus->ops = &ssbn_pci_ops;

	err = ssbn_bus_register(bus, ssbn_pci_get_invariants, 0);
	if (!err) {
		dev_info(&host_pci->dev,
			 "Sonics Silicon Backplane found on PCI device %s\n",
			 dev_name(&host_pci->dev));
	} else {
		dev_err(&host_pci->dev,
			"Failed to register PCI version of SSB with error %d\n",
			err);
	}

	return err;
}
#endif /* CONFIG_SSBN_PCIHOST */

#ifdef CONFIG_SSBN_PCMCIAHOST
int ssbn_bus_pcmciabus_register(struct ssbn_bus *bus,
			       struct pcmcia_device *pcmcia_dev,
			       unsigned long baseaddr)
{
	int err;

	bus->bustype = SSBN_BUSTYPE_PCMCIA;
	bus->host_pcmcia = pcmcia_dev;
	bus->ops = &ssbn_pcmcia_ops;

	err = ssbn_bus_register(bus, ssbn_pcmcia_get_invariants, baseaddr);
	if (!err) {
		dev_info(&pcmcia_dev->dev,
			 "Sonics Silicon Backplane found on PCMCIA device %s\n",
			 pcmcia_dev->devname);
	}

	return err;
}
#endif /* CONFIG_SSBN_PCMCIAHOST */

#ifdef CONFIG_SSBN_SDIOHOST
int ssbn_bus_sdiobus_register(struct ssbn_bus *bus, struct sdio_func *func,
			     unsigned int quirks)
{
	int err;

	bus->bustype = SSBN_BUSTYPE_SDIO;
	bus->host_sdio = func;
	bus->ops = &ssbn_sdio_ops;
	bus->quirks = quirks;

	err = ssbn_bus_register(bus, ssbn_sdio_get_invariants, ~0);
	if (!err) {
		dev_info(&func->dev,
			 "Sonics Silicon Backplane found on SDIO device %s\n",
			 sdio_func_id(func));
	}

	return err;
}
EXPORT_SYMBOL(ssbn_bus_sdiobus_register);
#endif /* CONFIG_SSBN_PCMCIAHOST */

#ifdef CONFIG_SSBN_HOST_SOC
int ssbn_bus_host_soc_register(struct ssbn_bus *bus, unsigned long baseaddr)
{
	int err;

	bus->bustype = SSBN_BUSTYPE_SSB;
	bus->ops = &ssbn_host_soc_ops;

	err = ssbn_bus_register(bus, ssbn_host_soc_get_invariants, baseaddr);
	if (!err) {
		pr_info("Sonics Silicon Backplane found at address 0x%08lX\n",
			baseaddr);
	}

	return err;
}
#endif

int __ssbn_driver_register(struct ssbn_driver *drv, struct module *owner)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &ssbn_bustype;
	drv->drv.owner = owner;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL(__ssbn_driver_register);

void ssbn_driver_unregister(struct ssbn_driver *drv)
{
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL(ssbn_driver_unregister);

void ssbn_set_devtypedata(struct ssbn_device *dev, void *data)
{
	struct ssbn_bus *bus = dev->bus;
	struct ssbn_device *ent;
	int i;

	for (i = 0; i < bus->nr_devices; i++) {
		ent = &(bus->devices[i]);
		if (ent->id.vendor != dev->id.vendor)
			continue;
		if (ent->id.coreid != dev->id.coreid)
			continue;

		ent->devtypedata = data;
	}
}
EXPORT_SYMBOL(ssbn_set_devtypedata);

static u32 clkfactor_f6_resolve(u32 v)
{
	/* map the magic values */
	switch (v) {
	case SSBN_CHIPCO_CLK_F6_2:
		return 2;
	case SSBN_CHIPCO_CLK_F6_3:
		return 3;
	case SSBN_CHIPCO_CLK_F6_4:
		return 4;
	case SSBN_CHIPCO_CLK_F6_5:
		return 5;
	case SSBN_CHIPCO_CLK_F6_6:
		return 6;
	case SSBN_CHIPCO_CLK_F6_7:
		return 7;
	}
	return 1;
}

/* Calculate the speed the backplane would run at a given set of clockcontrol values */
u32 ssbn_calc_clock_rate(u32 plltype, u32 n, u32 m)
{
	u32 n1, n2, clock, m1, m2, m3, mc;

	n1 = (n & SSBN_CHIPCO_CLK_N1);
	n2 = ((n & SSBN_CHIPCO_CLK_N2) >> SSBN_CHIPCO_CLK_N2_SHIFT);

	switch (plltype) {
	case SSBN_PLLTYPE_6: /* 100/200 or 120/240 only */
		if (m & SSBN_CHIPCO_CLK_T6_MMASK)
			return SSBN_CHIPCO_CLK_T6_M1;
		return SSBN_CHIPCO_CLK_T6_M0;
	case SSBN_PLLTYPE_1: /* 48Mhz base, 3 dividers */
	case SSBN_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSBN_PLLTYPE_4: /* 48Mhz, 4 dividers */
	case SSBN_PLLTYPE_7: /* 25Mhz, 4 dividers */
		n1 = clkfactor_f6_resolve(n1);
		n2 += SSBN_CHIPCO_CLK_F5_BIAS;
		break;
	case SSBN_PLLTYPE_2: /* 48Mhz, 4 dividers */
		n1 += SSBN_CHIPCO_CLK_T2_BIAS;
		n2 += SSBN_CHIPCO_CLK_T2_BIAS;
		WARN_ON(!((n1 >= 2) && (n1 <= 7)));
		WARN_ON(!((n2 >= 5) && (n2 <= 23)));
		break;
	case SSBN_PLLTYPE_5: /* 25Mhz, 4 dividers */
		return 100000000;
	default:
		WARN_ON(1);
	}

	switch (plltype) {
	case SSBN_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSBN_PLLTYPE_7: /* 25Mhz, 4 dividers */
		clock = SSBN_CHIPCO_CLK_BASE2 * n1 * n2;
		break;
	default:
		clock = SSBN_CHIPCO_CLK_BASE1 * n1 * n2;
	}
	if (!clock)
		return 0;

	m1 = (m & SSBN_CHIPCO_CLK_M1);
	m2 = ((m & SSBN_CHIPCO_CLK_M2) >> SSBN_CHIPCO_CLK_M2_SHIFT);
	m3 = ((m & SSBN_CHIPCO_CLK_M3) >> SSBN_CHIPCO_CLK_M3_SHIFT);
	mc = ((m & SSBN_CHIPCO_CLK_MC) >> SSBN_CHIPCO_CLK_MC_SHIFT);

	switch (plltype) {
	case SSBN_PLLTYPE_1: /* 48Mhz base, 3 dividers */
	case SSBN_PLLTYPE_3: /* 25Mhz, 2 dividers */
	case SSBN_PLLTYPE_4: /* 48Mhz, 4 dividers */
	case SSBN_PLLTYPE_7: /* 25Mhz, 4 dividers */
		m1 = clkfactor_f6_resolve(m1);
		if ((plltype == SSBN_PLLTYPE_1) ||
		    (plltype == SSBN_PLLTYPE_3))
			m2 += SSBN_CHIPCO_CLK_F5_BIAS;
		else
			m2 = clkfactor_f6_resolve(m2);
		m3 = clkfactor_f6_resolve(m3);

		switch (mc) {
		case SSBN_CHIPCO_CLK_MC_BYPASS:
			return clock;
		case SSBN_CHIPCO_CLK_MC_M1:
			return (clock / m1);
		case SSBN_CHIPCO_CLK_MC_M1M2:
			return (clock / (m1 * m2));
		case SSBN_CHIPCO_CLK_MC_M1M2M3:
			return (clock / (m1 * m2 * m3));
		case SSBN_CHIPCO_CLK_MC_M1M3:
			return (clock / (m1 * m3));
		}
		return 0;
	case SSBN_PLLTYPE_2:
		m1 += SSBN_CHIPCO_CLK_T2_BIAS;
		m2 += SSBN_CHIPCO_CLK_T2M2_BIAS;
		m3 += SSBN_CHIPCO_CLK_T2_BIAS;
		WARN_ON(!((m1 >= 2) && (m1 <= 7)));
		WARN_ON(!((m2 >= 3) && (m2 <= 10)));
		WARN_ON(!((m3 >= 2) && (m3 <= 7)));

		if (!(mc & SSBN_CHIPCO_CLK_T2MC_M1BYP))
			clock /= m1;
		if (!(mc & SSBN_CHIPCO_CLK_T2MC_M2BYP))
			clock /= m2;
		if (!(mc & SSBN_CHIPCO_CLK_T2MC_M3BYP))
			clock /= m3;
		return clock;
	default:
		WARN_ON(1);
	}
	return 0;
}

/* Get the current speed the backplane is running at */
u32 ssbn_clockspeed(struct ssbn_bus *bus)
{
	u32 rate;
	u32 plltype;
	u32 clkctl_n, clkctl_m;

	if (bus->chipco.capabilities & SSBN_CHIPCO_CAP_PMU)
		return ssbn_pmu_get_controlclock(&bus->chipco);

	if (ssbn_extif_available(&bus->extif))
		ssbn_extif_get_clockcontrol(&bus->extif, &plltype,
					   &clkctl_n, &clkctl_m);
	else if (bus->chipco.dev)
		ssbn_chipco_get_clockcontrol(&bus->chipco, &plltype,
					    &clkctl_n, &clkctl_m);
	else
		return 0;

	if (bus->chip_id == 0x5365) {
		rate = 100000000;
	} else {
		rate = ssbn_calc_clock_rate(plltype, clkctl_n, clkctl_m);
		if (plltype == SSBN_PLLTYPE_3) /* 25Mhz, 2 dividers */
			rate /= 2;
	}

	return rate;
}
EXPORT_SYMBOL(ssbn_clockspeed);

static u32 ssbn_tmslow_reject_bitmask(struct ssbn_device *dev)
{
	u32 rev = ssbn_read32(dev, SSBN_IDLOW) & SSBN_IDLOW_SSBREV;

	/* The REJECT bit seems to be different for Backplane rev 2.3 */
	switch (rev) {
	case SSBN_IDLOW_SSBREV_22:
	case SSBN_IDLOW_SSBREV_24:
	case SSBN_IDLOW_SSBREV_26:
		return SSBN_TMSLOW_REJECT;
	case SSBN_IDLOW_SSBREV_23:
		return SSBN_TMSLOW_REJECT_23;
	case SSBN_IDLOW_SSBREV_25:     /* TODO - find the proper REJECT bit */
	case SSBN_IDLOW_SSBREV_27:     /* same here */
		return SSBN_TMSLOW_REJECT;	/* this is a guess */
	case SSBN_IDLOW_SSBREV:
		break;
	default:
		WARN(1, KERN_INFO "ssb: Backplane Revision 0x%.8X\n", rev);
	}
	return (SSBN_TMSLOW_REJECT | SSBN_TMSLOW_REJECT_23);
}

int ssbn_device_is_enabled(struct ssbn_device *dev)
{
	u32 val;
	u32 reject;

	reject = ssbn_tmslow_reject_bitmask(dev);
	val = ssbn_read32(dev, SSBN_TMSLOW);
	val &= SSBN_TMSLOW_CLOCK | SSBN_TMSLOW_RESET | reject;

	return (val == SSBN_TMSLOW_CLOCK);
}
EXPORT_SYMBOL(ssbn_device_is_enabled);

static void ssbn_flush_tmslow(struct ssbn_device *dev)
{
	/* Make _really_ sure the device has finished the TMSLOW
	 * register write transaction, as we risk running into
	 * a machine check exception otherwise.
	 * Do this by reading the register back to commit the
	 * PCI write and delay an additional usec for the device
	 * to react to the change.
	 */
	ssbn_read32(dev, SSBN_TMSLOW);
	udelay(1);
}

void ssbn_device_enable(struct ssbn_device *dev, u32 core_specific_flags)
{
	u32 val;

	ssbn_device_disable(dev, core_specific_flags);
	ssbn_write32(dev, SSBN_TMSLOW,
		    SSBN_TMSLOW_RESET | SSBN_TMSLOW_CLOCK |
		    SSBN_TMSLOW_FGC | core_specific_flags);
	ssbn_flush_tmslow(dev);

	/* Clear SERR if set. This is a hw bug workaround. */
	if (ssbn_read32(dev, SSBN_TMSHIGH) & SSBN_TMSHIGH_SERR)
		ssbn_write32(dev, SSBN_TMSHIGH, 0);

	val = ssbn_read32(dev, SSBN_IMSTATE);
	if (val & (SSBN_IMSTATE_IBE | SSBN_IMSTATE_TO)) {
		val &= ~(SSBN_IMSTATE_IBE | SSBN_IMSTATE_TO);
		ssbn_write32(dev, SSBN_IMSTATE, val);
	}

	ssbn_write32(dev, SSBN_TMSLOW,
		    SSBN_TMSLOW_CLOCK | SSBN_TMSLOW_FGC |
		    core_specific_flags);
	ssbn_flush_tmslow(dev);

	ssbn_write32(dev, SSBN_TMSLOW, SSBN_TMSLOW_CLOCK |
		    core_specific_flags);
	ssbn_flush_tmslow(dev);
}
EXPORT_SYMBOL(ssbn_device_enable);

/* Wait for bitmask in a register to get set or cleared.
 * timeout is in units of ten-microseconds
 */
static int ssbn_wait_bits(struct ssbn_device *dev, u16 reg, u32 bitmask,
			 int timeout, int set)
{
	int i;
	u32 val;

	for (i = 0; i < timeout; i++) {
		val = ssbn_read32(dev, reg);
		if (set) {
			if ((val & bitmask) == bitmask)
				return 0;
		} else {
			if (!(val & bitmask))
				return 0;
		}
		udelay(10);
	}
	dev_err(dev->dev,
		"Timeout waiting for bitmask %08X on register %04X to %s\n",
		bitmask, reg, set ? "set" : "clear");

	return -ETIMEDOUT;
}

void ssbn_device_disable(struct ssbn_device *dev, u32 core_specific_flags)
{
	u32 reject, val;

	if (ssbn_read32(dev, SSBN_TMSLOW) & SSBN_TMSLOW_RESET)
		return;

	reject = ssbn_tmslow_reject_bitmask(dev);

	if (ssbn_read32(dev, SSBN_TMSLOW) & SSBN_TMSLOW_CLOCK) {
		ssbn_write32(dev, SSBN_TMSLOW, reject | SSBN_TMSLOW_CLOCK);
		ssbn_wait_bits(dev, SSBN_TMSLOW, reject, 1000, 1);
		ssbn_wait_bits(dev, SSBN_TMSHIGH, SSBN_TMSHIGH_BUSY, 1000, 0);

		if (ssbn_read32(dev, SSBN_IDLOW) & SSBN_IDLOW_INITIATOR) {
			val = ssbn_read32(dev, SSBN_IMSTATE);
			val |= SSBN_IMSTATE_REJECT;
			ssbn_write32(dev, SSBN_IMSTATE, val);
			ssbn_wait_bits(dev, SSBN_IMSTATE, SSBN_IMSTATE_BUSY, 1000,
				      0);
		}

		ssbn_write32(dev, SSBN_TMSLOW,
			SSBN_TMSLOW_FGC | SSBN_TMSLOW_CLOCK |
			reject | SSBN_TMSLOW_RESET |
			core_specific_flags);
		ssbn_flush_tmslow(dev);

		if (ssbn_read32(dev, SSBN_IDLOW) & SSBN_IDLOW_INITIATOR) {
			val = ssbn_read32(dev, SSBN_IMSTATE);
			val &= ~SSBN_IMSTATE_REJECT;
			ssbn_write32(dev, SSBN_IMSTATE, val);
		}
	}

	ssbn_write32(dev, SSBN_TMSLOW,
		    reject | SSBN_TMSLOW_RESET |
		    core_specific_flags);
	ssbn_flush_tmslow(dev);
}
EXPORT_SYMBOL(ssbn_device_disable);

/* Some chipsets need routing known for PCIe and 64-bit DMA */
static bool ssbn_dma_translation_special_bit(struct ssbn_device *dev)
{
	u16 chip_id = dev->bus->chip_id;

	if (dev->id.coreid == SSBN_DEV_80211) {
		return (chip_id == 0x4322 || chip_id == 43221 ||
			chip_id == 43231 || chip_id == 43222);
	}

	return false;
}

u32 ssbn_dma_translation(struct ssbn_device *dev)
{
	switch (dev->bus->bustype) {
	case SSBN_BUSTYPE_SSB:
		return 0;
	case SSBN_BUSTYPE_PCI:
		if (pci_is_pcie(dev->bus->host_pci) &&
		    ssbn_read32(dev, SSBN_TMSHIGH) & SSBN_TMSHIGH_DMA64) {
			return SSBN_PCIE_DMA_H32;
		} else {
			if (ssbn_dma_translation_special_bit(dev))
				return SSBN_PCIE_DMA_H32;
			else
				return SSBN_PCI_DMA;
		}
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL(ssbn_dma_translation);

int ssbn_bus_may_powerdown(struct ssbn_bus *bus)
{
	struct ssbn_chipcommon *cc;
	int err = 0;

	/* On buses where more than one core may be working
	 * at a time, we must not powerdown stuff if there are
	 * still cores that may want to run.
	 */
	if (bus->bustype == SSBN_BUSTYPE_SSB)
		goto out;

	cc = &bus->chipco;

	if (!cc->dev)
		goto out;
	if (cc->dev->id.revision < 5)
		goto out;

	ssbn_chipco_set_clockmode(cc, SSBN_CLKMODE_SLOW);
	err = ssbn_pci_xtal(bus, SSBN_GPIO_XTAL | SSBN_GPIO_PLL, 0);
	if (err)
		goto error;
out:
	bus->powered_up = 0;
	return err;
error:
	pr_err("Bus powerdown failed\n");
	goto out;
}
EXPORT_SYMBOL(ssbn_bus_may_powerdown);

int ssbn_bus_powerup(struct ssbn_bus *bus, bool dynamic_pctl)
{
	int err;
	enum ssbn_clkmode mode;

	err = ssbn_pci_xtal(bus, SSBN_GPIO_XTAL | SSBN_GPIO_PLL, 1);
	if (err)
		goto error;

	bus->powered_up = 1;

	mode = dynamic_pctl ? SSBN_CLKMODE_DYNAMIC : SSBN_CLKMODE_FAST;
	ssbn_chipco_set_clockmode(&bus->chipco, mode);

	return 0;
error:
	pr_err("Bus powerup failed\n");
	return err;
}
EXPORT_SYMBOL(ssbn_bus_powerup);

static void ssbn_broadcast_value(struct ssbn_device *dev,
				u32 address, u32 data)
{
#ifdef CONFIG_SSBN_DRIVER_PCICORE
	/* This is used for both, PCI and ChipCommon core, so be careful. */
	BUILD_BUG_ON(SSBN_PCICORE_BCAST_ADDR != SSBN_CHIPCO_BCAST_ADDR);
	BUILD_BUG_ON(SSBN_PCICORE_BCAST_DATA != SSBN_CHIPCO_BCAST_DATA);
#endif

	ssbn_write32(dev, SSBN_CHIPCO_BCAST_ADDR, address);
	ssbn_read32(dev, SSBN_CHIPCO_BCAST_ADDR); /* flush */
	ssbn_write32(dev, SSBN_CHIPCO_BCAST_DATA, data);
	ssbn_read32(dev, SSBN_CHIPCO_BCAST_DATA); /* flush */
}

void ssbn_commit_settings(struct ssbn_bus *bus)
{
	struct ssbn_device *dev;

#ifdef CONFIG_SSBN_DRIVER_PCICORE
	dev = bus->chipco.dev ? bus->chipco.dev : bus->pcicore.dev;
#else
	dev = bus->chipco.dev;
#endif
	if (WARN_ON(!dev))
		return;
	/* This forces an update of the cached registers. */
	ssbn_broadcast_value(dev, 0xFD8, 0);
}
EXPORT_SYMBOL(ssbn_commit_settings);

u32 ssbn_admatch_base(u32 adm)
{
	u32 base = 0;

	switch (adm & SSBN_ADM_TYPE) {
	case SSBN_ADM_TYPE0:
		base = (adm & SSBN_ADM_BASE0);
		break;
	case SSBN_ADM_TYPE1:
		WARN_ON(adm & SSBN_ADM_NEG); /* unsupported */
		base = (adm & SSBN_ADM_BASE1);
		break;
	case SSBN_ADM_TYPE2:
		WARN_ON(adm & SSBN_ADM_NEG); /* unsupported */
		base = (adm & SSBN_ADM_BASE2);
		break;
	default:
		WARN_ON(1);
	}

	return base;
}
EXPORT_SYMBOL(ssbn_admatch_base);

u32 ssbn_admatch_size(u32 adm)
{
	u32 size = 0;

	switch (adm & SSBN_ADM_TYPE) {
	case SSBN_ADM_TYPE0:
		size = ((adm & SSBN_ADM_SZ0) >> SSBN_ADM_SZ0_SHIFT);
		break;
	case SSBN_ADM_TYPE1:
		WARN_ON(adm & SSBN_ADM_NEG); /* unsupported */
		size = ((adm & SSBN_ADM_SZ1) >> SSBN_ADM_SZ1_SHIFT);
		break;
	case SSBN_ADM_TYPE2:
		WARN_ON(adm & SSBN_ADM_NEG); /* unsupported */
		size = ((adm & SSBN_ADM_SZ2) >> SSBN_ADM_SZ2_SHIFT);
		break;
	default:
		WARN_ON(1);
	}
	size = (1 << (size + 1));

	return size;
}
EXPORT_SYMBOL(ssbn_admatch_size);

int __init ssbn_modinit(void)
{
	int err;

	/* See the comment at the ssbn_is_early_boot definition */
	ssbn_is_early_boot = 0;
	err = bus_register(&ssbn_bustype);
	if (err)
		return err;

	/* Maybe we already registered some buses at early boot.
	 * Check for this and attach them
	 */
	ssbn_buses_lock();
	err = ssbn_attach_queued_buses();
	ssbn_buses_unlock();
	if (err) {
		bus_unregister(&ssbn_bustype);
		goto out;
	}

	err = b43_pci_ssb_bridge_init();
	if (err) {
		pr_err("Broadcom 43xx PCI-SSB-bridge initialization failed\n");
		/* don't fail SSB init because of this */
	}
	err = ssbn_host_pcmcia_init();
	if (err) {
		pr_err("PCMCIA host initialization failed\n");
		/* don't fail SSB init because of this */
	}
	err = ssbn_gige_init();
	if (err) {
		pr_err("SSB Broadcom Gigabit Ethernet driver initialization failed\n");
		/* don't fail SSB init because of this */
		err = 0;
	}
out:
	return err;
}

void ssbn_modexit(void)
{
	ssbn_gige_exit();
	ssbn_host_pcmcia_exit();
	b43_pci_ssb_bridge_exit();
	bus_unregister(&ssbn_bustype);
}
