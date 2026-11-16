/* SPDX-License-Identifier: GPL-2.0 */
/*
 * What the driver side (flows.c, built against the kernel headers) and the
 * user side (trace.c, built against libc) share: plain C types only.
 */
#ifndef LP_TRACE_H
#define LP_TRACE_H

unsigned short lp_mmio_read16(unsigned short offset);
void lp_mmio_write16(unsigned short offset, unsigned short value);
unsigned int lp_mmio_read32(unsigned short offset);
void lp_mmio_write32(unsigned short offset, unsigned int value);
/* ChipCommon, at its own window of the BAR: CC.RD/CC.WR ops */
unsigned int lp_cc_read32(unsigned short offset);
void lp_cc_write32(unsigned short offset, unsigned int value);
/* A function marker, ----FN:name---- or ----/FN:name----, if LP_FN is set */
void lp_marker(void *fn, int leave);
/* The driver's calibration report: peek mode on and off */
void lp_peek(int on);
void lp_log(const char *what);
/* An environment variable as a number, or dflt. */
long lp_env(const char *name, long dflt);
/* The board's SROM words, from the dump LP_SROM names; their count, or 0. */
int lp_srom(unsigned short *words, int max);
/* A line of the driver's calibration report */
void lp_report(const char *line);
/* A call into b43's core, which the harness does not compile. */
void lp_core(const char *fn);

/* Driver side: run one flow; nonzero if the name is unknown. */
int lp_run_flow(const char *name);

#endif
