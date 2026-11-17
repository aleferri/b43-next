// SPDX-License-Identifier: GPL-2.0
/*
 * The emission of the ops, in ../unit/wrap.c's format.
 *
 * Compiled as USER code, not kernel: stdio and getenv are needed here, and
 * this file is not driver code. The rest of the suite is compiled with
 * -nostdinc against the kernel headers, so the two halves share no system
 * headers -- only trace_out.h, which declares the types it needs.
 *
 * The reads return what the oracle says. Without an oracle they return zero,
 * and zero on a self-clearing status bit is the value that spins a poll
 * until the timeout: the same problem ../unit solves with AC_READ_ORACLE and
 * the read plans. The oracle hooks in a single function, oracle_lookup().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static FILE *out;
static int cpu = 1;

static FILE *stream(void)
{
	if (!out) {
		const char *p = getenv("B43_TRACE_OUT");

		out = p ? fopen(p, "w") : stdout;
		if (!out)
			out = stdout;
		/*
		 * One line per op, right away: if the probe dies halfway through
		 * the attach, what it emitted before is the only thing to compare,
		 * and with stdio's buffer full everything is lost.
		 */
		setvbuf(out, NULL, _IOLBF, 0);
	}
	return out;
}

/*
 * The reads' oracle.
 *
 * Needed for the reason ../unit learned: without it every read returns zero,
 * and zero on a self-clearing status bit ends at once a poll that should
 * spin, while on a bit the hardware raises by itself it spins it until the
 * timeout. A trace with invented reads looks good and is not a measurement.
 *
 * Format and semantics are ../unit's: a vendor capture with the RETVALs
 * folded (`trace_filter.py --retvals`), served IN ORDER per (class,
 * address). A queue per key, not a value per key: the vendor reads the same
 * register many times with different values, and the succession is what
 * counts -- flattening it on a fixed value is exactly what ends the polls at
 * the wrong time.
 *
 * Once the queue is exhausted the last value seen is returned, not zero: the
 * least wrong behaviour when the port reads more than the vendor, and it is
 * said once on stderr.
 *
 * The queue is the bus's, not the tracer's: a vendor PHY.MOD is a read
 * followed by a write, and the read has no recorded value. Without a slot in
 * the queue for that read, the driver's maskset -- which really reads on the
 * bus -- would consume the value of the next read and from then on the
 * address would be off by one (on the reference segment the switch's first
 * PHY.RD 0x19e would get 0x3d2 instead of 0x0). ORACLE_SHADOW holds the
 * slot, returning the register's last known value, read or written: what the
 * hardware would have answered, bits that move by themselves aside. The
 * RAD.MODs do not need it: the vendor tracer also records the inner RAD.RD
 * and RAD.WR, with the values.
 */
#define ORACLE_KEYS	4096
#define ORACLE_MAX	65536
#define ORACLE_SHADOW	0xffffffffu

struct oracle_key {
	char cls[32];
	u16 addr;
	u32 *vals;
	unsigned n, cap, pos;
	u32 last;
	int written;
};

/*
 * Registers that read back the last value written: after the driver's first
 * write the shadow is returned, the queue before it. From the queue, one read
 * more or less on the port's side would shift all the following ones.
 *
 *   0x019e  B43_PHY_AC_REG_TBL_WRITE_GATE
 */
static const struct { const char *cls; u16 addr; } oracle_latch[] = {
	{ "PHY.RD", 0x019e },
};

/*
 * The reads from the PHY's table data ports have a queue per table address:
 * the reads of an access consume in order the queue of its start address,
 * and a table the port reads and the vendor does not leaves the others'
 * queues alone. On the AC the ports are 0x000f-0x0011, with the id from
 * 0x000d and the offset from 0x000e; on the HT 0x0073 and 0x0074, with id
 * and offset together in 0x0072 (id << 10 | offset).
 */
static u16 vendor_tbl_id, vendor_tbl_off, port_tbl_id, port_tbl_off;
static int tbl_ht;

void b43_trace_ht_tables(int on)
{
	tbl_ht = on;
}

static int tbl_port(const char *cls, u16 addr)
{
	if (strcmp(cls, "PHY.RD"))
		return 0;
	if (tbl_ht)
		return addr == 0x0073 || addr == 0x0074;
	return addr >= 0x000f && addr <= 0x0011;
}

static void tbl_key(char *buf, size_t len, u16 id, u16 off)
{
	snprintf(buf, len, "TBL.RD@%04x.%04x", id, off);
}

static void tbl_note_addr(u16 *id, u16 *off, u16 addr, u32 val)
{
	if (tbl_ht) {
		if (addr == 0x0072) {
			*id = (u16)val >> 10;
			*off = (u16)val & 0x3ff;
		}
		return;
	}
	if (addr == 0x000d)
		*id = (u16)val;
	else if (addr == 0x000e)
		*off = (u16)val;
}

static int oracle_is_latch(const char *cls, u16 addr)
{
	unsigned i;

	for (i = 0; i < sizeof(oracle_latch) / sizeof(oracle_latch[0]); i++)
		if (oracle_latch[i].addr == addr &&
		    !strcmp(oracle_latch[i].cls, cls))
			return 1;
	return 0;
}

/*
 * The address the vendor gave its RX ring 0 (the first write of DMA64 RX
 * ring 0's low address, 0x0228, in a capture taken at the bus), for the
 * slots its status reads carry.
 */
static unsigned vendor_rx_ringlo;
static int vendor_rx_ringlo_seen;

static struct oracle_key okeys[ORACLE_KEYS];
static unsigned n_okeys;
static int oracle_loaded;
static int oracle_absent_warned;

static struct oracle_key *okey(const char *cls, u16 addr, int create)
{
	unsigned i;

	for (i = 0; i < n_okeys; i++)
		if (okeys[i].addr == addr && !strcmp(okeys[i].cls, cls))
			return &okeys[i];
	if (!create || n_okeys == ORACLE_KEYS)
		return NULL;
	snprintf(okeys[n_okeys].cls, sizeof(okeys[n_okeys].cls), "%s", cls);
	okeys[n_okeys].addr = addr;
	return &okeys[n_okeys++];
}

static void oracle_push(const char *cls, u16 addr, u32 val)
{
	struct oracle_key *k = okey(cls, addr, 1);

	if (!k || k->n == ORACLE_MAX)
		return;
	if (k->n == k->cap) {
		k->cap = k->cap ? k->cap * 2 : 8;
		k->vals = realloc(k->vals, k->cap * sizeof(*k->vals));
	}
	k->vals[k->n++] = val;
}

/*
 * A cold capture of the sweep carries TWO attaches at its head: `wl` at load
 * time attaches all the cores, so before wl1 -- the AC -- there is wl0's, the
 * 2.4 GHz N-PHY. They are 46 ops, and reverse-tools/strip_other_core.py
 * removes them.
 *
 * Here the oracle loads the whole file and has no window, so those ops end
 * up at the head of the per-address queues and the driver under test reads
 * the other core's state. Not a hypothetical case: `UCODEREV` (0x0000) and
 * `UCODEPATCH` (0x0002) are read by b43_validate_chipaccess() on both cores,
 * and without the cut the port gets wl0's self-test patterns, 0x55aa and
 * 0xaa55, instead of the real values.
 *
 * The witness is the boundary that defines the cut: between the first and
 * the second OTP.RDR/OTP.INIT pair. If there are two before the AC attach's
 * first PHY op -- the read of 0x0739 -- the capture is not cut.
 *
 * It refuses instead of warning: four wrong values among twenty thousand ops
 * go unnoticed, and the rest of the run looks good.
 */
static int oracle_has_other_core(FILE *f)
{
	char line[512];
	int otp = 0;

	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "PHY.RD") && strstr(line, "addr=0x0739"))
			break;
		if (strstr(line, "OTP.RDR"))
			otp++;
	}
	rewind(f);
	return otp >= 2;
}

/*
 * The shared memory has several spaces -- SHARED, SCRATCH, UCODE, ... -- with
 * the same addresses, and the capture tells them apart with the `sel=` field
 * (the routing of B43_MMIO_SHM_CONTROL in the high 16 bits). The queue is per
 * space: a single queue per address would give the UCODEREV read the value
 * of another routing at the same offset.
 */
static void shm_key(char *buf, size_t len, u32 routing)
{
	snprintf(buf, len, "OBJ.RD/%x", routing);
}

/*
 * The capture's lines: `<ts> #<ep> cpuN <CLASS> addr=0x.. val=0x..`, and
 * `off=` instead of `addr=` for the MAC's registers (REG.RD), which only a
 * capture taken at the bus carries (reverse-tools/mmio2ops.py). The reads are
 * taken, because the oracle answers those; of the vendor's writes only the
 * table address (0x000d, 0x000e), which gives the data port reads their key.
 * The others are the comparison's reference, not an input.
 */
static void oracle_load(void)
{
	const char *path = getenv("B43_READ_ORACLE");
	char line[512], cls[32];
	unsigned addr, val;
	FILE *f;

	oracle_loaded = 1;
	if (!path)
		return;
	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "b43-integration: B43_READ_ORACLE=%s "
			"cannot be opened\n", path);
		return;
	}
	if (oracle_has_other_core(f)) {
		fprintf(stderr,
			"b43-integration: %s still carries wl0's attach at its "
			"head: two OTP.RDR/OTP.INIT pairs before the first "
			"PHY.RD 0x0739.\n"
			"b43-integration:   The per-address queues would feed "
			"the driver the other core's state -- 0x0000 and 0x0002 "
			"with wl0's self-test.\n"
			"b43-integration:   Cut it first:\n"
			"b43-integration:     python3 reverse-tools/"
			"strip_other_core.py <segment> /tmp/seg\n"
			"b43-integration:     python3 reverse-tools/"
			"trace_filter.py --retvals /tmp/seg /tmp/oracle\n",
			path);
		fclose(f);
		abort();
	}
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%*s #%*u cpu%*u %31s addr=0x%x val=0x%x",
			   cls, &addr, &val) != 3 &&
		    sscanf(line, "%*s #%*u cpu%*u %31s off=0x%x val=0x%x",
			   cls, &addr, &val) != 3 &&
		    sscanf(line, "%*s #%*u cpu%*u %31s word=0x%x val=0x%x",
			   cls, &addr, &val) != 3)
			continue;
		if (!strcmp(cls, "OBJ.RD")) {
			const char *sel = strstr(line, " sel=0x");
			unsigned routing = 0;

			if (sel)
				sscanf(sel, " sel=0x%x", &routing);
			shm_key(cls, sizeof(cls), routing);
			oracle_push(cls, (u16)addr, val);
		} else if (tbl_port(cls, (u16)addr)) {
			char key[32];

			tbl_key(key, sizeof(key), vendor_tbl_id, vendor_tbl_off);
			oracle_push(key, (u16)addr, val);
		} else if (!strcmp(cls, "PHY.WR"))
			tbl_note_addr(&vendor_tbl_id, &vendor_tbl_off,
				      (u16)addr, val);
		else if (!strcmp(cls, "REG.WR") && addr == 0x0228 &&
			 !vendor_rx_ringlo_seen) {
			vendor_rx_ringlo = val;
			vendor_rx_ringlo_seen = 1;
		}
		else if (strstr(cls, ".RD"))
			oracle_push(cls, (u16)addr, val);
		else if (!strcmp(cls, "PHY.MOD") || !strcmp(cls, "PHY.AND") ||
			 !strcmp(cls, "PHY.OR"))
			oracle_push("PHY.RD", (u16)addr, ORACLE_SHADOW);
	}
	fclose(f);
	fprintf(stderr, "b43-integration: oracle from %s, %u keys\n",
		path, n_okeys);
}

static u32 oracle_lookup(const char *cls, u16 addr, int width)
{
	struct oracle_key *k;

	(void)width;
	if (!oracle_loaded)
		oracle_load();
	if (!n_okeys) {
		if (!oracle_absent_warned) {
			oracle_absent_warned = 1;
			fprintf(stderr,
				"b43-integration: no oracle "
				"(B43_READ_ORACLE not set), every read "
				"returns 0. The trace is NOT a measurement of the "
				"driver.\n");
		}
		return 0;
	}
	if (tbl_port(cls, addr)) {
		char key[32];

		tbl_key(key, sizeof(key), port_tbl_id, port_tbl_off);
		k = okey(key, addr, 0);
	} else
		k = okey(cls, addr, 0);
	if (!k || !k->n)
		return 0;
	if (k->written && oracle_is_latch(cls, addr))
		return k->last;
	while (k->pos < k->n) {
		u32 v = k->vals[k->pos++];

		if (v == ORACLE_SHADOW)
			return k->last;
		k->last = v;
		return v;
	}
	return k->last;	/* queue exhausted: the last one seen */
}

/* A driver write updates the shadow the placeholders return. */
static void oracle_note_write(const char *wr_cls, u16 addr, u32 val)
{
	char cls[32];
	struct oracle_key *k;
	size_t n = strlen(wr_cls);

	if (n < 3 || strcmp(wr_cls + n - 3, ".WR"))
		return;
	if (!strcmp(wr_cls, "PHY.WR"))
		tbl_note_addr(&port_tbl_id, &port_tbl_off, addr, val);
	snprintf(cls, sizeof(cls), "%.*s.RD", (int)(n - 3), wr_cls);
	k = okey(cls, addr, 0);
	if (k) {
		k->last = val;
		k->written = 1;
	}
}

/*
 * Boundary markers (B43_AC_FN and B43_AC_BLOCK in phy_ac.h), the same ones
 * ../unit emits and fn_map.py reads. They attribute this trace's ops to the
 * driver's functions: without them the comparison between the sequence of
 * the whole of b43 and the harness's can be aligned but not explained.
 *
 * Silent unless B43_FN_MARKERS is in the environment, because the default
 * trace is a comparison reference and the markers are not ops.
 */
static int fn_markers_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = getenv("B43_FN_MARKERS") ? 1 : 0;
	return enabled;
}

void b43_ac_fn_enter(const char *fn)
{
	if (fn_markers_enabled())
		fprintf(stream(), "----FN:%s----\n", fn);
}

void b43_ac_fn_leave(const char *fn)
{
	if (fn_markers_enabled())
		fprintf(stream(), "----/FN:%s----\n", fn);
}

void b43_ac_block_mark(const char *name)
{
	if (fn_markers_enabled())
		fprintf(stream(), "----BLK:%s----\n", name);
}

static FILE *timeline;

static FILE *timeline_file(void)
{
	const char *path = getenv("B43_TIMELINE");

	if (!timeline && path && *path) {
		timeline = fopen(path, "r");
		if (!timeline)
			fprintf(stderr, "b43-integration: B43_TIMELINE=%s cannot "
				"be opened\n", path);
	}
	return timeline;
}

int b43_test_timeline_wd(long long *first_us, long long *last_us, int *n)
{
	FILE *f = timeline_file();
	char line[128], kind[16];
	double t;

	*n = 0;
	if (!f)
		return 0;
	rewind(f);
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%lf %*d %15s", &t, kind) != 2 ||
		    strcmp(kind, "WD"))
			continue;
		if (!(*n)++)
			*first_us = (long long)(t * 1e6 + 0.5);
		*last_us = (long long)(t * 1e6 + 0.5);
	}
	rewind(f);
	return *n;
}

int b43_test_timeline_next(long long *t_us, char *kind, int len,
			   u32 *arg0, u32 *arg1)
{
	FILE *f = timeline_file();
	char line[128], k[16];
	unsigned int a0 = 0, a1 = 0;
	double t;

	while (f && fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%lf %*d %15s %x %x", &t, k, &a0, &a1) < 2)
			continue;
		*t_us = (long long)(t * 1e6 + 0.5);
		snprintf(kind, len, "%s", k);
		*arg0 = a0;
		*arg1 = a1;
		return 1;
	}
	return 0;
}

/* For main.c's messages, which is compiled without stdio. */
void b43_trace_note(const char *fmt, int arg)
{
	fprintf(stderr, "b43-integration: ");
	fprintf(stderr, fmt, arg);
}

void b43_trace_op(const char *cls, u16 addr, u32 val, u16 mask, int has_mask)
{
	oracle_note_write(cls, addr, val);
	if (has_mask >= 0)
		fprintf(stream(), "cpu%d %-8s addr=0x%04x val=0x%04x mask=0x%04x\n",
			cpu, cls, addr, val, mask);
	else
		fprintf(stream(), "cpu%d %-8s addr=0x%04x val=0x%04x\n",
			cpu, cls, addr, val);
}

/*
 * MACCONTROL in the vendor's form, which traces it as a 32-bit maskset
 * without an address. On the bus it is a whole write: there is no mask, and
 * compare.py's mask comparison binds the bits the vendor touched.
 */
void b43_trace_macctl(u32 val)
{
	fprintf(stream(), "cpu%d MAC.MCTRL val=0x%08x\n", cpu, val);
}

/* 32-bit maskset with an address: the PMU's registers. */
void b43_trace_op32(const char *cls, u16 addr, u32 val, u32 mask)
{
	fprintf(stream(), "cpu%d %s addr=0x%x val=0x%08x mask=0x%08x\n",
		cpu, cls, addr, val, mask);
}

/* The chipcommon GPIOs: register implicit in the class, as in the vendor. */
void b43_trace_gpio(const char *cls, u32 val, u32 mask)
{
	fprintf(stream(), "cpu%d %s val=0x%08x mask=0x%08x\n", cpu, cls, val, mask);
}

/*
 * The shared memory: b43 writes (routing << 16) | offset into the control
 * register, so offset and routing are split here. The routing is the field
 * the d6220's captures do not have and the DSL's call `sel`.
 */
void b43_trace_shm(const char *cls, u32 routing_off, u32 val, int width)
{
	u16 routing = (u16)(routing_off >> 16);
	u16 off = (u16)(routing_off & 0xffff);

	fprintf(stream(), "cpu%d %-8s addr=0x%04x val=0x%0*x sel=0x%x\n",
		cpu, cls, off, width == 32 ? 8 : 4, val, routing << 16);
}

void b43_trace_raw(const char *cls, u16 off, u32 val, int width)
{
	fprintf(stream(), "cpu%d %-8s off=0x%04x val=0x%0*x\n",
		cpu, cls, off, width == 32 ? 8 : 4, val);
}

void b43_trace_srom(u16 word, u16 val)
{
	fprintf(stream(), "cpu%d %-8s word=0x%03x val=0x%04x\n",
		cpu, "SROM.RD", word, val);
}

void b43_trace_block(const char *cls, u16 off, unsigned long count,
		     u8 reg_width)
{
	fprintf(stream(), "cpu%d %-8s addr=0x%04x len=%lu width=%u\n",
		cpu, cls, off, count, reg_width);
}

void b43_trace_fill(void *buf, unsigned long count, u16 off, u8 reg_width)
{
	(void)off;
	(void)reg_width;
	memset(buf, 0, count);
}

u32 b43_trace_read(const char *cls, u16 addr, int width)
{
	return oracle_lookup(cls, addr, width);
}

int b43_trace_vendor_rx_ringlo(u32 *v)
{
	if (!oracle_loaded)
		oracle_load();
	*v = vendor_rx_ringlo;
	return vendor_rx_ringlo_seen;
}

int b43_trace_has(const char *cls, u16 addr)
{
	if (!oracle_loaded)
		oracle_load();
	return okey(cls, addr, 0) != NULL;
}

u32 b43_trace_read_shm(u32 routing, u16 addr, int width)
{
	char cls[32];

	shm_key(cls, sizeof(cls), routing);
	return oracle_lookup(cls, addr, width);
}

void b43_trace_consume_shm_if(u32 routing, u16 addr, u32 val)
{
	struct oracle_key *k;
	char cls[32];

	if (!oracle_loaded)
		oracle_load();
	shm_key(cls, sizeof(cls), routing);
	k = okey(cls, addr, 0);
	if (k && k->pos < k->n && k->vals[k->pos] == val)
		k->last = k->vals[k->pos++];
}

/*
 * The files compiled with the kernel headers have no stdlib: with
 * `-nostdinc` getenv() ends up implicitly declared, truncated to int. That is
 * why the environment is read from here, which is user code.
 */
const char *b43_test_env(const char *name)
{
	const char *v = getenv(name);

	return (v && *v) ? v : NULL;
}

long b43_test_env_long(const char *name, long def)
{
	const char *v = getenv(name);
	char *end;
	long n;

	if (!v || !*v)
		return def;
	n = strtol(v, &end, 0);
	if (*end)
		return def;
	return n;
}

u32 b43_trace_read_raw(u16 off, int width)
{
	return oracle_lookup("REG.RD", off, width);
}
