/*
 * Sonics Silicon Backplane
 * Common SPROM support routines
 *
 * Copyright (C) 2005-2008 Michael Buesch <m@bues.ch>
 * Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
 * Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
 * Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
 * Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "ssbn_private.h"
#include "../nvram/bcm47xx_sprom.h"

#include <linux/ctype.h>
#include <linux/slab.h>


static int sprom2hex(const u16 *sprom, char *buf, size_t buf_len,
		     size_t sprom_size_words)
{
	int i, pos = 0;

	for (i = 0; i < sprom_size_words; i++)
		pos += scnprintf(buf + pos, buf_len - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	pos += scnprintf(buf + pos, buf_len - pos - 1, "\n");

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, size_t len,
		     size_t sprom_size_words)
{
	char c, tmp[5] = { 0 };
	int err, cnt = 0;
	unsigned long parsed;

	/* Strip whitespace at the end. */
	while (len) {
		c = dump[len - 1];
		if (!isspace(c) && c != '\0')
			break;
		len--;
	}
	/* Length must match exactly. */
	if (len != sprom_size_words * 4)
		return -EINVAL;

	while (cnt < sprom_size_words) {
		memcpy(tmp, dump, 4);
		dump += 4;
		err = kstrtoul(tmp, 16, &parsed);
		if (err)
			return err;
		sprom[cnt++] = swab16((u16)parsed);
	}

	return 0;
}

/* Common sprom device-attribute show-handler */
ssize_t ssbn_attr_sprom_show(struct ssbn_bus *bus, char *buf,
			    int (*sprom_read)(struct ssbn_bus *bus, u16 *sprom))
{
	u16 *sprom;
	int err = -ENOMEM;
	ssize_t count = 0;
	size_t sprom_size_words = bus->sprom_size;

	sprom = kcalloc(sprom_size_words, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation.
	 */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->sprom_mutex))
		goto out_kfree;
	err = sprom_read(bus, sprom);
	mutex_unlock(&bus->sprom_mutex);

	if (!err)
		count = sprom2hex(sprom, buf, PAGE_SIZE, sprom_size_words);

out_kfree:
	kfree(sprom);
out:
	return err ? err : count;
}

/* Common sprom device-attribute store-handler */
ssize_t ssbn_attr_sprom_store(struct ssbn_bus *bus,
			     const char *buf, size_t count,
			     int (*sprom_check_crc)(const u16 *sprom, size_t size),
			     int (*sprom_write)(struct ssbn_bus *bus, const u16 *sprom))
{
	u16 *sprom;
	int res = 0, err = -ENOMEM;
	size_t sprom_size_words = bus->sprom_size;
	struct ssbn_freeze_context freeze;

	sprom = kcalloc(bus->sprom_size, sizeof(u16), GFP_KERNEL);
	if (!sprom)
		goto out;
	err = hex2sprom(sprom, buf, count, sprom_size_words);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}
	err = sprom_check_crc(sprom, sprom_size_words);
	if (err) {
		err = -EINVAL;
		goto out_kfree;
	}

	/* Use interruptible locking, as the SPROM write might
	 * be holding the lock for several seconds. So allow userspace
	 * to cancel operation.
	 */
	err = -ERESTARTSYS;
	if (mutex_lock_interruptible(&bus->sprom_mutex))
		goto out_kfree;
	err = ssbn_devices_freeze(bus, &freeze);
	if (err) {
		pr_err("SPROM write: Could not freeze all devices\n");
		goto out_unlock;
	}
	res = sprom_write(bus, sprom);
	err = ssbn_devices_thaw(&freeze);
	if (err)
		pr_err("SPROM write: Could not thaw all devices\n");
out_unlock:
	mutex_unlock(&bus->sprom_mutex);
out_kfree:
	kfree(sprom);
out:
	if (res)
		return res;
	return err ? err : count;
}

int ssbn_fill_sprom_with_fallback(struct ssbn_bus *bus, struct ssbn_sprom *out)
{
	return bcm47xx_get_sprom_ssb(bus, out);
}

/* https://bcm-v4.sipsolutions.net/802.11/IsSpromAvailable */
bool ssbn_is_sprom_available(struct ssbn_bus *bus)
{
	/* status register only exists on chipcomon rev >= 11 and we need check
	 * for >= 31 only
	 */
	/* this routine differs from specs as we do not access SPROM directly
	 * on PCMCIA
	 */
	if (bus->bustype == SSBN_BUSTYPE_PCI &&
	    bus->chipco.dev &&	/* can be unavailable! */
	    bus->chipco.dev->id.revision >= 31)
		return bus->chipco.capabilities & SSBN_CHIPCO_CAP_SPROM;

	return true;
}
