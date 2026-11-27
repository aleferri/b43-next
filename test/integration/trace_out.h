/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The emission of the ops, in ../unit/wrap.c's format.
 *
 * One line per op, as the unit harness produces it: `cpuN <class>
 * <operands>`. The comparison with the vendor capture is done by the same
 * tools (unit/compare.py, reverse-tools/tracelib.py), so the format is not a
 * free choice.
 *
 * The reads: without an oracle they return zero, and for a self-clearing
 * status bit zero is the value that spins a poll until the timeout. It is
 * the same problem ../unit solves with AC_READ_ORACLE and the read plans,
 * and here it is solved with the same, not reinvented.
 */
#ifndef B43_TEST_TRACE_OUT_H
#define B43_TEST_TRACE_OUT_H

#include <linux/types.h>

void b43_trace_op(const char *cls, u16 addr, u32 val, u16 mask, int has_mask);
void b43_trace_macctl(u32 val);
void b43_trace_gpio(const char *cls, u32 val, u32 mask);
void b43_trace_op32(const char *cls, u16 addr, u32 val, u32 mask);
void b43_trace_shm(const char *cls, u32 routing_off, u32 val, int width);
void b43_trace_raw(const char *cls, u16 off, u32 val, int width);
void b43_trace_block(const char *cls, u16 off, unsigned long count,
		     u8 reg_width);
void b43_trace_fill(void *buf, unsigned long count, u16 off, u8 reg_width);
u32  b43_trace_read(const char *cls, u16 addr, int width);
u32  b43_trace_read_raw(u16 off, int width);
/* Whether the capture carries at least one read of (cls, addr). */
int  b43_trace_has(const char *cls, u16 addr);
/* The PHY's table ports are the HT-PHY's (0x0072-0x0074), not the AC's */
void b43_trace_ht_tables(int on);
/* The vendor's RX ring 0 address, if the capture has its write */
int  b43_trace_vendor_rx_ringlo(u32 *v);
/* The shared memory reads, with the queue of the `routing` space (the high
 * 16 bits of B43_MMIO_SHM_CONTROL, the capture's `sel=`). */
u32  b43_trace_read_shm(u32 routing, u16 addr, int width);
/* Consume the head entry of that queue if it is `val`: a model served the
 * read, and it is the same one the vendor made. */
void b43_trace_consume_shm_if(u32 routing, u16 addr, u32 val);

/*
 * Read of an integer environment variable, for the stubs that need one but
 * are compiled with the kernel's flags and have no getenv: subsystem_stub.c
 * uses it for the channel and the width. Decimal and 0x... are accepted; if
 * the variable is missing or not a number it returns `def`.
 */
const char *b43_test_env(const char *name);
long b43_test_env_long(const char *name, long def);

/* One line of the suite's diagnostics on stderr, with a single integer. */
void b43_trace_note(const char *fmt, int arg);

/*
 * The environment's timeline, from B43_TIMELINE: the file
 * reverse-tools/timeline.py writes, `<t> <ep> <EVENT>` per line. The instants
 * are in microseconds. _wd() gives the first and the last round of the
 * vendor's watchdog and how many there are; _next() the next event, 0 at the
 * end.
 */
int b43_test_timeline_wd(long long *first_us, long long *last_us, int *n);
int b43_test_timeline_next(long long *t_us, char *kind, int len,
			   u32 *arg0, u32 *arg1);

#endif /* B43_TEST_TRACE_OUT_H */
