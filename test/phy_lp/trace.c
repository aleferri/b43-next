// SPDX-License-Identifier: GPL-2.0
/*
 * User side of the LP-PHY harness. It turns the driver's MMIO and
 * ChipCommon accesses into the ops mmio2ops prints for an mmiotrace capture,
 * serves every read from the capture's own reads, and compares the flow's
 * ops with the capture's, op by op.
 *
 *   lp-trace FLOW OPS FIRST LAST [-v]
 *   lp-trace FLOW OPS FIRST LAST -o TRACE
 *
 * OPS is mmio2ops output, FIRST and LAST the #numbers of the window. With
 * -o the flow's ops go to TRACE, for ../unit/cmp_skip.py --bus, and the
 * reads it took from the window are summed up on stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <unistd.h>
#include "lp_trace.h"

#define PHY_VER		0x3e0
#define PHY_CTL		0x3fc
#define PHY_DATA	0x3fe
/* The radio registers of core revs before 24, the BCM4312's (rev 15) */
#define RADIO_CTL	0x3f6
#define RADIO_DATA	0x3fa

/* The LP-PHY's table port */
#define TBL_ADDR	0x455
#define TBL_LO		0x456
#define TBL_HI		0x457

/* The read address of a radio register of LP-PHY rev 0/1: its write one | 0x100 */
#define RADIO_READ	0x100

#define LINE		256

struct op {
	char text[LINE];
};

struct ops {
	struct op *v;
	int n, cap;
};

static struct ops ref, out;
static int nlog;

/* The next line of @l, which grows as needed */
static char *op_slot(struct ops *l)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? 2 * l->cap : 65536;
		l->v = realloc(l->v, l->cap * sizeof(*l->v));
		if (!l->v)
			exit(2);
	}
	return l->v[l->n++].text;
}

/* One queue of read values per register, in the capture's order. */
struct queue {
	char key[24];
	unsigned int *vals;
	long *nums;	/* the #number of each read in the capture */
	int n, cap, pos;
	/* reads of wl's that differ from its last write: the hardware's */
	int changed;
};
/* Per register, and per table cell: thousands */
static struct queue queues[16384];
static int nqueues, exhausted;

/* Last value written per register, for a read the capture does not have. */
struct cell {
	char key[24];
	unsigned int val;
};
static struct cell cells[16384], wl_cells[16384];
static int ncells, nwl_cells;

/*
 * A register the hardware changes: wl read something else than it last
 * wrote more often than its resets of the PHY explain. The others are served
 * from b43's own writes once it has written them, so that a driver reading
 * back its state does not take wl's from another point of the stream.
 */
#define HW_CHANGED	3

static unsigned int phy_addr, radio_addr;

/*
 * The table port: 0x455 = table id << 10 | offset, 0x457 the high half of a
 * 32-bit cell, 0x456 the low half (or an 8- or 16-bit cell), and the offset
 * advances on every 0x456 access. Table reads are served per cell, keyed by
 * (id, offset, half), from wl's reads of that cell.
 */
struct tbl_port {
	unsigned int id, off;
};
static struct tbl_port b43_tbl;

/* The key of a table data access at PHY address a; 0 if a is not one */
static int tbl_key(struct tbl_port *t, unsigned int a, char *key, size_t n)
{
	if (a != TBL_LO && a != TBL_HI)
		return 0;
	snprintf(key, n, "TBL %02x %03x %c", t->id, t->off, a == TBL_HI ? 'H' : 'L');
	return 1;
}

static void tbl_after(struct tbl_port *t, unsigned int a, unsigned int v,
		      int write)
{
	if (a == TBL_ADDR && write) {
		t->id = v >> 10;
		t->off = v & 0x3ff;
	} else if (a == TBL_LO) {
		t->off++;
	}
}

/*
 * Peek mode (the driver's calibration report): reads are served from the
 * mirror of the driver's writes without consuming the capture's reads, and
 * nothing is emitted, so the report changes neither the stream nor the
 * read oracle.
 */
static int peek;
static struct {
	unsigned int phy_addr, radio_addr;
	char phy_last;
	unsigned int tbl_id, tbl_off;
} peek_save;
static char phy_last;

static struct queue *queue(const char *key, int create)
{
	int i;

	for (i = 0; i < nqueues; i++)
		if (!strcmp(queues[i].key, key))
			return &queues[i];
	if (!create)
		return NULL;
	if (nqueues == (int)(sizeof(queues) / sizeof(queues[0]))) {
		fprintf(stderr, "too many read queues\n");
		exit(2);
	}
	snprintf(queues[nqueues].key, sizeof(queues[0].key), "%s", key);
	return &queues[nqueues++];
}

static void push(const char *key, unsigned int v, long n)
{
	struct queue *q = queue(key, 1);

	if (q->n == q->cap) {
		q->cap = q->cap ? 2 * q->cap : 16;
		q->vals = realloc(q->vals, q->cap * sizeof(*q->vals));
		q->nums = realloc(q->nums, q->cap * sizeof(*q->nums));
		if (!q->vals || !q->nums)
			exit(2);
	}
	q->vals[q->n] = v;
	q->nums[q->n++] = n;
}

static struct cell *cell_of(struct cell *c, int *n, const char *key,
			    int create)
{
	int i;

	for (i = 0; i < *n; i++)
		if (!strcmp(c[i].key, key))
			return &c[i];
	if (!create)
		return NULL;
	if (*n == (int)(sizeof(cells) / sizeof(cells[0]))) {
		fprintf(stderr, "too many cells\n");
		exit(2);
	}
	snprintf(c[*n].key, sizeof(c[0].key), "%s", key);
	return &c[(*n)++];
}

static void store(const char *key, unsigned int val)
{
	cell_of(cells, &ncells, key, 1)->val = val;
}

static unsigned int serve(const char *key)
{
	struct queue *q = queue(key, 0);
	struct cell *c = cell_of(cells, &ncells, key, 0);

	if (peek)
		return c ? c->val : 0;

	if (c && strncmp(key, "TBL", 3) && (!q || q->changed < HW_CHANGED)) {
		if (q && q->pos < q->n)
			q->pos++;
		return c->val;
	}
	if (q && q->pos < q->n)
		return q->vals[q->pos++];
	exhausted++;
	if (getenv("LP_DEBUG"))
		fprintf(stderr, "read past the window: %s (op %d)\n", key, out.n + 1);
	return c ? c->val : 0;
}

static void emit(const char *fmt, const char *cls, unsigned int a,
		 unsigned int v)
{
	if (peek)
		return;
	snprintf(op_slot(&out), LINE, fmt, cls, a, v);
}

/*
 * A data access with no select of its own, but a write after a read (the
 * maskset's), is printed as mmio2ops prints wl's wide accesses.
 */
static void phy_data(const char *cls, int wide, unsigned int v)
{
	if (peek)
		return;
	if (wide)
		snprintf(op_slot(&out), LINE, "%sW val=0x%04x", cls, v);
	else
		snprintf(op_slot(&out), LINE, "%s addr=0x%04x val=0x%04x",
			 cls, phy_addr, v);
}

unsigned short lp_mmio_read16(unsigned short offset)
{
	char key[24];
	unsigned int v;

	switch (offset) {
	case PHY_DATA:
		if (!tbl_key(&b43_tbl, phy_addr, key, sizeof(key)))
			snprintf(key, sizeof(key), "PHY %04x", phy_addr);
		v = serve(key);
		tbl_after(&b43_tbl, phy_addr, v, 0);
		phy_data("PHY.RD", phy_last != 0, v);
		phy_last = 'R';
		return v;
	case RADIO_DATA:
		snprintf(key, sizeof(key), "RAD %04x", radio_addr);
		v = serve(key);
		emit("%s addr=0x%04x val=0x%04x", "RAD.RD", radio_addr, v);
		return v;
	case PHY_VER:
		/* b43's posting read every 64 writes: mmio2ops drops it. */
		return 0;
	default:
		snprintf(key, sizeof(key), "REG %04x", offset);
		v = serve(key);
		emit("%s off=0x%04x val=0x%04x", "REG.RD", offset, v);
		return v;
	}
}

void lp_mmio_write16(unsigned short offset, unsigned short value)
{
	char key[24];

	switch (offset) {
	case PHY_CTL:
		phy_addr = value;
		phy_last = 0;
		return;
	case PHY_DATA:
		phy_data("PHY.WR", phy_last == 'W', value);
		phy_last = 'W';
		if (!tbl_key(&b43_tbl, phy_addr, key, sizeof(key)))
			snprintf(key, sizeof(key), "PHY %04x", phy_addr);
		store(key, value);
		tbl_after(&b43_tbl, phy_addr, value, 1);
		return;
	case RADIO_CTL:
		radio_addr = value;
		return;
	case RADIO_DATA:
		emit("%s addr=0x%04x val=0x%04x", "RAD.WR", radio_addr, value);
		snprintf(key, sizeof(key), "RAD %04x", radio_addr | RADIO_READ);
		store(key, value);
		return;
	default:
		emit("%s off=0x%04x val=0x%04x", "REG.WR", offset, value);
		snprintf(key, sizeof(key), "REG %04x", offset);
		store(key, value);
	}
}

unsigned int lp_mmio_read32(unsigned short offset)
{
	char key[24];
	unsigned int v;

	snprintf(key, sizeof(key), "REG %04x", offset);
	v = serve(key);
	emit("%s off=0x%04x val=0x%08x", "REG.RD", offset, v);
	return v;
}

void lp_mmio_write32(unsigned short offset, unsigned int value)
{
	char key[24];

	if (peek)
		;
	else if (offset == 0x120)
		snprintf(op_slot(&out), LINE, "MAC.MCTRL val=0x%08x", value);
	else if (offset != 0x120)
		emit("%s off=0x%04x val=0x%08x", "REG.WR", offset, value);
	snprintf(key, sizeof(key), "REG %04x", offset);
	store(key, value);
}

unsigned short lp_shm_read16(unsigned short routing, unsigned short offset)
{
	char key[24];
	unsigned int v;

	snprintf(key, sizeof(key), "OBJ %04x", offset);
	v = serve(key);
	if (!peek)
		snprintf(op_slot(&out), LINE,
			 "OBJ.RD addr=0x%04x val=0x%04x sel=0x%x", offset, v,
			 routing << 16);
	return v;
}

void lp_shm_write16(unsigned short routing, unsigned short offset,
		    unsigned short value)
{
	char key[24];

	if (!peek)
		snprintf(op_slot(&out), LINE,
			 "OBJ.WR addr=0x%04x val=0x%04x sel=0x%x", offset, value,
			 routing << 16);
	snprintf(key, sizeof(key), "OBJ %04x", offset);
	store(key, value);
}

unsigned int lp_cc_read32(unsigned short offset)
{
	char key[24];
	unsigned int v;

	snprintf(key, sizeof(key), "CC %04x", offset);
	v = serve(key);
	emit("%s off=0x%04x val=0x%08x", "CC.RD", offset, v);
	return v;
}

void lp_cc_write32(unsigned short offset, unsigned int value)
{
	char key[24];

	emit("%s off=0x%04x val=0x%08x", "CC.WR", offset, value);
	snprintf(key, sizeof(key), "CC %04x", offset);
	store(key, value);
}

/* The binary's own symbols, for the markers */
static struct sym {
	unsigned long addr;
	char name[64];
} *syms;
static int nsyms, fn_markers = -1;

static void load_syms(void)
{
	char cmd[300], line[200], exe[256];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	FILE *p;

	if (n <= 0)
		return;
	exe[n] = 0;
	snprintf(cmd, sizeof(cmd), "nm '%s'", exe);
	p = popen(cmd, "r");
	while (p && fgets(line, sizeof(line), p)) {
		unsigned long a;
		char t, name[64];

		if (sscanf(line, "%lx %c %63s", &a, &t, name) != 3 ||
		    (t != 't' && t != 'T'))
			continue;
		syms = realloc(syms, (nsyms + 1) * sizeof(*syms));
		syms[nsyms].addr = a;
		snprintf(syms[nsyms++].name, sizeof(syms[0].name), "%s", name);
	}
	if (p)
		pclose(p);
}

void lp_marker(void *fn, int leave)
{
	int i;
	const char *name = "?";

	if (fn_markers < 0) {
		fn_markers = getenv("LP_FN") != NULL;
		if (fn_markers)
			load_syms();
	}
	if (!fn_markers)
		return;
	for (i = 0; i < nsyms; i++)
		if (syms[i].addr == (unsigned long)fn)
			name = syms[i].name;
	snprintf(op_slot(&out), LINE, "----%sFN:%s----", leave ? "/" : "", name);
}

/*
 * LP_STEPS lists, comma separated, the #number where wl's window of each
 * marked step begins: its reads are served from there on, the reads wl made
 * before it are skipped, so that every step reads its own window.
 */
void lp_mark(const char *what)
{
	static const char *steps;
	static int started;
	char *end;
	long first;
	int i;

	snprintf(op_slot(&out), LINE, "----%s----", what);
	if (!started) {
		steps = getenv("LP_STEPS");
		started = 1;
	}
	if (!steps || !*steps)
		return;
	first = strtol(steps, &end, 10);
	steps = *end ? end + 1 : end;
	for (i = 0; i < nqueues; i++)
		while (queues[i].pos < queues[i].n &&
		       queues[i].nums[queues[i].pos] < first)
			queues[i].pos++;
}

void lp_peek(int on)
{
	if (on) {
		peek_save.phy_addr = phy_addr;
		peek_save.radio_addr = radio_addr;
		peek_save.phy_last = phy_last;
		peek_save.tbl_id = b43_tbl.id;
		peek_save.tbl_off = b43_tbl.off;
	} else {
		phy_addr = peek_save.phy_addr;
		radio_addr = peek_save.radio_addr;
		phy_last = peek_save.phy_last;
		b43_tbl.id = peek_save.tbl_id;
		b43_tbl.off = peek_save.tbl_off;
	}
	peek = on;
}

void lp_log(const char *what)
{
	nlog++;
	fprintf(stderr, "b43: %s%s", what,
		what[strlen(what) - 1] == '\n' ? "" : "\n");
}

long lp_env(const char *name, long dflt)
{
	const char *v = getenv(name);

	return v ? strtol(v, NULL, 0) : dflt;
}

int lp_channels(unsigned int *ch, int max)
{
	const char *p = getenv("LP_SCAN");
	int n = 0;

	while (p && *p && n < max) {
		char *end;

		ch[n++] = strtoul(p, &end, 10);
		p = *end ? end + 1 : end;
	}
	return n;
}

/* b43-next-traces' srom.txt: "0xNNN: wwww wwww ..." per line */
int lp_srom(unsigned short *words, int max)
{
	const char *path = getenv("LP_SROM");
	char line[256];
	int n = 0;
	FILE *f = path ? fopen(path, "r") : NULL;

	while (f && fgets(line, sizeof(line), f)) {
		char *p = strchr(line, ':');
		unsigned long at = strtoul(line, NULL, 16) / 2, w;
		char *end;

		if (!p)
			continue;
		for (p++; ; p = end) {
			w = strtoul(p, &end, 16);
			if (end == p)
				break;
			if (at < (unsigned long)max) {
				words[at] = w;
				n = n > (int)at + 1 ? n : (int)at + 1;
			}
			at++;
		}
	}
	if (f)
		fclose(f);
	return n;
}

void lp_report(const char *line)
{
	fprintf(stderr, "%s", line);
}

/* The kernel's string formatting, for the driver side: libc's */
#include <stdarg.h>
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int n;

	if (!size)
		return 0;
	n = vsnprintf(buf, size, fmt, args);
	return n < 0 ? 0 : ((size_t)n >= size ? (int)size - 1 : n);
}

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vscnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

/* Calls into b43's core, by name, for the summary of a trace run. */
static struct {
	const char *fn;
	int n;
} core_calls[32];
static int ncore;

void lp_core(const char *fn)
{
	int i;

	for (i = 0; i < ncore; i++)
		if (core_calls[i].fn == fn)
			break;
	if (i == ncore && ncore < 32)
		core_calls[ncore++].fn = fn;
	if (i < 32)
		core_calls[i].n++;
}

/* The window of the capture, without timestamps, numbers and reg names. */
static int load(const char *path, long first, long last)
{
	struct tbl_port wl_tbl = { 0, 0 };
	regex_t re;
	regmatch_t m[4];
	char line[512], key[24];
	FILE *f = fopen(path, "r");

	if (!f) {
		perror(path);
		return -1;
	}
	regcomp(&re, "#([0-9]+) +cpu[0-9]+ +([A-Z0-9.]+) +(.*)$", REG_EXTENDED);
	while (fgets(line, sizeof(line), f)) {
		char cls[24], args[200], *r;
		unsigned int a, v;
		long n;

		line[strcspn(line, "\n")] = 0;
		if (regexec(&re, line, 4, m, 0))
			continue;
		n = strtol(line + m[1].rm_so, NULL, 10);
		if (n < first || n > last)
			continue;
		snprintf(cls, sizeof(cls), "%.*s", (int)(m[2].rm_eo - m[2].rm_so),
			 line + m[2].rm_so);
		if (!strcmp(cls, "MARK") || !strcmp(cls, "WIN"))
			continue;
		snprintf(args, sizeof(args), "%s", line + m[3].rm_so);
		r = strstr(args, " reg=");
		if (r)
			*r = 0;
		snprintf(op_slot(&ref), LINE, "%s %s", cls, args);

		if (!strncmp(cls, "PHY.", 4) &&
		    sscanf(args, "addr=0x%x val=0x%x", &a, &v) == 2) {
			int rd = !strcmp(cls, "PHY.RD");

			if (rd && tbl_key(&wl_tbl, a, key, sizeof(key))) {
				push(key, v, n);
				tbl_after(&wl_tbl, a, v, 0);
				continue;
			}
			tbl_after(&wl_tbl, a, v, !rd);
		}
		if ((!strcmp(cls, "PHY.WR") || !strcmp(cls, "RAD.WR")) &&
		    sscanf(args, "addr=0x%x val=0x%x", &a, &v) == 2) {
			snprintf(key, sizeof(key), "%.3s %04x", cls,
				 cls[0] == 'R' ? a | RADIO_READ : a);
			cell_of(wl_cells, &nwl_cells, key, 1)->val = v;
			continue;
		}
		if (strcmp(cls + strlen(cls) - 3, ".RD"))
			continue;
		if (sscanf(args, "addr=0x%x val=0x%x", &a, &v) == 2)
			snprintf(key, sizeof(key), "%.3s %04x", cls, a);
		else if (sscanf(args, "off=0x%x val=0x%x", &a, &v) == 2)
			/* the window the op is in: REG, CC, PCIE, WRAP */
			snprintf(key, sizeof(key), "%.*s %04x",
				 (int)(strchr(cls, '.') - cls), cls, a);
		else
			continue;
		push(key, v, n);
		{
			struct cell *w = cell_of(wl_cells, &nwl_cells, key, 0);

			if (w && w->val != v)
				queue(key, 0)->changed++;
			if (w)
				w->val = v;
		}
	}
	fclose(f);
	regfree(&re);
	return 0;
}

int main(int argc, char **argv)
{
	int i, verbose = argc > 5 && !strcmp(argv[5], "-v");
	const char *trace = argc > 6 && !strcmp(argv[5], "-o") ? argv[6] : NULL;
	int unread = 0, r = 1;

	if (argc < 5) {
		fprintf(stderr, "usage: %s FLOW OPS FIRST LAST [-v | -o TRACE]\n", argv[0]);
		return 2;
	}
	if (load(argv[2], atol(argv[3]), atol(argv[4])))
		return 2;
	if (lp_run_flow(argv[1])) {
		fprintf(stderr, "unknown flow %s\n", argv[1]);
		return 2;
	}
	for (i = 0; i < nqueues; i++)
		unread += queues[i].n - queues[i].pos;

	if (verbose)
		for (i = 0; i < out.n; i++)
			printf("%s\n", out.v[i].text);
	if (trace) {
		FILE *f = fopen(trace, "w");

		if (!f) {
			perror(trace);
			return 2;
		}
		for (i = 0; i < out.n; i++)
			fprintf(f, out.v[i].text[0] == '-' ? "%s\n" : "cpu0 %s\n",
				out.v[i].text);
		fclose(f);
		fprintf(stderr, "%d ops; reads past the window %d, window reads "
			"not taken %d, b43 messages %d\n", out.n, exhausted, unread,
			nlog);
		for (i = 0; i < ncore; i++)
			fprintf(stderr, "core, no ops: %s x%d\n",
				core_calls[i].fn, core_calls[i].n);
		return 0;
	}

	for (i = 0; i < out.n && i < ref.n; i++) {
		r = !strcmp(out.v[i].text, ref.v[i].text);
		if (!r)
			break;
	}
	if (i == out.n && i == ref.n && !exhausted && !unread && !nlog) {
		printf("PASS %-12s %d ops\n", argv[1], ref.n);
		return 0;
	}
	printf("FAIL %-12s ops %d/%d, first difference at op %d of the window\n",
	       argv[1], out.n, ref.n, i + 1);
	if (i < ref.n || i < out.n)
		printf("     wl: %s\n     b43: %s\n", i < ref.n ? ref.v[i].text : "(end)",
		       i < out.n ? out.v[i].text : "(end)");
	printf("     reads past the capture %d, capture reads not taken %d, "
	       "b43 messages %d\n", exhausted, unread, nlog);
	return 1;
}
