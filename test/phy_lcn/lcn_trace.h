/* SPDX-License-Identifier: GPL-2.0 */
/*
 * What the driver side (flows.c, built against the kernel headers) and the
 * user side (trace.c, built against libc) share: plain C types only.
 */
#ifndef LCN_TRACE_H
#define LCN_TRACE_H

unsigned short lcn_mmio_read16(unsigned short offset);
void lcn_mmio_write16(unsigned short offset, unsigned short value);
unsigned int lcn_mmio_read32(unsigned short offset);
void lcn_mmio_write32(unsigned short offset, unsigned int value);
unsigned int lcn_wrap_read32(unsigned short offset);
void lcn_wrap_write32(unsigned short offset, unsigned int value);
/* ChipCommon, at its own window of the BAR: CC.RD/CC.WR ops */
unsigned int lcn_cc_read32(unsigned short offset);
void lcn_cc_write32(unsigned short offset, unsigned int value);
/* A shared memory cell, from the capture's reads of it; the core's, no ops */
unsigned short lcn_shm_read16(unsigned short offset);
void lcn_log(const char *what);
/* An nvram variable of the board as a number, or dflt if it has none. */
long lcn_nvram(const char *name, long dflt);
/* An environment variable as a number, or dflt. */
long lcn_env(const char *name, long dflt);
/* A call into b43's core, which the harness does not compile. */
void lcn_core(const char *fn);

/* Driver side: run one flow; nonzero if the name is unknown. */
int lcn_run_flow(const char *name);
/* Driver side: the device the flow runs, for the stubs (stubs.c) */
struct b43_wldev;
extern struct b43_wldev *lcn_dev;

#endif
