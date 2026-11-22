/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_CALSTAT_H_
#define B43_CALSTAT_H_

#include <linux/kernel.h>

struct b43_wldev;

/*
 * The calibration statistics: what each calibration of the PHY sampled,
 * evaluated and computed. A PHY declares its entries in a table of
 * struct b43_cal_entry, indexed by its own enum: each entry is a tuple of
 * numeric fields, kept as its last value or as a ring of its last values.
 */

/* The calibrations, which group the entries and count the runs */
#define B43_CAL_GROUPS(C)			\
	C(RCAL,		"rcal")			\
	C(RCCAL,	"rccal")		\
	C(VCOCAL,	"vcocal")		\
	C(RSSI,		"rssi")			\
	C(IDLE_TSSI,	"idle_tssi")		\
	C(TXGAIN,	"txgain")		\
	C(TXIQLO,	"txiqlo")		\
	C(RXIQ,		"rxiq")			\
	C(LPF,		"lpf")			\
	C(PAPD,		"papd")			\
	C(AFE,		"afe")			\
	C(RPCAL,	"rpcal")		\
	C(TEMP,		"temp")			\
	C(LO,		"lo")			\
	C(NRSSI,	"nrssi")		\
	C(TXPWR,	"txpwr")

#define B43_CAL_GROUP_ENUM(id, name)	B43_CAL_##id,
enum b43_cal_group {
	B43_CAL_GROUPS(B43_CAL_GROUP_ENUM)
	__B43_NR_CAL_GROUPS,
};
#undef B43_CAL_GROUP_ENUM

enum b43_cal_stage {
	/* Measured by the calibration: estimates, TSSI, RSSI, done flags */
	B43_CAL_SAMPLED,
	/* Derived from the samples to decide: sums, ratios, picks, failures */
	B43_CAL_EVALUATED,
	/* The results the calibration writes or leaves in the hardware */
	B43_CAL_COMPUTED,
	/* Read only for the statistics: hardware, board or driver state */
	B43_CAL_READBACK,
	__B43_NR_CAL_STAGES,
};

enum b43_cal_fmt {
	B43_CAL_FMT_U,
	B43_CAL_FMT_S,
	B43_CAL_FMT_X2,
	B43_CAL_FMT_X4,
	B43_CAL_FMT_X8,
	B43_CAL_FMT_ENUM,
};

struct b43_cal_field {
	const char *name;
	/* The value names of a B43_CAL_FMT_ENUM field */
	const char *const *names;
	u8 nr_names;
	u8 fmt;
	/* Values in the field: more than one for a vector */
	u8 count;
};

struct b43_cal_entry {
	const char *name;
	const struct b43_cal_field *fields;
	u8 nr_fields;
	u8 group;
	u8 stage;
	/* 1 keeps the last value, more a ring of the last ones */
	u8 depth;
};

#define B43_CAL_U(n)		{ .name = #n, .fmt = B43_CAL_FMT_U, .count = 1 }
#define B43_CAL_S(n)		{ .name = #n, .fmt = B43_CAL_FMT_S, .count = 1 }
#define B43_CAL_X(n, w)		{ .name = #n, .fmt = B43_CAL_FMT_X##w, .count = 1 }
#define B43_CAL_UV(n, c)	{ .name = #n, .fmt = B43_CAL_FMT_U, .count = c }
#define B43_CAL_SV(n, c)	{ .name = #n, .fmt = B43_CAL_FMT_S, .count = c }
#define B43_CAL_XV(n, w, c)	{ .name = #n, .fmt = B43_CAL_FMT_X##w, .count = c }
#define B43_CAL_E(n, nm)						\
	{ .name = #n, .fmt = B43_CAL_FMT_ENUM, .count = 1,		\
	  .names = nm, .nr_names = ARRAY_SIZE(nm) }

#define B43_CAL_ENTRY(id, grp, ename, stg, dep, ...)			\
	[id] = {							\
		.name = ename,						\
		.group = B43_CAL_##grp,					\
		.stage = B43_CAL_##stg,					\
		.depth = dep,						\
		.fields = (const struct b43_cal_field[]){ __VA_ARGS__ },	\
		.nr_fields = sizeof((const struct b43_cal_field[]){	\
					__VA_ARGS__ }) /		\
			     sizeof(struct b43_cal_field),		\
	}

/* A PHY's table, and its place in the PHY's operations */
#define B43_CAL_TABLE(name)	\
	static const struct b43_cal_entry name[] __maybe_unused

/* A ring of a calibration entry, in the storage of struct b43_calstat */
struct b43_cal_ring {
	u32 off;
	u16 slot;
	u16 nr_values;
	u16 head;
	u16 used;
};

struct b43_calstat {
	struct b43_wldev *dev;
	const struct b43_cal_entry *entries;
	unsigned int nr_entries;
	struct b43_cal_ring *rings;
	/* The bytes of storage the entries take */
	size_t size;
	/* The storage, NULL while the statistics are off */
	u8 *mem;
	u32 seq;
	u32 run[__B43_NR_CAL_GROUPS];
};

/* b43_cal_format(): the slot's sequence number, run and channel first */
#define B43_CAL_FMT_CONTEXT	BIT(0)

#ifdef CONFIG_B43_NEXT_CALSTAT
#define B43_CAL_OPS(table)						\
	.cal_entries = table, .nr_cal_entries = ARRAY_SIZE(table),

void b43_calstat_setup(struct b43_calstat *cs, struct b43_wldev *dev,
		       const struct b43_cal_entry *entries,
		       unsigned int nr_entries, struct b43_cal_ring *rings);
void b43_calstat_start(struct b43_calstat *cs, u8 *mem);
u8 *b43_calstat_stop(struct b43_calstat *cs);

const char *b43_cal_group_name(enum b43_cal_group group);
bool b43_cal_on(struct b43_wldev *dev);
void b43_cal_begin(struct b43_wldev *dev, enum b43_cal_group group);
void b43_cal_report_values(struct b43_wldev *dev, unsigned int entry,
			   const u32 *v, unsigned int n);
void b43_cal_report_vec16(struct b43_wldev *dev, unsigned int entry,
			  const u32 *head, unsigned int nhead,
			  const u16 *vec, unsigned int nvec);
int b43_cal_format(struct b43_calstat *cs, unsigned int entry,
		   unsigned int age, unsigned int flags, char *buf,
		   size_t size);
bool b43_cal_next(struct b43_calstat *cs, u32 *seq, unsigned int *entry,
		  unsigned int *age);
#else
#define B43_CAL_OPS(table)

static inline bool b43_cal_on(struct b43_wldev *dev)
{
	return false;
}
static inline void b43_cal_begin(struct b43_wldev *dev,
				 enum b43_cal_group group)
{
}
static inline void b43_cal_report_values(struct b43_wldev *dev,
					 unsigned int entry, const u32 *v,
					 unsigned int n)
{
}
static inline void b43_cal_report_vec16(struct b43_wldev *dev,
					unsigned int entry, const u32 *head,
					unsigned int nhead, const u16 *vec,
					unsigned int nvec)
{
}
#endif

/*
 * Report a tuple of entry @entry, its fields' values in order. The values
 * are not evaluated while the statistics are off; hardware reads made only
 * for the statistics go between B43_CAL_PEEK(dev, 1) and B43_CAL_PEEK(dev, 0).
 */
#define b43_cal_report(dev, entry, ...)					\
	do {								\
		if (b43_cal_on(dev)) {					\
			const u32 __b43_cal_v[] = { __VA_ARGS__ };	\
									\
			b43_cal_report_values(dev, entry, __b43_cal_v,	\
					      ARRAY_SIZE(__b43_cal_v));	\
		}							\
	} while (0)

/* A harness replaying traces defines it so that the reads are not served */
#ifndef B43_CAL_PEEK
#define B43_CAL_PEEK(dev, on)	do { } while (0)
#endif

#endif /* B43_CALSTAT_H_ */
