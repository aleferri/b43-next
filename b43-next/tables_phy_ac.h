/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef B43_TABLES_PHY_AC_H_
#define B43_TABLES_PHY_AC_H_

#include <linux/types.h>

struct b43_wldev;

#define B43_ACTAB_RFSEQ			0x07
#define B43_ACTAB_FEMCTRL		0x0a
#define B43_ACTAB_IQLOCAL		0x0c
#define B43_ACTAB_SAMPLE_PLAY		0x0e
#define B43_ACTAB_TXGAIN		0x20
#define B43_ACTAB_ESTPWR(core)		(0x40 + 0x20 * (core))
#define B43_ACTAB_LOFT(core)		(0x42 + 0x20 * (core))

/* Load the AC-PHY init tables into the PHY's table memory. */
void b43_phy_ac_tables_init(struct b43_wldev *dev);

/* Bulk write into one of the AC-PHY's internal tables. */
void b43_actab_write_bulk(struct b43_wldev *dev,
			  u16 id, u16 offset, u8 width,
			  size_t len, const void *data);
/*
 * write_bulk with an implicit all-zero source: emits the same op sequence
 * (gate peek, WR TABLE_ID/OFFSET, len cells) without making the caller
 * keep a buffer of zeroes around.
 */
void b43_actab_zerofill(struct b43_wldev *dev,
			u16 id, u16 offset, u8 width, size_t len);
/*
 * Variants for callers that already hold the 0x019e gate locked across a
 * run of bulk writes: they do not re-read it before each access.
 */
void b43_actab_write_bulk_locked(struct b43_wldev *dev,
				 u16 id, u16 offset, u8 width,
				 size_t len, const void *data);
void b43_actab_zerofill_locked(struct b43_wldev *dev,
			       u16 id, u16 offset, u8 width, size_t len);
/*
 * Self-contained variant: the table write inside its own
 * b43_phy_ac_tbl_write_lock()/unlock() pair, instead of relying on a lock
 * the caller holds across several of them.
 */
void b43_actab_write_bulk_scoped(struct b43_wldev *dev,
				 u16 id, u16 offset, u8 width,
				 size_t len, const void *data);
void b43_actab_read_bulk(struct b43_wldev *dev,
			 u16 id, u16 offset, u8 width,
			 size_t len, void *data);

/*
 * Save+set / restore of bit 0x0002 of B43_PHY_AC_REG_TBL_WRITE_GATE (PHY reg
 * 0x19E).
 */
u16  b43_phy_ac_tbl_write_lock(struct b43_wldev *dev);
void b43_phy_ac_tbl_write_unlock(struct b43_wldev *dev, u16 saved);
void b43_phy_ac_tbl_write_gate(struct b43_wldev *dev, bool lock);

const u16 (*b43_actab_get_tx_gain_table(struct b43_wldev *dev))[3];

extern const u16 b43_actab_rfseq_rx2tx_cmd[16];
extern const u16 b43_actab_rfseq_tx2rx_cmd[16];
extern const u16 b43_actab_rfseq_reset2rx_cmd[16];
extern const u16 b43_actab_rfseq_reset2rx_dly[16];
extern const u16 b43_actab_rfseq_updl_lpf_hpc[2];
extern const u16 b43_actab_rfseq_updl_tia_hpc[2];
extern const u16 b43_actab_rfseq_2_cmd[2][3][8];
extern const u16 b43_actab_rfseq_2_dly[2][3][8];
extern const u32 b43_actab_txv_for_spexp[243];

extern const u8 b43_actab_fem_femctrl6[32];
extern const u8 b43_actab_fem_fem5516_fc1[32];
extern const u8 b43_actab_fem_x29c_c1_fc2_sub1[32];
extern const u8 b43_actab_fem_fc3_sub3_c0[32];
extern const u8 b43_actab_fem_fc3_sub3_c1[32];
extern const u8 b43_actab_fem_fc3_sub3_c2[32];
extern const u8 b43_actab_fem_fc3_sub0_c0[32];
extern const u8 b43_actab_fem_fc3_sub0_c1[32];
extern const u8 b43_actab_fem_fc3_sub0_c2[32];

struct b43_actab_nshp {
	u16 nvar[5];
	u16 nvar_off[3];
	u16 a8[6], b8[6];
	u16 a10[7], b10[7];
};

extern const struct b43_actab_nshp b43_actab_nshp_5g;
extern const struct b43_actab_nshp b43_actab_nshp_2g;
extern const u16 b43_actab_glim_a[6];
extern const u16 b43_actab_glim_b[7];

extern const u16 b43_actab_tbl11_head[12][3];

extern const u16 b43_actab_farrow_vals[196];
extern const u16 b43_actab_farrow_vals_432x_media_a1[98];

#endif /* B43_TABLES_PHY_AC_H_ */
