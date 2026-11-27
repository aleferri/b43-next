/* SPDX-License-Identifier: GPL-2.0
 * Stub for b43/main.h. Scratch code includes this for utility prototypes
 * we do not need in the test build; leave empty and let the linker sort
 * out concrete symbols via wrap.c.
 */
#ifndef _STUB_MAIN_H
#define _STUB_MAIN_H
#include "b43.h"

static inline int b43_is_cck_rate(int rate)
{
	return (rate == B43_CCK_RATE_1MB ||
		rate == B43_CCK_RATE_2MB ||
		rate == B43_CCK_RATE_5MB || rate == B43_CCK_RATE_11MB);
}

/* wrap.c's, as main.c's: its read of the direct map is a SHM.RD */
u16 b43_rate_block(struct b43_wldev *dev, u16 hw_value);

/* wrap.c's, as main.c's: the board's chains, those of the harness's board */
void b43_board_chains(struct b43_wldev *dev, u8 *rxchain, u8 *txchain);

/* The survey's noise, which the trace does not see */
static inline void b43_noise_pwr_report(struct b43_wldev *dev, const u32 *pwr,
					unsigned int cores)
{
}
#endif
