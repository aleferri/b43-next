// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver

  debugfs driver debugging code

  Copyright (c) 2005-2007 Michael Buesch <m@bues.ch>


*/

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include "b43.h"
#include "main.h"
#include "debugfs.h"
#include "calstat.h"
#include "dma.h"
#include "xmit.h"


/* The root directory. */
static struct dentry *rootdir;

struct b43_debugfs_fops {
	ssize_t (*read)(struct b43_wldev *dev, char *buf, size_t bufsize);
	int (*write)(struct b43_wldev *dev, const char *buf, size_t count);
	/* Offset of struct b43_dfs_file in struct b43_dfsentry */
	size_t file_struct_offset;
};

static inline
struct b43_dfs_file *fops_to_dfs_file(struct b43_wldev *dev,
				      const struct b43_debugfs_fops *dfops)
{
	void *p;

	p = dev->dfsentry;
	p += dfops->file_struct_offset;

	return p;
}


#define fappend(fmt, x...)	\
	do {							\
		if (bufsize - count)				\
			count += scnprintf(buf + count,		\
					  bufsize - count,	\
					  fmt , ##x);		\
		else						\
			pr_err("b43: fappend overflow\n");	\
	} while (0)


/* The biggest address values for SHM access from the debugfs files. */
#define B43_MAX_SHM_ROUTING	4
#define B43_MAX_SHM_ADDR	0xFFFF

static ssize_t shm16read__read_file(struct b43_wldev *dev,
				    char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int routing, addr;
	u16 val;

	routing = dev->dfsentry->shm16read_routing_next;
	addr = dev->dfsentry->shm16read_addr_next;
	if ((routing > B43_MAX_SHM_ROUTING) ||
	    (addr > B43_MAX_SHM_ADDR))
		return -EDESTADDRREQ;

	val = b43_shm_read16(dev, routing, addr);
	fappend("0x%04X\n", val);

	return count;
}

static int shm16read__write_file(struct b43_wldev *dev,
				 const char *buf, size_t count)
{
	unsigned int routing, addr;
	int res;

	res = sscanf(buf, "0x%X 0x%X", &routing, &addr);
	if (res != 2)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}

	dev->dfsentry->shm16read_routing_next = routing;
	dev->dfsentry->shm16read_addr_next = addr;

	return 0;
}

static int shm16write__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int routing, addr, mask, set;
	u16 val;
	int res;

	res = sscanf(buf, "0x%X 0x%X 0x%X 0x%X",
		     &routing, &addr, &mask, &set);
	if (res != 4)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}
	if ((mask > 0xFFFF) || (set > 0xFFFF))
		return -E2BIG;

	if (mask == 0)
		val = 0;
	else
		val = b43_shm_read16(dev, routing, addr);
	val &= mask;
	val |= set;
	b43_shm_write16(dev, routing, addr, val);

	return 0;
}

static ssize_t shm32read__read_file(struct b43_wldev *dev,
				    char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int routing, addr;
	u32 val;

	routing = dev->dfsentry->shm32read_routing_next;
	addr = dev->dfsentry->shm32read_addr_next;
	if ((routing > B43_MAX_SHM_ROUTING) ||
	    (addr > B43_MAX_SHM_ADDR))
		return -EDESTADDRREQ;

	val = b43_shm_read32(dev, routing, addr);
	fappend("0x%08X\n", val);

	return count;
}

static int shm32read__write_file(struct b43_wldev *dev,
				 const char *buf, size_t count)
{
	unsigned int routing, addr;
	int res;

	res = sscanf(buf, "0x%X 0x%X", &routing, &addr);
	if (res != 2)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}

	dev->dfsentry->shm32read_routing_next = routing;
	dev->dfsentry->shm32read_addr_next = addr;

	return 0;
}

static int shm32write__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int routing, addr, mask, set;
	u32 val;
	int res;

	res = sscanf(buf, "0x%X 0x%X 0x%X 0x%X",
		     &routing, &addr, &mask, &set);
	if (res != 4)
		return -EINVAL;
	if (routing > B43_MAX_SHM_ROUTING)
		return -EADDRNOTAVAIL;
	if (addr > B43_MAX_SHM_ADDR)
		return -EADDRNOTAVAIL;
	if (routing == B43_SHM_SHARED) {
		if ((addr % 2) != 0)
			return -EADDRNOTAVAIL;
	}
	if ((mask > 0xFFFFFFFF) || (set > 0xFFFFFFFF))
		return -E2BIG;

	if (mask == 0)
		val = 0;
	else
		val = b43_shm_read32(dev, routing, addr);
	val &= mask;
	val |= set;
	b43_shm_write32(dev, routing, addr, val);

	return 0;
}

/* The biggest MMIO address that we allow access to from the debugfs files. */
#define B43_MAX_MMIO_ACCESS	(0xF00 - 1)

static ssize_t mmio16read__read_file(struct b43_wldev *dev,
				     char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int addr;
	u16 val;

	addr = dev->dfsentry->mmio16read_next;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EDESTADDRREQ;

	val = b43_read16(dev, addr);
	fappend("0x%04X\n", val);

	return count;
}

static int mmio16read__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int addr;

	if (kstrtouint(buf, 16, &addr))
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((addr % 2) != 0)
		return -EINVAL;

	dev->dfsentry->mmio16read_next = addr;

	return 0;
}

static int mmio16write__write_file(struct b43_wldev *dev,
				   const char *buf, size_t count)
{
	unsigned int addr, mask, set;
	int res;
	u16 val;

	res = sscanf(buf, "0x%X 0x%X 0x%X", &addr, &mask, &set);
	if (res != 3)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((mask > 0xFFFF) || (set > 0xFFFF))
		return -E2BIG;
	if ((addr % 2) != 0)
		return -EINVAL;

	if (mask == 0)
		val = 0;
	else
		val = b43_read16(dev, addr);
	val &= mask;
	val |= set;
	b43_write16(dev, addr, val);

	return 0;
}

static ssize_t mmio32read__read_file(struct b43_wldev *dev,
				     char *buf, size_t bufsize)
{
	ssize_t count = 0;
	unsigned int addr;
	u32 val;

	addr = dev->dfsentry->mmio32read_next;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EDESTADDRREQ;

	val = b43_read32(dev, addr);
	fappend("0x%08X\n", val);

	return count;
}

static int mmio32read__write_file(struct b43_wldev *dev,
				  const char *buf, size_t count)
{
	unsigned int addr;

	if (kstrtouint(buf, 16, &addr))
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((addr % 4) != 0)
		return -EINVAL;

	dev->dfsentry->mmio32read_next = addr;

	return 0;
}

static int mmio32write__write_file(struct b43_wldev *dev,
				   const char *buf, size_t count)
{
	unsigned int addr, mask, set;
	int res;
	u32 val;

	res = sscanf(buf, "0x%X 0x%X 0x%X", &addr, &mask, &set);
	if (res != 3)
		return -EINVAL;
	if (addr > B43_MAX_MMIO_ACCESS)
		return -EADDRNOTAVAIL;
	if ((mask > 0xFFFFFFFF) || (set > 0xFFFFFFFF))
		return -E2BIG;
	if ((addr % 4) != 0)
		return -EINVAL;

	if (mask == 0)
		val = 0;
	else
		val = b43_read32(dev, addr);
	val &= mask;
	val |= set;
	b43_write32(dev, addr, val);

	return 0;
}

static ssize_t txstat_read_file(struct b43_wldev *dev,
				char *buf, size_t bufsize)
{
	struct b43_txstatus_log *log = &dev->dfsentry->txstatlog;
	ssize_t count = 0;
	int i, idx;
	struct b43_txstatus *stat;

	if (log->end < 0) {
		fappend("Nothing transmitted, yet\n");
		goto out;
	}
	fappend("b43 TX status reports:\n\n"
		"index | cookie | seq | phy_stat | frame_count | "
		"rts_count | supp_reason | pm_indicated | "
		"intermediate | for_ampdu | acked\n" "---\n");
	i = log->end + 1;
	idx = 0;
	while (1) {
		if (i == B43_NR_LOGGED_TXSTATUS)
			i = 0;
		stat = &(log->log[i]);
		if (stat->cookie) {
			fappend("%03d | "
				"0x%04X | 0x%04X | 0x%02X | "
				"0x%X | 0x%X | "
				"%u | %u | "
				"%u | %u | %u\n",
				idx,
				stat->cookie, stat->seq, stat->phy_stat,
				stat->frame_count, stat->rts_count,
				stat->supp_reason, stat->pm_indicated,
				stat->intermediate, stat->for_ampdu,
				stat->acked);
			idx++;
		}
		if (i == log->end)
			break;
		i++;
	}
out:

	return count;
}

static int restart_write_file(struct b43_wldev *dev,
			      const char *buf, size_t count)
{
	int err = 0;

	if (count > 0 && buf[0] == '1') {
		b43_controller_restart(dev, "manually restarted");
	} else
		err = -EINVAL;

	return err;
}

static unsigned long calc_expire_secs(unsigned long now,
				      unsigned long time,
				      unsigned long expire)
{
	expire = time + expire;

	if (time_after(now, expire))
		return 0; /* expired */
	if (expire < now) {
		/* jiffies wrapped */
		expire -= MAX_JIFFY_OFFSET;
		now -= MAX_JIFFY_OFFSET;
	}
	B43_WARN_ON(expire < now);

	return (expire - now) / HZ;
}

static ssize_t loctls_read_file(struct b43_wldev *dev,
				char *buf, size_t bufsize)
{
	ssize_t count = 0;
	struct b43_txpower_lo_control *lo;
	int i, err = 0;
	struct b43_lo_calib *cal;
	unsigned long now = jiffies;
	struct b43_phy *phy = &dev->phy;

	if (phy->type != B43_PHYTYPE_G) {
		fappend("Device is not a G-PHY\n");
		err = -ENODEV;
		goto out;
	}
	lo = phy->g->lo_control;
	fappend("-- Local Oscillator calibration data --\n\n");
	fappend("HW-power-control enabled: %d\n",
		dev->phy.hardware_power_control);
	fappend("TX Bias: 0x%02X,  TX Magn: 0x%02X  (expire in %lu sec)\n",
		lo->tx_bias, lo->tx_magn,
		calc_expire_secs(now, lo->txctl_measured_time,
				 B43_LO_TXCTL_EXPIRE));
	fappend("Power Vector: 0x%08X%08X  (expires in %lu sec)\n",
		(unsigned int)((lo->power_vector & 0xFFFFFFFF00000000ULL) >> 32),
		(unsigned int)(lo->power_vector & 0x00000000FFFFFFFFULL),
		calc_expire_secs(now, lo->pwr_vec_read_time,
				 B43_LO_PWRVEC_EXPIRE));

	fappend("\nCalibrated settings:\n");
	list_for_each_entry(cal, &lo->calib_list, list) {
		bool active;

		active = (b43_compare_bbatt(&cal->bbatt, &phy->g->bbatt) &&
			  b43_compare_rfatt(&cal->rfatt, &phy->g->rfatt));
		fappend("BB(%d), RF(%d,%d)  ->  I=%d, Q=%d  "
			"(expires in %lu sec)%s\n",
			cal->bbatt.att,
			cal->rfatt.att, cal->rfatt.with_padmix,
			cal->ctl.i, cal->ctl.q,
			calc_expire_secs(now, cal->calib_time,
					 B43_LO_CALIB_EXPIRE),
			active ? "  ACTIVE" : "");
	}

	fappend("\nUsed RF attenuation values:  Value(WithPadmix flag)\n");
	for (i = 0; i < lo->rfatt_list.len; i++) {
		fappend("%u(%d), ",
			lo->rfatt_list.list[i].att,
			lo->rfatt_list.list[i].with_padmix);
	}
	fappend("\n");
	fappend("\nUsed Baseband attenuation values:\n");
	for (i = 0; i < lo->bbatt_list.len; i++) {
		fappend("%u, ",
			lo->bbatt_list.list[i].att);
	}
	fappend("\n");

out:
	return err ? err : count;
}

#undef fappend

static ssize_t b43_debugfs_read(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	struct b43_wldev *dev;
	const struct b43_debugfs_fops *dfops;
	struct b43_dfs_file *dfile;
	ssize_t ret;
	char *buf;
	const size_t bufsize = 1024 * 16; /* 16 kiB buffer */
	int err = 0;

	if (!count)
		return 0;
	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->wl->mutex);
	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = debugfs_get_aux(file);
	if (!dfops->read) {
		err = -ENOSYS;
		goto out_unlock;
	}
	dfile = fops_to_dfs_file(dev, dfops);

	if (!dfile->buffer) {
		buf = kzalloc(bufsize, GFP_KERNEL);
		if (!buf) {
			err = -ENOMEM;
			goto out_unlock;
		}
		ret = dfops->read(dev, buf, bufsize);
		if (ret <= 0) {
			kfree(buf);
			err = ret;
			goto out_unlock;
		}
		dfile->data_len = ret;
		dfile->buffer = buf;
	}

	ret = simple_read_from_buffer(userbuf, count, ppos,
				      dfile->buffer,
				      dfile->data_len);
	if (*ppos >= dfile->data_len) {
		kfree(dfile->buffer);
		dfile->buffer = NULL;
		dfile->data_len = 0;
	}
out_unlock:
	mutex_unlock(&dev->wl->mutex);

	return err ? err : ret;
}

static ssize_t b43_debugfs_write(struct file *file,
				 const char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct b43_wldev *dev;
	const struct b43_debugfs_fops *dfops;
	char *buf;
	int err = 0;

	if (!count)
		return 0;
	if (count >= PAGE_SIZE)
		return -E2BIG;
	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	buf = memdup_user_nul(userbuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	mutex_lock(&dev->wl->mutex);
	if (b43_status(dev) < B43_STAT_INITIALIZED) {
		err = -ENODEV;
		goto out_unlock;
	}

	dfops = debugfs_get_aux(file);
	if (!dfops->write) {
		err = -ENOSYS;
		goto out_unlock;
	}

	err = dfops->write(dev, buf, count);
out_unlock:
	mutex_unlock(&dev->wl->mutex);
	kfree(buf);

	return err ? err : count;
}


static struct debugfs_short_fops debugfs_ops = {
	.read	= b43_debugfs_read,
	.write	= b43_debugfs_write,
	.llseek = generic_file_llseek,
};

#define B43_DEBUGFS_FOPS(name, _read, _write)			\
	static struct b43_debugfs_fops fops_##name = {		\
		.read	= _read,				\
		.write	= _write,				\
		.file_struct_offset = offsetof(struct b43_dfsentry, \
					       file_##name),	\
	}

B43_DEBUGFS_FOPS(shm16read, shm16read__read_file, shm16read__write_file);
B43_DEBUGFS_FOPS(shm16write, NULL, shm16write__write_file);
B43_DEBUGFS_FOPS(shm32read, shm32read__read_file, shm32read__write_file);
B43_DEBUGFS_FOPS(shm32write, NULL, shm32write__write_file);
B43_DEBUGFS_FOPS(mmio16read, mmio16read__read_file, mmio16read__write_file);
B43_DEBUGFS_FOPS(mmio16write, NULL, mmio16write__write_file);
B43_DEBUGFS_FOPS(mmio32read, mmio32read__read_file, mmio32read__write_file);
B43_DEBUGFS_FOPS(mmio32write, NULL, mmio32write__write_file);
B43_DEBUGFS_FOPS(txstat, txstat_read_file, NULL);
B43_DEBUGFS_FOPS(restart, NULL, restart_write_file);
B43_DEBUGFS_FOPS(loctls, loctls_read_file, NULL);


#ifdef CONFIG_B43_NEXT_CALSTAT
/* The longest line of an entry: its context and up to 96 values */
#define B43_CAL_LINE_MAX	1024

/* A file of the cal/ directory: one entry's slots, or every slot for -1 */
struct b43_cal_view {
	struct b43_calstat *cs;
	int entry;
};

struct b43_dfs_cal {
	struct b43_calstat cs;
	struct b43_cal_view all;
	struct b43_cal_view *views;
	struct b43_cal_ring rings[];
};

static int b43_cal_show(struct seq_file *s, void *unused)
{
	const struct b43_cal_view *v = s->private;
	struct b43_calstat *cs = v->cs;
	unsigned int entry, age;
	u32 seq = 0;
	char *buf;
	int a;

	buf = kmalloc(B43_CAL_LINE_MAX, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	mutex_lock(&cs->dev->wl->mutex);
	if (v->entry < 0) {
		while (b43_cal_next(cs, &seq, &entry, &age))
			if (b43_cal_format(cs, entry, age, B43_CAL_FMT_CONTEXT,
					   buf, B43_CAL_LINE_MAX) >= 0)
				seq_printf(s, "%s\n", buf);
	} else {
		for (a = cs->rings[v->entry].used - 1; a >= 0; a--)
			if (b43_cal_format(cs, v->entry, a, B43_CAL_FMT_CONTEXT,
					   buf, B43_CAL_LINE_MAX) >= 0)
				seq_printf(s, "%s\n", buf);
	}
	mutex_unlock(&cs->dev->wl->mutex);
	kfree(buf);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(b43_cal);

static ssize_t b43_cal_enable_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct b43_calstat *cs = file->private_data;
	char val[2] = { cs->mem ? '1' : '0', '\n' };

	return simple_read_from_buffer(buf, count, ppos, val, sizeof(val));
}

/* 1 starts new statistics, 0 stops them and frees their storage */
static ssize_t b43_cal_enable_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct b43_calstat *cs = file->private_data;
	u8 *mem = NULL, *old;
	bool on;
	int err;

	err = kstrtobool_from_user(buf, count, &on);
	if (err)
		return err;
	if (on) {
		mem = kvzalloc(cs->size, GFP_KERNEL);
		if (!mem)
			return -ENOMEM;
	}

	mutex_lock(&cs->dev->wl->mutex);
	old = b43_calstat_stop(cs);
	if (mem)
		b43_calstat_start(cs, mem);
	mutex_unlock(&cs->dev->wl->mutex);

	kvfree(old);
	return count;
}

static const struct file_operations b43_cal_enable_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.read	= b43_cal_enable_read,
	.write	= b43_cal_enable_write,
	.llseek	= default_llseek,
};

/* cal/enable, cal/all and cal/<group>/<entry>, from the PHY's table */
static void b43_cal_add(struct b43_wldev *dev, struct b43_dfsentry *e)
{
	const struct b43_phy_operations *ops = dev->phy.ops;
	struct dentry *groups[__B43_NR_CAL_GROUPS] = { };
	const struct b43_cal_entry *ce;
	struct b43_dfs_cal *dc;
	struct dentry *dir;
	unsigned int i;

	if (!ops->nr_cal_entries)
		return;
	dc = kzalloc_flex(*dc, rings, ops->nr_cal_entries);
	if (!dc)
		return;
	dc->views = kzalloc_objs(struct b43_cal_view, ops->nr_cal_entries);
	if (!dc->views) {
		kfree(dc);
		return;
	}
	b43_calstat_setup(&dc->cs, dev, ops->cal_entries, ops->nr_cal_entries,
			  dc->rings);
	e->cal = dc;
	dev->calstat = &dc->cs;

	dir = debugfs_create_dir("cal", e->subdir);
	debugfs_create_file("enable", 0600, dir, &dc->cs,
			    &b43_cal_enable_fops);
	dc->all = (struct b43_cal_view){ .cs = &dc->cs, .entry = -1 };
	debugfs_create_file("all", 0400, dir, &dc->all, &b43_cal_fops);
	for (i = 0; i < ops->nr_cal_entries; i++) {
		ce = &ops->cal_entries[i];
		if (!groups[ce->group])
			groups[ce->group] = debugfs_create_dir(
				b43_cal_group_name(ce->group), dir);
		dc->views[i] = (struct b43_cal_view){ .cs = &dc->cs, .entry = i };
		debugfs_create_file(ce->name, 0400, groups[ce->group],
				    &dc->views[i], &b43_cal_fops);
	}
}

/* After the files are gone */
static void b43_cal_remove(struct b43_wldev *dev, struct b43_dfsentry *e)
{
	if (!e->cal)
		return;
	dev->calstat = NULL;
	kvfree(b43_calstat_stop(&e->cal->cs));
	kfree(e->cal->views);
	kfree(e->cal);
}
#else
static inline void b43_cal_add(struct b43_wldev *dev, struct b43_dfsentry *e)
{
}
static inline void b43_cal_remove(struct b43_wldev *dev,
				  struct b43_dfsentry *e)
{
}
#endif

bool b43_debug(struct b43_wldev *dev, enum b43_dyndbg feature)
{
	bool enabled;

	enabled = (dev->dfsentry && dev->dfsentry->dyn_debug[feature]);
	if (unlikely(enabled)) {
		/* Force full debugging messages, if the user enabled
		 * some dynamic debugging feature. */
		b43_modparam_verbose = B43_VERBOSITY_MAX;
	}

	return enabled;
}

static void b43_add_dynamic_debug(struct b43_wldev *dev)
{
	struct b43_dfsentry *e = dev->dfsentry;

#define add_dyn_dbg(name, id, initstate) do {			\
	e->dyn_debug[id] = (initstate);				\
	debugfs_create_bool(name, 0600, e->subdir,		\
			    &(e->dyn_debug[id]));		\
	} while (0)

	add_dyn_dbg("debug_xmitpower", B43_DBG_XMITPOWER, false);
	add_dyn_dbg("debug_dmaoverflow", B43_DBG_DMAOVERFLOW, false);
	add_dyn_dbg("debug_dmaverbose", B43_DBG_DMAVERBOSE, false);
	add_dyn_dbg("debug_pwork_fast", B43_DBG_PWORK_FAST, false);
	add_dyn_dbg("debug_pwork_stop", B43_DBG_PWORK_STOP, false);
	add_dyn_dbg("debug_lo", B43_DBG_LO, false);
	add_dyn_dbg("debug_firmware", B43_DBG_FIRMWARE, false);
	add_dyn_dbg("debug_keys", B43_DBG_KEYS, false);
	add_dyn_dbg("debug_verbose_stats", B43_DBG_VERBOSESTATS, false);

#undef add_dyn_dbg
}

void b43_debugfs_add_device(struct b43_wldev *dev)
{
	struct b43_dfsentry *e;
	struct b43_txstatus_log *log;
	char devdir[16];

	B43_WARN_ON(!dev);
	e = kzalloc_obj(*e);
	if (!e) {
		b43err(dev->wl, "debugfs: add device OOM\n");
		return;
	}
	e->dev = dev;
	log = &e->txstatlog;
	log->log = kzalloc_objs(struct b43_txstatus, B43_NR_LOGGED_TXSTATUS);
	if (!log->log) {
		b43err(dev->wl, "debugfs: add device txstatus OOM\n");
		kfree(e);
		return;
	}
	log->end = -1;

	dev->dfsentry = e;

	snprintf(devdir, sizeof(devdir), "%s", wiphy_name(dev->wl->hw->wiphy));
	e->subdir = debugfs_create_dir(devdir, rootdir);

	e->mmio16read_next = 0xFFFF; /* invalid address */
	e->mmio32read_next = 0xFFFF; /* invalid address */
	e->shm16read_routing_next = 0xFFFFFFFF; /* invalid routing */
	e->shm16read_addr_next = 0xFFFFFFFF; /* invalid address */
	e->shm32read_routing_next = 0xFFFFFFFF; /* invalid routing */
	e->shm32read_addr_next = 0xFFFFFFFF; /* invalid address */

#define ADD_FILE(name, mode)	\
	do {							\
		debugfs_create_file_aux(__stringify(name),	\
				mode, e->subdir, dev,		\
				&fops_##name, &debugfs_ops);	\
	} while (0)


	ADD_FILE(shm16read, 0600);
	ADD_FILE(shm16write, 0200);
	ADD_FILE(shm32read, 0600);
	ADD_FILE(shm32write, 0200);
	ADD_FILE(mmio16read, 0600);
	ADD_FILE(mmio16write, 0200);
	ADD_FILE(mmio32read, 0600);
	ADD_FILE(mmio32write, 0200);
	ADD_FILE(txstat, 0400);
	ADD_FILE(restart, 0200);
	ADD_FILE(loctls, 0400);

#undef ADD_FILE

	b43_add_dynamic_debug(dev);
	b43_cal_add(dev, e);
}

void b43_debugfs_remove_device(struct b43_wldev *dev)
{
	struct b43_dfsentry *e;

	if (!dev)
		return;
	e = dev->dfsentry;
	if (!e)
		return;

	debugfs_remove(e->subdir);
	b43_cal_remove(dev, e);
	kfree(e->txstatlog.log);
	kfree(e);
}

void b43_debugfs_log_txstat(struct b43_wldev *dev,
			    const struct b43_txstatus *status)
{
	struct b43_dfsentry *e = dev->dfsentry;
	struct b43_txstatus_log *log;
	struct b43_txstatus *cur;
	int i;

	if (!e)
		return;
	log = &e->txstatlog;
	i = log->end + 1;
	if (i == B43_NR_LOGGED_TXSTATUS)
		i = 0;
	log->end = i;
	cur = &(log->log[i]);
	memcpy(cur, status, sizeof(*cur));
}

void b43_debugfs_init(void)
{
	rootdir = debugfs_create_dir(KBUILD_MODNAME, NULL);
}

void b43_debugfs_exit(void)
{
	debugfs_remove(rootdir);
}
