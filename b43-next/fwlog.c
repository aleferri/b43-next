// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#include "b43.h"
#include "main.h"
#include "fwlog.h"

static bool modparam_fwlog;
module_param_named(fwlog, modparam_fwlog, bool, 0444);
MODULE_PARM_DESC(fwlog, "Take the microcode's log into debugfs (default off)");

#define B43_FWLOG_POLL_MS	10
/* How long a microcode has to publish the mailbox once the core starts */
#define B43_FWLOG_WAIT_US	20000
/* Entries taken in one poll, as the microcode refills the mailbox */
#define B43_FWLOG_BURST		32
#define B43_FWLOG_RING		1024

struct b43_fwlog_entry {
	u64 time;		/* ns, when the host took it */
	u16 marker;
	u16 key;
	u16 value;
	u16 lost;		/* dropped by the microcode right before it */
};

struct b43_fwlog {
	struct b43_wldev *dev;
	struct delayed_work work;
	u16 base;		/* bytes, 0 if the microcode publishes none */
	u16 last;		/* MARKER of the last entry taken, 0 if none */
	unsigned int head;
	unsigned int used;
	struct b43_fwlog_entry ring[B43_FWLOG_RING];
};

static struct b43_fwlog *b43_fwlog_of(struct b43_wldev *dev)
{
	return dev->dfsentry ? dev->dfsentry->fwlog : NULL;
}

static u16 b43_fwlog_read(struct b43_fwlog *log, u16 cell)
{
	return b43_shm_read16(log->dev, B43_SHM_SHARED, log->base + cell);
}

static void b43_fwlog_take(struct b43_fwlog *log)
{
	struct b43_fwlog_entry *e;
	unsigned int n, next;
	u16 marker;

	for (n = 0; n < B43_FWLOG_BURST; n++) {
		marker = b43_fwlog_read(log, B43_FWLOG_MARKER);
		if (!marker || marker == log->last)
			break;
		next = log->last == 0xffff ? 1 : log->last + 1;
		e = &log->ring[log->head];
		e->time = ktime_get_ns();
		e->marker = marker;
		e->key = b43_fwlog_read(log, B43_FWLOG_KEY);
		e->value = b43_fwlog_read(log, B43_FWLOG_VALUE);
		e->lost = (marker + 0xffff - next) % 0xffff;
		b43_shm_write16(log->dev, B43_SHM_SHARED,
				log->base + B43_FWLOG_ACK, marker);
		log->last = marker;
		log->head = (log->head + 1) % B43_FWLOG_RING;
		if (log->used < B43_FWLOG_RING)
			log->used++;
	}
}

static void b43_fwlog_work(struct work_struct *work)
{
	struct b43_fwlog *log = container_of(work, struct b43_fwlog,
					     work.work);
	struct b43_wl *wl = log->dev->wl;

	mutex_lock(&wl->mutex);
	if (b43_status(log->dev) == B43_STAT_STARTED) {
		b43_fwlog_take(log);
		ieee80211_queue_delayed_work(wl->hw, &log->work,
				msecs_to_jiffies(B43_FWLOG_POLL_MS));
	}
	mutex_unlock(&wl->mutex);
}

void b43_fwlog_start(struct b43_wldev *dev)
{
	struct b43_fwlog *log = b43_fwlog_of(dev);
	u16 addr;

	if (!log)
		return;
	read_poll_timeout(b43_shm_read16, addr, addr, 1000, B43_FWLOG_WAIT_US,
			  false, dev, B43_SHM_SHARED, B43_SHM_SH_FWLOG);
	if (!addr || addr > (0xffff - B43_FWLOG_ACK) / 2) {
		b43warn(dev->wl, "fwlog: no mailbox (0x%04x)\n", addr);
		log->base = 0;
		return;
	}
	log->base = addr * 2;
	log->last = 0;
	ieee80211_queue_delayed_work(dev->wl->hw, &log->work, 0);
}

/* Without wl->mutex */
void b43_fwlog_cancel(struct b43_wldev *dev)
{
	struct b43_fwlog *log = b43_fwlog_of(dev);

	if (log)
		cancel_delayed_work_sync(&log->work);
}

/* One entry per line, the oldest first: time, MARKER, KEY, VALUE, lost */
static int b43_fwlog_show(struct seq_file *s, void *unused)
{
	struct b43_fwlog *log = s->private;
	const struct b43_fwlog_entry *e;
	unsigned int i;

	mutex_lock(&log->dev->wl->mutex);
	for (i = 0; i < log->used; i++) {
		e = &log->ring[(log->head + B43_FWLOG_RING - log->used + i) %
			       B43_FWLOG_RING];
		seq_printf(s, "%llu %u 0x%04x 0x%04x %u\n", e->time,
			   e->marker, e->key, e->value, e->lost);
	}
	mutex_unlock(&log->dev->wl->mutex);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(b43_fwlog);

void b43_fwlog_add(struct b43_wldev *dev, struct b43_dfsentry *e)
{
	struct b43_fwlog *log;

	if (!modparam_fwlog)
		return;
	log = kzalloc_obj(*log);
	if (!log)
		return;
	log->dev = dev;
	INIT_DELAYED_WORK(&log->work, b43_fwlog_work);
	e->fwlog = log;
	debugfs_create_file("fwlog", 0400, e->subdir, log, &b43_fwlog_fops);
}

/* After the files are gone, with the core stopped */
void b43_fwlog_remove(struct b43_dfsentry *e)
{
	kfree(e->fwlog);
}
