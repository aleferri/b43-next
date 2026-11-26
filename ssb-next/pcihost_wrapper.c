/*
 * Sonics Silicon Backplane
 * PCI Hostdevice wrapper
 *
 * Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (c) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 * Copyright (c) 2005-2007 Michael Buesch <m@bues.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <ssb-next/ssbn.h>


#ifdef CONFIG_PM_SLEEP
static int ssbn_pcihost_suspend(struct device *d)
{
	struct pci_dev *dev = to_pci_dev(d);
	struct ssbn_bus *ssb = pci_get_drvdata(dev);
	int err;

	err = ssbn_bus_suspend(ssb);
	if (err)
		return err;
	pci_save_state(dev);
	pci_disable_device(dev);

	/* if there is a wakeup enabled child device on ssb bus,
	   enable pci wakeup posibility. */
	device_set_wakeup_enable(d, d->power.wakeup_path);

	pci_prepare_to_sleep(dev);

	return 0;
}

static int ssbn_pcihost_resume(struct device *d)
{
	struct pci_dev *dev = to_pci_dev(d);
	struct ssbn_bus *ssb = pci_get_drvdata(dev);
	int err;

	pci_back_from_sleep(dev);
	err = pci_enable_device(dev);
	if (err)
		return err;
	pci_restore_state(dev);
	err = ssbn_bus_resume(ssb);
	if (err)
		return err;

	return 0;
}

static const struct dev_pm_ops ssbn_pcihost_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ssbn_pcihost_suspend, ssbn_pcihost_resume)
};

#endif /* CONFIG_PM_SLEEP */

static int ssbn_pcihost_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	struct ssbn_bus *ssb;
	int err;
	u32 val;

	ssb = devm_kzalloc(&dev->dev, sizeof(*ssb), GFP_KERNEL);
	if (!ssb)
		return -ENOMEM;
	err = pcim_enable_device(dev);
	if (err)
		return err;
	err = pcim_request_region(dev, 0, dev_driver_string(&dev->dev));
	if (err)
		return err;
	pci_set_master(dev);

	/* Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_read_config_dword(dev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(dev, 0x40, val & 0xffff00ff);

	err = ssbn_bus_pcibus_register(ssb, dev);
	if (err)
		return err;

	pci_set_drvdata(dev, ssb);

	return 0;
}

static void ssbn_pcihost_remove(struct pci_dev *dev)
{
	struct ssbn_bus *ssb = pci_get_drvdata(dev);

	ssbn_bus_unregister(ssb);
}

int ssbn_pcihost_register(struct pci_driver *driver)
{
	driver->probe = ssbn_pcihost_probe;
	driver->remove = ssbn_pcihost_remove;
#ifdef CONFIG_PM_SLEEP
	driver->driver.pm = &ssbn_pcihost_pm_ops;
#endif

	return pci_register_driver(driver);
}
