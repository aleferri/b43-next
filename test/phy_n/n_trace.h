/* SPDX-License-Identifier: GPL-2.0 */
/*
 * What the driver side (flows.c, built against the kernel headers) and the
 * user side (trace.c, built against libc) share: plain C types only.
 */
#ifndef N_TRACE_H
#define N_TRACE_H

unsigned short n_mmio_read16(unsigned short offset);
void n_mmio_write16(unsigned short offset, unsigned short value);
unsigned int n_mmio_read32(unsigned short offset);
void n_mmio_write32(unsigned short offset, unsigned int value);
unsigned int n_wrap_read32(unsigned short offset);
void n_wrap_write32(unsigned short offset, unsigned int value);
/* ChipCommon, at its own window of the BAR: CC.RD/CC.WR ops */
unsigned int n_cc_read32(unsigned short offset);
void n_cc_write32(unsigned short offset, unsigned int value);
/* A function marker, ----FN:name---- or ----/FN:name----, if N_FN is set */
void n_marker(void *fn, int leave);
void n_log(const char *what);
/* An nvram variable of the board as a number, or dflt if it has none. */
long n_nvram(const char *name, long dflt);
/* An environment variable as a number, or dflt. */
long n_env(const char *name, long dflt);
/* A line of the driver's calibration report */
void n_report(const char *line);
/* A call into b43's core, which the harness does not compile. */
void n_core(const char *fn);

/* Driver side: run one flow; nonzero if the name is unknown. */
int n_run_flow(const char *name);

#endif
