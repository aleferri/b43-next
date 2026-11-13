/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_B43_PPR_H_
#define LINUX_B43_PPR_H_

#include "phy_common.h"

struct ssbn_sprom;
struct ieee80211_channel;

/*
 * PPR, power per rate: the highest power each rate may be sent at, one byte
 * per rate in quarter dBm (Q5.2). Every PHY family builds its table with one
 * of the SROM loaders below, then caps, offsets and floors it with the same
 * primitives and reads its targets and per-rate offsets back out of it.
 *
 * The table has room for every rate b43 sends: CCK, legacy OFDM and the MCS
 * of each transmit mode, OFDM and MCS per bandwidth. A loader fills the
 * groups its SROM layout describes and marks them loaded; the primitives act
 * on the loaded groups only, and an entry no loader filled reads as zero.
 */

/* The bands the SROM carries maximum powers and rate offsets for. */
enum b43_band {
	B43_BAND_2G = 0,
	B43_BAND_5G_LO = 1,
	B43_BAND_5G_MI = 2,
	B43_BAND_5G_HI = 3,
	B43_BAND_NUM,
};

enum b43_ppr_bw {
	B43_PPR_BW_20,
	B43_PPR_BW_40,
	B43_PPR_BW_80,
	B43_PPR_BW_NUM,
};

enum b43_ppr_mode {
	B43_PPR_SISO,		/* one stream on one chain */
	B43_PPR_CDD,		/* one stream, cyclic delay over two chains */
	B43_PPR_STBC,		/* one stream, space-time coded on two chains */
	B43_PPR_SDM,		/* two streams: MCS 8-15 at 0-7 */
	B43_PPR_MODE_NUM,
};

#define B43_PPR_CCK_RATES	4	/* 1, 2, 5.5, 11 Mb/s */
#define B43_PPR_OFDM_RATES	8	/* 6, 9, 12, 18, 24, 36, 48, 54 Mb/s */
#define B43_PPR_OFDM_MODES	(B43_PPR_CDD + 1)
#define B43_PPR_MCS_RATES	10	/* per stream: HT 0-7, VHT 0-9 */

#define B43_PPR_ENTRIES	(B43_PPR_CCK_RATES +				\
			 B43_PPR_BW_NUM * B43_PPR_OFDM_MODES *		\
			 B43_PPR_OFDM_RATES +				\
			 B43_PPR_BW_NUM * B43_PPR_MODE_NUM *		\
			 B43_PPR_MCS_RATES)

struct b43_ppr {
	union {
		u8 __all[B43_PPR_ENTRIES];
		struct {
			u8 cck[B43_PPR_CCK_RATES];
			u8 ofdm[B43_PPR_BW_NUM][B43_PPR_OFDM_MODES]
			       [B43_PPR_OFDM_RATES];
			u8 mcs[B43_PPR_BW_NUM][B43_PPR_MODE_NUM]
			      [B43_PPR_MCS_RATES];
		};
	};
	/* Private to ppr.c: the groups loaded, and how many MCS each has. */
	u32 loaded;
	u8 mcs_num;
};

void b43_ppr_clear(struct b43_ppr *ppr);
void b43_ppr_add(struct b43_ppr *ppr, int diff);
void b43_ppr_apply_max(struct b43_ppr *ppr, u8 max);
void b43_ppr_apply_min(struct b43_ppr *ppr, u8 min);
u8 b43_ppr_get_max(const struct b43_ppr *ppr);
/* The same over the OFDM and MCS rates of one bandwidth, CCK left out. */
u8 b43_ppr_get_bw_max(const struct b43_ppr *ppr, enum b43_ppr_bw bw);

bool b43_ppr_has_cck(const struct b43_ppr *ppr);
u8 b43_ppr_get_cck(const struct b43_ppr *ppr, unsigned int rate);
u8 b43_ppr_get_ofdm(const struct b43_ppr *ppr, enum b43_ppr_bw bw,
		    enum b43_ppr_mode mode, unsigned int rate);
u8 b43_ppr_get_mcs(const struct b43_ppr *ppr, enum b43_ppr_bw bw,
		   enum b43_ppr_mode mode, unsigned int mcs);

/* 80 and 40 MHz have their own rows, every other width the 20 MHz one. */
enum b43_ppr_bw b43_ppr_bw(enum nl80211_chan_width width);
/* The SROM band of a channel; false for one below 4900 MHz on 5 GHz. */
bool b43_ppr_band(const struct ieee80211_channel *chan, enum b43_band *band);

/*
 * The loaders clear the table, fill it from the SROM and return the maximum
 * power the rate offsets are taken from, 0 when the SROM has none for the
 * band.
 *
 * _legacy reads the single-chain fields, maxpwr_bg/al/a/ah, and the CCK and
 * OFDM offsets of rev 8 (zero in older layouts): the G-PHY, the LP-PHY and
 * the LCN-PHY. @ht also loads the 20 MHz MCS of one stream, from the first
 * two words of mcs*po, for the LCN-PHY.
 *
 * _r8 reads the per-core maxima of rev 8 to 10, at the lowest over
 * @coremask, and the OFDM and 20 MHz MCS offsets of every mode of rev 8: the
 * N-PHY and the HT-PHY. @cdd_stbc_po also takes the cddpo and stbcpo nibbles
 * off the CDD and STBC rates.
 *
 * _r8_40 adds the 40 MHz rows of rev 8 to a table _r8 filled from @maxp, for
 * the N-PHY. With @mcs40po (N-PHY rev 5 and later), words 4 and 5 of mcs*po
 * give one stream, CDD and STBC less their cddpo and stbcpo nibbles, words 6
 * and 7 SDM, and the OFDM rates of each mode take the power of its MCS;
 * without it, each 40 MHz rate takes the power of its 20 MHz one. @bw40po
 * then takes the bw40po nibble off every 40 MHz rate.
 *
 * _r9 reads the same maxima and the per-rate offsets of rev 9 and 10.
 *
 * _r11 is in the comments of ppr.c, by the SROM layout.
 */
u8 b43_ppr_load_sprom_legacy(struct b43_ppr *ppr,
			     const struct ssbn_sprom *sprom,
			     enum b43_band band, bool ht);

u8 b43_ppr_sprom_r8_maxpwr(const struct ssbn_sprom *sprom, enum b43_band band,
			   u8 coremask);
u8 b43_ppr_load_sprom_r8(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			 enum b43_band band, u8 coremask, bool cdd_stbc_po);
void b43_ppr_load_sprom_r8_40(struct b43_ppr *ppr,
			      const struct ssbn_sprom *sprom,
			      enum b43_band band, u8 maxp, bool mcs40po,
			      bool bw40po);
u8 b43_ppr_load_sprom_r9(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			 enum b43_band band, u8 coremask);

unsigned int b43_ppr_sprom_r11_subband(u16 chan, enum nl80211_chan_width width);
bool b43_ppr_sprom_r11_has_subband_po(const struct ssbn_sprom *sprom);
u8 b43_ppr_load_sprom_r11(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			  u8 coremask, unsigned int num_cores, u16 chan,
			  enum nl80211_chan_width width, u8 ceiling);
void b43_ppr_load_sprom_r11_spacing(struct b43_ppr *ppr,
				    const struct ssbn_sprom *sprom, u16 chan,
				    enum nl80211_chan_width width);

#endif /* LINUX_B43_PPR_H_ */
