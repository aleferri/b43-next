// SPDX-License-Identifier: GPL-2.0
/*
 * Broadcom B43 wireless driver
 *
 * The calibration statistics: the rings of the PHY's entries in one block of
 * storage, sized from the PHY's table. This file allocates nothing and does
 * not know debugfs, so that the harnesses link it as it is.
 */

#include "b43.h"
#include "calstat.h"

#define B43_CAL_GROUP_NAME(id, name)	[B43_CAL_##id] = name,
static const char *const b43_cal_group_names[] = {
	B43_CAL_GROUPS(B43_CAL_GROUP_NAME)
};
#undef B43_CAL_GROUP_NAME

const char *b43_cal_group_name(enum b43_cal_group group)
{
	return b43_cal_group_names[group];
}

static const char *const b43_cal_stage_names[] = {
	[B43_CAL_SAMPLED]	= "sampled",
	[B43_CAL_EVALUATED]	= "evaluated",
	[B43_CAL_COMPUTED]	= "computed",
	[B43_CAL_READBACK]	= "readback",
};

/* A slot of a ring: n is how many of the entry's values were reported */
struct b43_cal_slot {
	u32 seq;
	u32 run;
	u16 channel;
	u16 n;
	u32 v[];
};

static unsigned int b43_cal_nr_values(const struct b43_cal_entry *e)
{
	unsigned int i, n = 0;

	for (i = 0; i < e->nr_fields; i++)
		n += e->fields[i].count;
	return n;
}

void b43_calstat_setup(struct b43_calstat *cs, struct b43_wldev *dev,
		       const struct b43_cal_entry *entries,
		       unsigned int nr_entries, struct b43_cal_ring *rings)
{
	unsigned int i;

	memset(cs, 0, sizeof(*cs));
	cs->dev = dev;
	cs->entries = entries;
	cs->nr_entries = nr_entries;
	cs->rings = rings;
	for (i = 0; i < nr_entries; i++) {
		rings[i].off = cs->size;
		rings[i].nr_values = b43_cal_nr_values(&entries[i]);
		rings[i].slot = sizeof(struct b43_cal_slot) +
				rings[i].nr_values * sizeof(u32);
		rings[i].head = 0;
		rings[i].used = 0;
		cs->size += rings[i].slot * entries[i].depth;
	}
}

/* @mem: cs->size bytes, zeroed */
void b43_calstat_start(struct b43_calstat *cs, u8 *mem)
{
	unsigned int i;

	for (i = 0; i < cs->nr_entries; i++) {
		cs->rings[i].head = 0;
		cs->rings[i].used = 0;
	}
	memset(cs->run, 0, sizeof(cs->run));
	cs->seq = 0;
	cs->mem = mem;
}

/* Returns the storage for the caller to free */
u8 *b43_calstat_stop(struct b43_calstat *cs)
{
	u8 *mem = cs->mem;

	cs->mem = NULL;
	return mem;
}

bool b43_cal_on(struct b43_wldev *dev)
{
	return dev->calstat && dev->calstat->mem;
}

void b43_cal_begin(struct b43_wldev *dev, enum b43_cal_group group)
{
	if (b43_cal_on(dev))
		dev->calstat->run[group]++;
}

/* The next slot of the entry's ring, for @n values: NULL when off */
static u32 *b43_cal_slot_new(struct b43_wldev *dev, unsigned int entry,
			     unsigned int n)
{
	struct b43_calstat *cs = dev->calstat;
	const struct b43_cal_entry *e;
	struct b43_cal_ring *r;
	struct b43_cal_slot *s;

	if (!b43_cal_on(dev) || WARN_ON_ONCE(entry >= cs->nr_entries))
		return NULL;
	lockdep_assert_held(&dev->wl->mutex);

	e = &cs->entries[entry];
	r = &cs->rings[entry];
	if (WARN_ON_ONCE(n > r->nr_values))
		return NULL;

	s = (void *)(cs->mem + r->off + r->head * r->slot);
	s->seq = ++cs->seq;
	s->run = cs->run[e->group];
	s->channel = dev->phy.channel;
	s->n = n;

	r->head = (r->head + 1) % e->depth;
	if (r->used < e->depth)
		r->used++;
	return s->v;
}

void b43_cal_report_values(struct b43_wldev *dev, unsigned int entry,
			   const u32 *v, unsigned int n)
{
	u32 *dst = b43_cal_slot_new(dev, entry, n);

	if (dst)
		memcpy(dst, v, n * sizeof(u32));
}

/* @nhead values, then the @nvec values of a vector field */
void b43_cal_report_vec16(struct b43_wldev *dev, unsigned int entry,
			  const u32 *head, unsigned int nhead,
			  const u16 *vec, unsigned int nvec)
{
	u32 *dst = b43_cal_slot_new(dev, entry, nhead + nvec);
	unsigned int i;

	if (!dst)
		return;
	memcpy(dst, head, nhead * sizeof(u32));
	for (i = 0; i < nvec; i++)
		dst[nhead + i] = vec[i];
}

/* The slot @age reports before the last one, NULL when there is none */
static const struct b43_cal_slot *
b43_cal_slot(const struct b43_calstat *cs, unsigned int entry,
	     unsigned int age)
{
	const struct b43_cal_entry *e = &cs->entries[entry];
	const struct b43_cal_ring *r = &cs->rings[entry];
	unsigned int i;

	if (!cs->mem || age >= r->used)
		return NULL;
	i = (r->head + e->depth - 1 - age) % e->depth;
	return (void *)(cs->mem + r->off + i * r->slot);
}

static int b43_cal_format_value(const struct b43_cal_field *f, u32 v,
				char *buf, size_t size)
{
	switch (f->fmt) {
	case B43_CAL_FMT_S:
		return scnprintf(buf, size, "%d", (s32)v);
	case B43_CAL_FMT_X2:
		return scnprintf(buf, size, "%02x", v);
	case B43_CAL_FMT_X4:
		return scnprintf(buf, size, "%04x", v);
	case B43_CAL_FMT_X8:
		return scnprintf(buf, size, "%08x", v);
	case B43_CAL_FMT_ENUM:
		if (v < f->nr_names)
			return scnprintf(buf, size, "%s", f->names[v]);
		fallthrough;
	default:
		return scnprintf(buf, size, "%u", v);
	}
}

/* A slot as a line of key=value pairs: its length, or -ENOENT */
int b43_cal_format(struct b43_calstat *cs, unsigned int entry,
		   unsigned int age, unsigned int flags, char *buf,
		   size_t size)
{
	const struct b43_cal_entry *e;
	const struct b43_cal_slot *s;
	unsigned int i, k, v = 0;
	int len = 0;

	if (entry >= cs->nr_entries)
		return -EINVAL;
	s = b43_cal_slot(cs, entry, age);
	if (!s)
		return -ENOENT;
	e = &cs->entries[entry];

	if (flags & B43_CAL_FMT_CONTEXT)
		len += scnprintf(buf + len, size - len, "seq=%u ch=%u run=%u ",
				 s->seq, s->channel, s->run);
	len += scnprintf(buf + len, size - len, "cal=%s entry=%s stage=%s",
			 b43_cal_group_names[e->group], e->name,
			 b43_cal_stage_names[e->stage]);
	for (i = 0; i < e->nr_fields && v < s->n; i++) {
		const struct b43_cal_field *f = &e->fields[i];

		len += scnprintf(buf + len, size - len, " %s=", f->name);
		for (k = 0; k < f->count && v < s->n; k++, v++) {
			if (k)
				len += scnprintf(buf + len, size - len, ",");
			len += b43_cal_format_value(f, s->v[v], buf + len,
						    size - len);
		}
	}
	return len;
}

/*
 * The slot made after sequence number *@seq, in every ring: false when
 * there is none. Start with *@seq at 0.
 */
bool b43_cal_next(struct b43_calstat *cs, u32 *seq, unsigned int *entry,
		  unsigned int *age)
{
	const struct b43_cal_slot *s;
	unsigned int i, a;
	u32 best = 0;

	for (i = 0; i < cs->nr_entries; i++) {
		for (a = 0; (s = b43_cal_slot(cs, i, a)); a++) {
			if (s->seq <= *seq)
				break;
			if (best && s->seq >= best)
				continue;
			best = s->seq;
			*entry = i;
			*age = a;
		}
	}
	*seq = best;
	return best;
}
