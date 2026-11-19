/* SPDX-License-Identifier: GPL-2.0 */
#ifndef B43_FWLOG_H_
#define B43_FWLOG_H_

#include <linux/types.h>

struct b43_wldev;
struct b43_dfsentry;

/*
 * A log channel from the microcode to debugfs: one mailbox of four 16 bit
 * cells of shared memory, wherever the microcode puts it. It publishes the
 * mailbox's word address at B43_SHM_SH_FWLOG, as it does the key table's
 * at B43_SHM_SH_KTP, or 0 for none; the host reads it when the core starts.
 *
 *   +0 KEY     what the value is, the microcode's own numbering
 *   +2 MARKER  sequence number of the entry, 1 to 0xffff and then 1 again
 *   +4 VALUE
 *   +6 ACK     MARKER of the last entry the host took
 *
 * The microcode writes KEY, VALUE and MARKER, the host ACK only. The
 * microcode zeroes the four cells before it publishes the address. The
 * mailbox is free when ACK equals MARKER: the microcode then writes KEY and
 * VALUE, and MARKER last. When it is not free the entry is dropped, but its
 * sequence number is spent all the same, so that the host counts the gap.
 */
#define B43_SHM_SH_FWLOG	0x019A

#define B43_FWLOG_KEY		0
#define B43_FWLOG_MARKER	2
#define B43_FWLOG_VALUE		4
#define B43_FWLOG_ACK		6

#ifdef CONFIG_B43_NEXT_DEBUG
void b43_fwlog_add(struct b43_wldev *dev, struct b43_dfsentry *e);
void b43_fwlog_remove(struct b43_dfsentry *e);
void b43_fwlog_start(struct b43_wldev *dev);
void b43_fwlog_cancel(struct b43_wldev *dev);
#else
static inline void b43_fwlog_start(struct b43_wldev *dev)
{
}
static inline void b43_fwlog_cancel(struct b43_wldev *dev)
{
}
#endif

#endif /* B43_FWLOG_H_ */
