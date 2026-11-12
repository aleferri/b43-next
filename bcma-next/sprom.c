/*
 * Broadcom specific AMBA
 * SPROM reading
 *
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcmn_private.h"

#include <bcma-next/bcmn.h>
#include <bcma-next/bcmn_regs.h>
#include "../nvram/bcm47xx_sprom.h"
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

static int bcmn_fill_sprom_with_fallback(struct bcmn_bus *bus,
					 struct ssbn_sprom *out)
{
	int err;

	err = bcm47xx_get_sprom_bcma(bus, out);
	if (err)
		goto fail;

	bcmn_info(bus, "Using SPROM revision %d provided by platform.\n",
		  bus->sprom.revision);
	return 0;
fail:
	bcmn_warn(bus, "Using fallback SPROM failed (err %d)\n", err);
	return err;
}

/**************************************************
 * R/W ops.
 **************************************************/

static void bcmn_sprom_read(struct bcmn_bus *bus, u16 offset, u16 *sprom,
			    size_t words)
{
	int i;
	for (i = 0; i < words; i++)
		sprom[i] = bcmn_read16(bus->drv_cc.core, offset + (i * 2));
}

/**************************************************
 * Validation.
 **************************************************/

static inline u8 bcmn_crc8(u8 crc, u8 data)
{
	/* Polynomial:   x^8 + x^7 + x^6 + x^4 + x^2 + 1   */
	static const u8 t[] = {
		0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
		0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
		0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
		0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
		0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
		0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
		0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
		0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
		0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
		0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
		0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
		0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
		0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
		0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
		0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
		0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
		0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
		0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
		0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
		0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
		0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
		0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
		0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
		0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
		0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
		0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
		0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
		0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
		0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
		0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
		0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
		0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
	};
	return t[crc ^ data];
}

static u8 bcmn_sprom_crc(const u16 *sprom, size_t words)
{
	int word;
	u8 crc = 0xFF;

	for (word = 0; word < words - 1; word++) {
		crc = bcmn_crc8(crc, sprom[word] & 0x00FF);
		crc = bcmn_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = bcmn_crc8(crc, sprom[words - 1] & 0x00FF);
	crc ^= 0xFF;

	return crc;
}

static int bcmn_sprom_check_crc(const u16 *sprom, size_t words)
{
	u8 crc;
	u8 expected_crc;
	u16 tmp;

	crc = bcmn_sprom_crc(sprom, words);
	tmp = sprom[words - 1] & SSBN_SPROM_REVISION_CRC;
	expected_crc = tmp >> SSBN_SPROM_REVISION_CRC_SHIFT;
	if (crc != expected_crc)
		return -EPROTO;

	return 0;
}

static int bcmn_sprom_valid(struct bcmn_bus *bus, const u16 *sprom,
			    size_t words)
{
	u16 revision;
	int err;

	err = bcmn_sprom_check_crc(sprom, words);
	if (err)
		return err;

	revision = sprom[words - 1] & SSBN_SPROM_REVISION_REV;
	if (revision < 8 || revision > 11) {
		pr_err("Unsupported SPROM revision: %d\n", revision);
		return -ENOENT;
	}

	bus->sprom.revision = revision;
	bcmn_info(bus, "Found SPROM revision %d\n", revision);

	return 0;
}

/**************************************************
 * SPROM extraction.
 **************************************************/

#define SPOFF(offset)	((offset) / sizeof(u16))

#define SPEX(_field, _offset, _mask, _shift)	\
	bus->sprom._field = ((sprom[SPOFF(_offset)] & (_mask)) >> (_shift))

#define SPEX32(_field, _offset, _mask, _shift)	\
	bus->sprom._field = ((((u32)sprom[SPOFF((_offset)+2)] << 16 | \
				sprom[SPOFF(_offset)]) & (_mask)) >> (_shift))

#define SPEX_ARRAY8(_field, _offset, _mask, _shift)	\
	do {	\
		SPEX(_field[0], _offset +  0, _mask, _shift);	\
		SPEX(_field[1], _offset +  2, _mask, _shift);	\
		SPEX(_field[2], _offset +  4, _mask, _shift);	\
		SPEX(_field[3], _offset +  6, _mask, _shift);	\
		SPEX(_field[4], _offset +  8, _mask, _shift);	\
		SPEX(_field[5], _offset + 10, _mask, _shift);	\
		SPEX(_field[6], _offset + 12, _mask, _shift);	\
		SPEX(_field[7], _offset + 14, _mask, _shift);	\
	} while (0)

static s8 sprom_extract_antgain(const u16 *in, u16 offset, u16 mask, u16 shift)
{
	u16 v;
	u8 gain;

	v = in[SPOFF(offset)];
	gain = (v & mask) >> shift;
	if (gain == 0xFF) {
		gain = 8; /* If unset use 2dBm */
	} else {
		/* Q5.2 Fractional part is stored in 0xC0 */
		gain = ((gain & 0xC0) >> 6) | ((gain & 0x3F) << 2);
	}

	return (s8)gain;
}

/* Rev 8 only: rev 9 and 10 hold their power per rate in the same words. */
static void bcmn_sprom_extract_po_r8(struct bcmn_bus *bus, const u16 *sprom)
{
	SPEX(cck2gpo, SSBN_SPROM8_CCK2GPO, ~0, 0);
	SPEX32(ofdm2gpo, SSBN_SPROM8_OFDM2GPO, ~0, 0);
	SPEX32(ofdm5glpo, SSBN_SPROM8_OFDM5GLPO, ~0, 0);
	SPEX32(ofdm5gpo, SSBN_SPROM8_OFDM5GPO, ~0, 0);
	SPEX32(ofdm5ghpo, SSBN_SPROM8_OFDM5GHPO, ~0, 0);

	SPEX_ARRAY8(mcs2gpo, SSBN_SPROM8_2G_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5gpo, SSBN_SPROM8_5G_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5glpo, SSBN_SPROM8_5GL_MCSPO, ~0, 0);
	SPEX_ARRAY8(mcs5ghpo, SSBN_SPROM8_5GH_MCSPO, ~0, 0);

	SPEX(cddpo, SSBN_SPROM8_CDDPO, ~0, 0);
	SPEX(stbcpo, SSBN_SPROM8_STBCPO, ~0, 0);
	SPEX(bw40po, SSBN_SPROM8_BW40PO, ~0, 0);
	SPEX(bwduppo, SSBN_SPROM8_BWDUPPO, ~0, 0);
}

static void bcmn_sprom_extract_po_r9(struct bcmn_bus *bus, const u16 *sprom)
{
	SPEX(cckbw202gpo, SSBN_SPROM9_2GPO_CCKBW20, ~0, 0);
	SPEX(cckbw20ul2gpo, SSBN_SPROM9_2GPO_CCKBW20UL, ~0, 0);
	SPEX32(legofdmbw202gpo, SSBN_SPROM9_2GPO_LOFDMBW20, ~0, 0);
	SPEX32(legofdmbw20ul2gpo, SSBN_SPROM9_2GPO_LOFDMBW20UL, ~0, 0);
	SPEX32(legofdmbw205glpo, SSBN_SPROM9_5GLPO_LOFDMBW20, ~0, 0);
	SPEX32(legofdmbw20ul5glpo, SSBN_SPROM9_5GLPO_LOFDMBW20UL, ~0, 0);
	SPEX32(legofdmbw205gmpo, SSBN_SPROM9_5GMPO_LOFDMBW20, ~0, 0);
	SPEX32(legofdmbw20ul5gmpo, SSBN_SPROM9_5GMPO_LOFDMBW20UL, ~0, 0);
	SPEX32(legofdmbw205ghpo, SSBN_SPROM9_5GHPO_LOFDMBW20, ~0, 0);
	SPEX32(legofdmbw20ul5ghpo, SSBN_SPROM9_5GHPO_LOFDMBW20UL, ~0, 0);

	SPEX32(mcsbw202gpo, SSBN_SPROM9_2GPO_MCSBW20, ~0, 0);
	SPEX32(mcsbw20ul2gpo, SSBN_SPROM9_2GPO_MCSBW20UL, ~0, 0);
	SPEX32(mcsbw402gpo, SSBN_SPROM9_2GPO_MCSBW40, ~0, 0);
	SPEX32(mcsbw205glpo, SSBN_SPROM9_5GLPO_MCSBW20, ~0, 0);
	SPEX32(mcsbw20ul5glpo, SSBN_SPROM9_5GLPO_MCSBW20UL, ~0, 0);
	SPEX32(mcsbw405glpo, SSBN_SPROM9_5GLPO_MCSBW40, ~0, 0);
	SPEX32(mcsbw205gmpo, SSBN_SPROM9_5GMPO_MCSBW20, ~0, 0);
	SPEX32(mcsbw20ul5gmpo, SSBN_SPROM9_5GMPO_MCSBW20UL, ~0, 0);
	SPEX32(mcsbw405gmpo, SSBN_SPROM9_5GMPO_MCSBW40, ~0, 0);
	SPEX32(mcsbw205ghpo, SSBN_SPROM9_5GHPO_MCSBW20, ~0, 0);
	SPEX32(mcsbw20ul5ghpo, SSBN_SPROM9_5GHPO_MCSBW20UL, ~0, 0);
	SPEX32(mcsbw405ghpo, SSBN_SPROM9_5GHPO_MCSBW40, ~0, 0);

	SPEX(mcs32po, SSBN_SPROM9_PO_MCS32, ~0, 0);
	SPEX(legofdm40duppo, SSBN_SPROM9_PO_LOFDM40DUP, ~0, 0);
}

static void bcmn_sprom_extract_r8(struct bcmn_bus *bus, const u16 *sprom)
{
	u16 v, o;
	int i;
	static const u16 pwr_info_offset[] = {
		SSBN_SROM8_PWR_INFO_CORE0, SSBN_SROM8_PWR_INFO_CORE1,
		SSBN_SROM8_PWR_INFO_CORE2, SSBN_SROM8_PWR_INFO_CORE3
	};
	BUILD_BUG_ON(ARRAY_SIZE(pwr_info_offset) !=
			ARRAY_SIZE(bus->sprom.core_pwr_info));

	for (i = 0; i < 3; i++) {
		v = sprom[SPOFF(SSBN_SPROM8_IL0MAC) + i];
		*(((__be16 *)bus->sprom.il0mac) + i) = cpu_to_be16(v);
	}

	SPEX(board_rev, SSBN_SPROM8_BOARDREV, ~0, 0);
	SPEX(board_type, SSBN_SPROM1_SPID, ~0, 0);

	SPEX(txpid2g[0], SSBN_SPROM4_TXPID2G01, SSBN_SPROM4_TXPID2G0,
	     SSBN_SPROM4_TXPID2G0_SHIFT);
	SPEX(txpid2g[1], SSBN_SPROM4_TXPID2G01, SSBN_SPROM4_TXPID2G1,
	     SSBN_SPROM4_TXPID2G1_SHIFT);
	SPEX(txpid2g[2], SSBN_SPROM4_TXPID2G23, SSBN_SPROM4_TXPID2G2,
	     SSBN_SPROM4_TXPID2G2_SHIFT);
	SPEX(txpid2g[3], SSBN_SPROM4_TXPID2G23, SSBN_SPROM4_TXPID2G3,
	     SSBN_SPROM4_TXPID2G3_SHIFT);

	SPEX(txpid5gl[0], SSBN_SPROM4_TXPID5GL01, SSBN_SPROM4_TXPID5GL0,
	     SSBN_SPROM4_TXPID5GL0_SHIFT);
	SPEX(txpid5gl[1], SSBN_SPROM4_TXPID5GL01, SSBN_SPROM4_TXPID5GL1,
	     SSBN_SPROM4_TXPID5GL1_SHIFT);
	SPEX(txpid5gl[2], SSBN_SPROM4_TXPID5GL23, SSBN_SPROM4_TXPID5GL2,
	     SSBN_SPROM4_TXPID5GL2_SHIFT);
	SPEX(txpid5gl[3], SSBN_SPROM4_TXPID5GL23, SSBN_SPROM4_TXPID5GL3,
	     SSBN_SPROM4_TXPID5GL3_SHIFT);

	SPEX(txpid5g[0], SSBN_SPROM4_TXPID5G01, SSBN_SPROM4_TXPID5G0,
	     SSBN_SPROM4_TXPID5G0_SHIFT);
	SPEX(txpid5g[1], SSBN_SPROM4_TXPID5G01, SSBN_SPROM4_TXPID5G1,
	     SSBN_SPROM4_TXPID5G1_SHIFT);
	SPEX(txpid5g[2], SSBN_SPROM4_TXPID5G23, SSBN_SPROM4_TXPID5G2,
	     SSBN_SPROM4_TXPID5G2_SHIFT);
	SPEX(txpid5g[3], SSBN_SPROM4_TXPID5G23, SSBN_SPROM4_TXPID5G3,
	     SSBN_SPROM4_TXPID5G3_SHIFT);

	SPEX(txpid5gh[0], SSBN_SPROM4_TXPID5GH01, SSBN_SPROM4_TXPID5GH0,
	     SSBN_SPROM4_TXPID5GH0_SHIFT);
	SPEX(txpid5gh[1], SSBN_SPROM4_TXPID5GH01, SSBN_SPROM4_TXPID5GH1,
	     SSBN_SPROM4_TXPID5GH1_SHIFT);
	SPEX(txpid5gh[2], SSBN_SPROM4_TXPID5GH23, SSBN_SPROM4_TXPID5GH2,
	     SSBN_SPROM4_TXPID5GH2_SHIFT);
	SPEX(txpid5gh[3], SSBN_SPROM4_TXPID5GH23, SSBN_SPROM4_TXPID5GH3,
	     SSBN_SPROM4_TXPID5GH3_SHIFT);

	SPEX(boardflags_lo, SSBN_SPROM8_BFLLO, ~0, 0);
	SPEX(boardflags_hi, SSBN_SPROM8_BFLHI, ~0, 0);
	SPEX(boardflags2_lo, SSBN_SPROM8_BFL2LO, ~0, 0);
	SPEX(boardflags2_hi, SSBN_SPROM8_BFL2HI, ~0, 0);

	SPEX(alpha2[0], SSBN_SPROM8_CCODE, 0xff00, 8);
	SPEX(alpha2[1], SSBN_SPROM8_CCODE, 0x00ff, 0);

	/* Extract core's power info */
	for (i = 0; i < ARRAY_SIZE(pwr_info_offset); i++) {
		o = pwr_info_offset[i];
		SPEX(core_pwr_info[i].itssi_2g, o + SSBN_SROM8_2G_MAXP_ITSSI,
			SSBN_SPROM8_2G_ITSSI, SSBN_SPROM8_2G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_2g, o + SSBN_SROM8_2G_MAXP_ITSSI,
			SSBN_SPROM8_2G_MAXP, 0);

		SPEX(core_pwr_info[i].pa_2g[0], o + SSBN_SROM8_2G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[1], o + SSBN_SROM8_2G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_2g[2], o + SSBN_SROM8_2G_PA_2, ~0, 0);

		SPEX(core_pwr_info[i].itssi_5g, o + SSBN_SROM8_5G_MAXP_ITSSI,
			SSBN_SPROM8_5G_ITSSI, SSBN_SPROM8_5G_ITSSI_SHIFT);
		SPEX(core_pwr_info[i].maxpwr_5g, o + SSBN_SROM8_5G_MAXP_ITSSI,
			SSBN_SPROM8_5G_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gh, o + SSBN_SPROM8_5GHL_MAXP,
			SSBN_SPROM8_5GH_MAXP, 0);
		SPEX(core_pwr_info[i].maxpwr_5gl, o + SSBN_SPROM8_5GHL_MAXP,
			SSBN_SPROM8_5GL_MAXP, SSBN_SPROM8_5GL_MAXP_SHIFT);

		SPEX(core_pwr_info[i].pa_5gl[0], o + SSBN_SROM8_5GL_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[1], o + SSBN_SROM8_5GL_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gl[2], o + SSBN_SROM8_5GL_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[0], o + SSBN_SROM8_5G_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[1], o + SSBN_SROM8_5G_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5g[2], o + SSBN_SROM8_5G_PA_2, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[0], o + SSBN_SROM8_5GH_PA_0, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[1], o + SSBN_SROM8_5GH_PA_1, ~0, 0);
		SPEX(core_pwr_info[i].pa_5gh[2], o + SSBN_SROM8_5GH_PA_2, ~0, 0);
	}

	SPEX(fem.ghz2.tssipos, SSBN_SPROM8_FEM2G, SSBN_SROM8_FEM_TSSIPOS,
	     SSBN_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz2.extpa_gain, SSBN_SPROM8_FEM2G, SSBN_SROM8_FEM_EXTPA_GAIN,
	     SSBN_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz2.pdet_range, SSBN_SPROM8_FEM2G, SSBN_SROM8_FEM_PDET_RANGE,
	     SSBN_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz2.tr_iso, SSBN_SPROM8_FEM2G, SSBN_SROM8_FEM_TR_ISO,
	     SSBN_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz2.antswlut, SSBN_SPROM8_FEM2G, SSBN_SROM8_FEM_ANTSWLUT,
	     SSBN_SROM8_FEM_ANTSWLUT_SHIFT);

	SPEX(fem.ghz5.tssipos, SSBN_SPROM8_FEM5G, SSBN_SROM8_FEM_TSSIPOS,
	     SSBN_SROM8_FEM_TSSIPOS_SHIFT);
	SPEX(fem.ghz5.extpa_gain, SSBN_SPROM8_FEM5G, SSBN_SROM8_FEM_EXTPA_GAIN,
	     SSBN_SROM8_FEM_EXTPA_GAIN_SHIFT);
	SPEX(fem.ghz5.pdet_range, SSBN_SPROM8_FEM5G, SSBN_SROM8_FEM_PDET_RANGE,
	     SSBN_SROM8_FEM_PDET_RANGE_SHIFT);
	SPEX(fem.ghz5.tr_iso, SSBN_SPROM8_FEM5G, SSBN_SROM8_FEM_TR_ISO,
	     SSBN_SROM8_FEM_TR_ISO_SHIFT);
	SPEX(fem.ghz5.antswlut, SSBN_SPROM8_FEM5G, SSBN_SROM8_FEM_ANTSWLUT,
	     SSBN_SROM8_FEM_ANTSWLUT_SHIFT);

	SPEX(ant_available_a, SSBN_SPROM8_ANTAVAIL, SSBN_SPROM8_ANTAVAIL_A,
	     SSBN_SPROM8_ANTAVAIL_A_SHIFT);
	SPEX(ant_available_bg, SSBN_SPROM8_ANTAVAIL, SSBN_SPROM8_ANTAVAIL_BG,
	     SSBN_SPROM8_ANTAVAIL_BG_SHIFT);
	SPEX(maxpwr_bg, SSBN_SPROM8_MAXP_BG, SSBN_SPROM8_MAXP_BG_MASK, 0);
	SPEX(itssi_bg, SSBN_SPROM8_MAXP_BG, SSBN_SPROM8_ITSSI_BG,
	     SSBN_SPROM8_ITSSI_BG_SHIFT);
	SPEX(maxpwr_a, SSBN_SPROM8_MAXP_A, SSBN_SPROM8_MAXP_A_MASK, 0);
	SPEX(itssi_a, SSBN_SPROM8_MAXP_A, SSBN_SPROM8_ITSSI_A,
	     SSBN_SPROM8_ITSSI_A_SHIFT);
	SPEX(maxpwr_ah, SSBN_SPROM8_MAXP_AHL, SSBN_SPROM8_MAXP_AH_MASK, 0);
	SPEX(maxpwr_al, SSBN_SPROM8_MAXP_AHL, SSBN_SPROM8_MAXP_AL_MASK,
	     SSBN_SPROM8_MAXP_AL_SHIFT);
	SPEX(gpio0, SSBN_SPROM8_GPIOA, SSBN_SPROM8_GPIOA_P0, 0);
	SPEX(gpio1, SSBN_SPROM8_GPIOA, SSBN_SPROM8_GPIOA_P1,
	     SSBN_SPROM8_GPIOA_P1_SHIFT);
	SPEX(gpio2, SSBN_SPROM8_GPIOB, SSBN_SPROM8_GPIOB_P2, 0);
	SPEX(gpio3, SSBN_SPROM8_GPIOB, SSBN_SPROM8_GPIOB_P3,
	     SSBN_SPROM8_GPIOB_P3_SHIFT);
	SPEX(tri2g, SSBN_SPROM8_TRI25G, SSBN_SPROM8_TRI2G, 0);
	SPEX(tri5g, SSBN_SPROM8_TRI25G, SSBN_SPROM8_TRI5G,
	     SSBN_SPROM8_TRI5G_SHIFT);
	SPEX(tri5gl, SSBN_SPROM8_TRI5GHL, SSBN_SPROM8_TRI5GL, 0);
	SPEX(tri5gh, SSBN_SPROM8_TRI5GHL, SSBN_SPROM8_TRI5GH,
	     SSBN_SPROM8_TRI5GH_SHIFT);
	SPEX(rxpo2g, SSBN_SPROM8_RXPO, SSBN_SPROM8_RXPO2G,
	     SSBN_SPROM8_RXPO2G_SHIFT);
	SPEX(rxpo5g, SSBN_SPROM8_RXPO, SSBN_SPROM8_RXPO5G,
	     SSBN_SPROM8_RXPO5G_SHIFT);
	SPEX(rssismf2g, SSBN_SPROM8_RSSIPARM2G, SSBN_SPROM8_RSSISMF2G, 0);
	SPEX(rssismc2g, SSBN_SPROM8_RSSIPARM2G, SSBN_SPROM8_RSSISMC2G,
	     SSBN_SPROM8_RSSISMC2G_SHIFT);
	SPEX(rssisav2g, SSBN_SPROM8_RSSIPARM2G, SSBN_SPROM8_RSSISAV2G,
	     SSBN_SPROM8_RSSISAV2G_SHIFT);
	SPEX(bxa2g, SSBN_SPROM8_RSSIPARM2G, SSBN_SPROM8_BXA2G,
	     SSBN_SPROM8_BXA2G_SHIFT);
	SPEX(rssismf5g, SSBN_SPROM8_RSSIPARM5G, SSBN_SPROM8_RSSISMF5G, 0);
	SPEX(rssismc5g, SSBN_SPROM8_RSSIPARM5G, SSBN_SPROM8_RSSISMC5G,
	     SSBN_SPROM8_RSSISMC5G_SHIFT);
	SPEX(rssisav5g, SSBN_SPROM8_RSSIPARM5G, SSBN_SPROM8_RSSISAV5G,
	     SSBN_SPROM8_RSSISAV5G_SHIFT);
	SPEX(bxa5g, SSBN_SPROM8_RSSIPARM5G, SSBN_SPROM8_BXA5G,
	     SSBN_SPROM8_BXA5G_SHIFT);

	SPEX(pa0b0, SSBN_SPROM8_PA0B0, ~0, 0);
	SPEX(pa0b1, SSBN_SPROM8_PA0B1, ~0, 0);
	SPEX(pa0b2, SSBN_SPROM8_PA0B2, ~0, 0);
	SPEX(pa1b0, SSBN_SPROM8_PA1B0, ~0, 0);
	SPEX(pa1b1, SSBN_SPROM8_PA1B1, ~0, 0);
	SPEX(pa1b2, SSBN_SPROM8_PA1B2, ~0, 0);
	SPEX(pa1lob0, SSBN_SPROM8_PA1LOB0, ~0, 0);
	SPEX(pa1lob1, SSBN_SPROM8_PA1LOB1, ~0, 0);
	SPEX(pa1lob2, SSBN_SPROM8_PA1LOB2, ~0, 0);
	SPEX(pa1hib0, SSBN_SPROM8_PA1HIB0, ~0, 0);
	SPEX(pa1hib1, SSBN_SPROM8_PA1HIB1, ~0, 0);
	SPEX(pa1hib2, SSBN_SPROM8_PA1HIB2, ~0, 0);

	/* Extract the antenna gain values. */
	bus->sprom.antenna_gain.a0 = sprom_extract_antgain(sprom,
							   SSBN_SPROM8_AGAIN01,
							   SSBN_SPROM8_AGAIN0,
							   SSBN_SPROM8_AGAIN0_SHIFT);
	bus->sprom.antenna_gain.a1 = sprom_extract_antgain(sprom,
							   SSBN_SPROM8_AGAIN01,
							   SSBN_SPROM8_AGAIN1,
							   SSBN_SPROM8_AGAIN1_SHIFT);
	bus->sprom.antenna_gain.a2 = sprom_extract_antgain(sprom,
							   SSBN_SPROM8_AGAIN23,
							   SSBN_SPROM8_AGAIN2,
							   SSBN_SPROM8_AGAIN2_SHIFT);
	bus->sprom.antenna_gain.a3 = sprom_extract_antgain(sprom,
							   SSBN_SPROM8_AGAIN23,
							   SSBN_SPROM8_AGAIN3,
							   SSBN_SPROM8_AGAIN3_SHIFT);

	SPEX(leddc_on_time, SSBN_SPROM8_LEDDC, SSBN_SPROM8_LEDDC_ON,
	     SSBN_SPROM8_LEDDC_ON_SHIFT);
	SPEX(leddc_off_time, SSBN_SPROM8_LEDDC, SSBN_SPROM8_LEDDC_OFF,
	     SSBN_SPROM8_LEDDC_OFF_SHIFT);

	SPEX(txchain, SSBN_SPROM8_TXRXC, SSBN_SPROM8_TXRXC_TXCHAIN,
	     SSBN_SPROM8_TXRXC_TXCHAIN_SHIFT);
	SPEX(rxchain, SSBN_SPROM8_TXRXC, SSBN_SPROM8_TXRXC_RXCHAIN,
	     SSBN_SPROM8_TXRXC_RXCHAIN_SHIFT);
	SPEX(antswitch, SSBN_SPROM8_TXRXC, SSBN_SPROM8_TXRXC_SWITCH,
	     SSBN_SPROM8_TXRXC_SWITCH_SHIFT);

	SPEX(opo, SSBN_SPROM8_OFDM2GPO, 0x00ff, 0);

	SPEX(rawtempsense, SSBN_SPROM8_RAWTS, SSBN_SPROM8_RAWTS_RAWTEMP,
	     SSBN_SPROM8_RAWTS_RAWTEMP_SHIFT);
	SPEX(measpower, SSBN_SPROM8_RAWTS, SSBN_SPROM8_RAWTS_MEASPOWER,
	     SSBN_SPROM8_RAWTS_MEASPOWER_SHIFT);
	SPEX(tempsense_slope, SSBN_SPROM8_OPT_CORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMP_SLOPE,
	     SSBN_SPROM8_OPT_CORRX_TEMP_SLOPE_SHIFT);
	SPEX(tempcorrx, SSBN_SPROM8_OPT_CORRX, SSBN_SPROM8_OPT_CORRX_TEMPCORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMPCORRX_SHIFT);
	SPEX(tempsense_option, SSBN_SPROM8_OPT_CORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMP_OPTION,
	     SSBN_SPROM8_OPT_CORRX_TEMP_OPTION_SHIFT);
	SPEX(freqoffset_corr, SSBN_SPROM8_HWIQ_IQSWP,
	     SSBN_SPROM8_HWIQ_IQSWP_FREQ_CORR,
	     SSBN_SPROM8_HWIQ_IQSWP_FREQ_CORR_SHIFT);
	SPEX(iqcal_swp_dis, SSBN_SPROM8_HWIQ_IQSWP,
	     SSBN_SPROM8_HWIQ_IQSWP_IQCAL_SWP,
	     SSBN_SPROM8_HWIQ_IQSWP_IQCAL_SWP_SHIFT);
	SPEX(hw_iqcal_en, SSBN_SPROM8_HWIQ_IQSWP, SSBN_SPROM8_HWIQ_IQSWP_HW_IQCAL,
	     SSBN_SPROM8_HWIQ_IQSWP_HW_IQCAL_SHIFT);

	SPEX(tempthresh, SSBN_SPROM8_THERMAL, SSBN_SPROM8_THERMAL_TRESH,
	     SSBN_SPROM8_THERMAL_TRESH_SHIFT);
	SPEX(tempoffset, SSBN_SPROM8_THERMAL, SSBN_SPROM8_THERMAL_OFFSET,
	     SSBN_SPROM8_THERMAL_OFFSET_SHIFT);
	SPEX(phycal_tempdelta, SSBN_SPROM8_TEMPDELTA,
	     SSBN_SPROM8_TEMPDELTA_PHYCAL,
	     SSBN_SPROM8_TEMPDELTA_PHYCAL_SHIFT);
	SPEX(temps_period, SSBN_SPROM8_TEMPDELTA, SSBN_SPROM8_TEMPDELTA_PERIOD,
	     SSBN_SPROM8_TEMPDELTA_PERIOD_SHIFT);
	SPEX(temps_hysteresis, SSBN_SPROM8_TEMPDELTA,
	     SSBN_SPROM8_TEMPDELTA_HYSTERESIS,
	     SSBN_SPROM8_TEMPDELTA_HYSTERESIS_SHIFT);

	if (bus->sprom.revision == 8)
		bcmn_sprom_extract_po_r8(bus, sprom);
	else
		bcmn_sprom_extract_po_r9(bus, sprom);
}

static void bcmn_sprom_unpack_rxgains(struct ssbn_sprom_rxgains *out,
				      int chain, u8 packed)
{
	out->trelnabyp[chain] = (packed >> 7) & 0x01;
	out->triso[chain]     = (packed >> 3) & 0x0f;
	out->elnagain[chain]  =  packed       & 0x07;
}

static void bcmn_sprom_extract_r11(struct bcmn_bus *bus, const u16 *sprom)
{
	static const u16 pwr_info_offset[] = {
		SSBN_SPROM11_PWR_INFO_CORE0,
		SSBN_SPROM11_PWR_INFO_CORE1,
		SSBN_SPROM11_PWR_INFO_CORE2,
	};
	u16 v;
	int i, j;

	BUILD_BUG_ON(ARRAY_SIZE(pwr_info_offset) >
		     ARRAY_SIZE(bus->sprom.core_pwr_info));

	/* Header: rev-11 offsets, rev-8 masks/shifts. */
	for (i = 0; i < 3; i++) {
		v = sprom[SPOFF(SSBN_SPROM11_IL0MAC) + i];
		*(((__be16 *)bus->sprom.il0mac) + i) = cpu_to_be16(v);
	}

	SPEX(board_rev, SSBN_SPROM8_BOARDREV, ~0, 0);
	SPEX(board_type, SSBN_SPROM1_SPID, ~0, 0);
	SPEX(boardflags_lo, SSBN_SPROM8_BFLLO, ~0, 0);
	SPEX(boardflags_hi, SSBN_SPROM8_BFLHI, ~0, 0);
	SPEX(boardflags2_lo, SSBN_SPROM8_BFL2LO, ~0, 0);
	SPEX(boardflags2_hi, SSBN_SPROM8_BFL2HI, ~0, 0);
	SPEX32(boardflags3, SSBN_SPROM11_BFL3, ~0, 0);

	SPEX(alpha2[0], SSBN_SPROM11_CCODE, 0xff00, 8);
	SPEX(alpha2[1], SSBN_SPROM11_CCODE, 0x00ff, 0);
	SPEX(gpio0, SSBN_SPROM11_GPIOA, SSBN_SPROM8_GPIOA_P0, 0);
	SPEX(gpio1, SSBN_SPROM11_GPIOA, SSBN_SPROM8_GPIOA_P1,
	     SSBN_SPROM8_GPIOA_P1_SHIFT);
	SPEX(gpio2, SSBN_SPROM11_GPIOB, SSBN_SPROM8_GPIOB_P2, 0);
	SPEX(gpio3, SSBN_SPROM11_GPIOB, SSBN_SPROM8_GPIOB_P3,
	     SSBN_SPROM8_GPIOB_P3_SHIFT);
	SPEX(ant_available_a, SSBN_SPROM11_ANTAVAIL, SSBN_SPROM8_ANTAVAIL_A,
	     SSBN_SPROM8_ANTAVAIL_A_SHIFT);
	SPEX(ant_available_bg, SSBN_SPROM11_ANTAVAIL, SSBN_SPROM8_ANTAVAIL_BG,
	     SSBN_SPROM8_ANTAVAIL_BG_SHIFT);
	SPEX(txchain, SSBN_SPROM11_TXRXC, SSBN_SPROM8_TXRXC_TXCHAIN,
	     SSBN_SPROM8_TXRXC_TXCHAIN_SHIFT);
	SPEX(rxchain, SSBN_SPROM11_TXRXC, SSBN_SPROM8_TXRXC_RXCHAIN,
	     SSBN_SPROM8_TXRXC_RXCHAIN_SHIFT);
	SPEX(antswitch, SSBN_SPROM11_TXRXC, SSBN_SPROM8_TXRXC_SWITCH,
	     SSBN_SPROM8_TXRXC_SWITCH_SHIFT);
	SPEX(subband5gver, SSBN_SPROM11_SUBBAND5GVER, 0x00ff, 0);

	/* FEM / PA control block. */
	SPEX(tssiposslope2g, SSBN_SPROM11_FEM_CFG1,
	     SSBN_SPROM11_FEM_TSSIPOSSLOPE, 0);
	SPEX(epagain2g, SSBN_SPROM11_FEM_CFG1, SSBN_SPROM11_FEM_EPAGAIN,
	     SSBN_SPROM11_FEM_EPAGAIN_SHIFT);
	SPEX(pdgain2g, SSBN_SPROM11_FEM_CFG1, SSBN_SPROM11_FEM_PDGAIN,
	     SSBN_SPROM11_FEM_PDGAIN_SHIFT);
	SPEX(tworangetssi2g, SSBN_SPROM11_FEM_CFG1,
	     SSBN_SPROM11_FEM_TWORANGETSSI, SSBN_SPROM11_FEM_TWORANGETSSI_SHIFT);
	SPEX(papdcap2g, SSBN_SPROM11_FEM_CFG1, SSBN_SPROM11_FEM_PAPDCAP,
	     SSBN_SPROM11_FEM_PAPDCAP_SHIFT);
	SPEX(femctrl, SSBN_SPROM11_FEM_CFG1, SSBN_SPROM11_FEM_CTRL,
	     SSBN_SPROM11_FEM_CTRL_SHIFT);
	SPEX(tssiposslope5g, SSBN_SPROM11_FEM_CFG2,
	     SSBN_SPROM11_FEM_TSSIPOSSLOPE, 0);
	SPEX(epagain5g, SSBN_SPROM11_FEM_CFG2, SSBN_SPROM11_FEM_EPAGAIN,
	     SSBN_SPROM11_FEM_EPAGAIN_SHIFT);
	SPEX(pdgain5g, SSBN_SPROM11_FEM_CFG2, SSBN_SPROM11_FEM_PDGAIN,
	     SSBN_SPROM11_FEM_PDGAIN_SHIFT);
	SPEX(tworangetssi5g, SSBN_SPROM11_FEM_CFG2,
	     SSBN_SPROM11_FEM_TWORANGETSSI, SSBN_SPROM11_FEM_TWORANGETSSI_SHIFT);
	SPEX(papdcap5g, SSBN_SPROM11_FEM_CFG2, SSBN_SPROM11_FEM_PAPDCAP,
	     SSBN_SPROM11_FEM_PAPDCAP_SHIFT);
	SPEX(gainctrlsph, SSBN_SPROM11_FEM_CFG2,
	     SSBN_SPROM11_FEM_GAINCTRLSPH, SSBN_SPROM11_FEM_GAINCTRLSPH_SHIFT);

	/* TSSI floor. */
	SPEX(tssifloor2g, SSBN_SPROM11_TSSIFLOOR_2G,
	     SSBN_SPROM11_TSSIFLOOR_MASK, 0);
	for (i = 0; i < 4; i++)
		SPEX(tssifloor5g[i], SSBN_SPROM11_TSSIFLOOR_5GL + i * 2,
		     SSBN_SPROM11_TSSIFLOOR_MASK, 0);

	/* Per-chain power info. */
	for (i = 0; i < ARRAY_SIZE(pwr_info_offset); i++) {
		struct ssbn_sprom_core_pwr_info *p = &bus->sprom.core_pwr_info[i];
		u16 base = pwr_info_offset[i];

		p->maxp2ga = sprom[SPOFF(base + SSBN_SPROM11_PWR_MAXP2GA)] & 0xff;

		for (j = 0; j < 3; j++)
			p->pa2ga[j] = sprom[SPOFF(base + SSBN_SPROM11_PWR_PA2GA) + j];

		v = sprom[SPOFF(base + SSBN_SPROM11_PWR_MAXP5GA)];
		p->maxp5ga[0] = v & 0xff;
		p->maxp5ga[1] = (v >> 8) & 0xff;
		v = sprom[SPOFF(base + SSBN_SPROM11_PWR_MAXP5GA) + 1];
		p->maxp5ga[2] = v & 0xff;
		p->maxp5ga[3] = (v >> 8) & 0xff;

		for (j = 0; j < 12; j++)
			p->pa5ga[j] = sprom[SPOFF(base + SSBN_SPROM11_PWR_PA5GA) + j];

		v = sprom[SPOFF(base + SSBN_SPROM11_PWR_RXGAINS0)];
		bcmn_sprom_unpack_rxgains(&bus->sprom.rxgains_5gm, i,  v       & 0xff);
		bcmn_sprom_unpack_rxgains(&bus->sprom.rxgains_5gh, i, (v >> 8) & 0xff);
		v = sprom[SPOFF(base + SSBN_SPROM11_PWR_RXGAINS1)];
		bcmn_sprom_unpack_rxgains(&bus->sprom.rxgains_2g,  i,  v       & 0xff);
		bcmn_sprom_unpack_rxgains(&bus->sprom.rxgains_5gl, i, (v >> 8) & 0xff);
	}

	SPEX(rpcal2g,    SSBN_SPROM11_RPCAL_2G,  ~0, 0);
	SPEX(rpcal5gb[0], SSBN_SPROM11_RPCAL_5GL, ~0, 0);
	SPEX(rpcal5gb[1], SSBN_SPROM11_RPCAL_5GM, ~0, 0);
	SPEX(rpcal5gb[2], SSBN_SPROM11_RPCAL_5GH, ~0, 0);
	SPEX(rpcal5gb[3], SSBN_SPROM11_RPCAL_5GU, ~0, 0);

	SPEX(pdoffset2g40ma[0], SSBN_SPROM11_PDOFFSET2G40M,
	     SSBN_SPROM11_PDOFFSET2G40M_MA0, SSBN_SPROM11_PDOFFSET2G40M_MA0_SHIFT);
	SPEX(pdoffset2g40ma[1], SSBN_SPROM11_PDOFFSET2G40M,
	     SSBN_SPROM11_PDOFFSET2G40M_MA1, SSBN_SPROM11_PDOFFSET2G40M_MA1_SHIFT);
	SPEX(pdoffset2g40ma[2], SSBN_SPROM11_PDOFFSET2G40M,
	     SSBN_SPROM11_PDOFFSET2G40M_MA2, SSBN_SPROM11_PDOFFSET2G40M_MA2_SHIFT);
	SPEX(pdoffset2g40mvalid, SSBN_SPROM11_PDOFFSET2G40M,
	     SSBN_SPROM11_PDOFFSET2G40M_VALID,
	     SSBN_SPROM11_PDOFFSET2G40M_VALID_SHIFT);

	for (i = 0; i < 3; i++) {
		bus->sprom.pdoffset40ma[i] =
			sprom[SPOFF(SSBN_SPROM11_PDOFFSET40MA) + i];
		bus->sprom.pdoffset80ma[i] =
			sprom[SPOFF(SSBN_SPROM11_PDOFFSET80MA) + i];
	}

	SPEX(cckbw202gpo,           SSBN_SPROM11_CCKBW202GPO,           ~0, 0);
	SPEX(cckbw20ul2gpo,         SSBN_SPROM11_CCKBW20UL2GPO,         ~0, 0);
	SPEX32(mcsbw202gpo,         SSBN_SPROM11_MCSBW202GPO,           ~0, 0);
	SPEX32(mcsbw402gpo,         SSBN_SPROM11_MCSBW402GPO,           ~0, 0);
	SPEX(dot11agofdmhrbw202gpo, SSBN_SPROM11_DOT11AGOFDMHRBW202GPO, ~0, 0);
	SPEX(ofdmlrbw202gpo,        SSBN_SPROM11_OFDMLRBW202GPO,        ~0, 0);
	SPEX32(mcsbw205glpo,        SSBN_SPROM11_MCSBW205GLPO,          ~0, 0);
	SPEX32(mcsbw405glpo,        SSBN_SPROM11_MCSBW405GLPO,          ~0, 0);
	SPEX32(mcsbw805glpo,        SSBN_SPROM11_MCSBW805GLPO,          ~0, 0);
	SPEX32(mcsbw205gmpo,        SSBN_SPROM11_MCSBW205GMPO,          ~0, 0);
	SPEX32(mcsbw405gmpo,        SSBN_SPROM11_MCSBW405GMPO,          ~0, 0);
	SPEX32(mcsbw805gmpo,        SSBN_SPROM11_MCSBW805GMPO,          ~0, 0);
	SPEX32(mcsbw205ghpo,        SSBN_SPROM11_MCSBW205GHPO,          ~0, 0);
	SPEX32(mcsbw405ghpo,        SSBN_SPROM11_MCSBW405GHPO,          ~0, 0);
	SPEX32(mcsbw805ghpo,        SSBN_SPROM11_MCSBW805GHPO,          ~0, 0);

	SPEX(mcslr5gpo[0],          SSBN_SPROM11_MCSLR5GLPO,            ~0, 0);
	SPEX(mcslr5gpo[1],          SSBN_SPROM11_MCSLR5GMPO,            ~0, 0);
	SPEX(mcslr5gpo[2],          SSBN_SPROM11_MCSLR5GHPO,            ~0, 0);
	SPEX(sb20in40hrpo,          SSBN_SPROM11_SB20IN40HRPO,          ~0, 0);
	SPEX(sb20in40lrpo,          SSBN_SPROM11_SB20IN40LRPO,          ~0, 0);
	SPEX(sb20in80and160hr5gpo[0], SSBN_SPROM11_SB20IN80AND160HR5GLPO, ~0, 0);
	SPEX(sb20in80and160hr5gpo[1], SSBN_SPROM11_SB20IN80AND160HR5GMPO, ~0, 0);
	SPEX(sb20in80and160hr5gpo[2], SSBN_SPROM11_SB20IN80AND160HR5GHPO, ~0, 0);
	SPEX(sb20in80and160lr5gpo[0], SSBN_SPROM11_SB20IN80AND160LR5GLPO, ~0, 0);
	SPEX(sb20in80and160lr5gpo[1], SSBN_SPROM11_SB20IN80AND160LR5GMPO, ~0, 0);
	SPEX(sb20in80and160lr5gpo[2], SSBN_SPROM11_SB20IN80AND160LR5GHPO, ~0, 0);
	SPEX(sb40and80hr5gpo[0],    SSBN_SPROM11_SB40AND80HR5GLPO,      ~0, 0);
	SPEX(sb40and80hr5gpo[1],    SSBN_SPROM11_SB40AND80HR5GMPO,      ~0, 0);
	SPEX(sb40and80hr5gpo[2],    SSBN_SPROM11_SB40AND80HR5GHPO,      ~0, 0);
	SPEX(sb40and80lr5gpo[0],    SSBN_SPROM11_SB40AND80LR5GLPO,      ~0, 0);
	SPEX(sb40and80lr5gpo[1],    SSBN_SPROM11_SB40AND80LR5GMPO,      ~0, 0);
	SPEX(sb40and80lr5gpo[2],    SSBN_SPROM11_SB40AND80LR5GHPO,      ~0, 0);
	SPEX(dot11agduphrpo,        SSBN_SPROM11_DOT11AGDUPHRPO,        ~0, 0);
	SPEX(dot11agduplrpo,        SSBN_SPROM11_DOT11AGDUPLRPO,        ~0, 0);

	SPEX(tempthresh, SSBN_SPROM11_THERMAL, SSBN_SPROM8_THERMAL_TRESH,
	     SSBN_SPROM8_THERMAL_TRESH_SHIFT);
	SPEX(tempoffset, SSBN_SPROM11_THERMAL, SSBN_SPROM8_THERMAL_OFFSET,
	     SSBN_SPROM8_THERMAL_OFFSET_SHIFT);
	SPEX(rawtempsense, SSBN_SPROM11_RAWTS, SSBN_SPROM8_RAWTS_RAWTEMP,
	     SSBN_SPROM8_RAWTS_RAWTEMP_SHIFT);
	SPEX(measpower, SSBN_SPROM11_RAWTS, SSBN_SPROM8_RAWTS_MEASPOWER,
	     SSBN_SPROM8_RAWTS_MEASPOWER_SHIFT);
	SPEX(tempsense_slope, SSBN_SPROM11_OPT_CORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMP_SLOPE,
	     SSBN_SPROM8_OPT_CORRX_TEMP_SLOPE_SHIFT);
	SPEX(tempcorrx, SSBN_SPROM11_OPT_CORRX, SSBN_SPROM8_OPT_CORRX_TEMPCORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMPCORRX_SHIFT);
	SPEX(tempsense_option, SSBN_SPROM11_OPT_CORRX,
	     SSBN_SPROM8_OPT_CORRX_TEMP_OPTION,
	     SSBN_SPROM8_OPT_CORRX_TEMP_OPTION_SHIFT);
	SPEX(phycal_tempdelta, SSBN_SPROM11_TEMPDELTA,
	     SSBN_SPROM8_TEMPDELTA_PHYCAL,
	     SSBN_SPROM8_TEMPDELTA_PHYCAL_SHIFT);
	SPEX(temps_period, SSBN_SPROM11_TEMPDELTA, SSBN_SPROM8_TEMPDELTA_PERIOD,
	     SSBN_SPROM8_TEMPDELTA_PERIOD_SHIFT);
	SPEX(temps_hysteresis, SSBN_SPROM11_TEMPDELTA,
	     SSBN_SPROM8_TEMPDELTA_HYSTERESIS,
	     SSBN_SPROM8_TEMPDELTA_HYSTERESIS_SHIFT);

	bus->sprom.antenna_gain_qdb[0] =
		ssbn_sprom_antgain_qdb(sprom[SPOFF(SSBN_SPROM11_AGBG10)] & 0xff);
	bus->sprom.antenna_gain_qdb[1] =
		ssbn_sprom_antgain_qdb(sprom[SPOFF(SSBN_SPROM11_AGBG2A0)] >> 8);
}

/*
 * Indicates the presence of external SPROM.
 */
static bool bcmn_sprom_ext_available(struct bcmn_bus *bus)
{
	u32 chip_status;
	u32 srom_control;
	u32 present_mask;

	if (bus->drv_cc.core->id.rev >= 31) {
		if (!(bus->drv_cc.capabilities & BCMN_CC_CAP_SPROM))
			return false;

		srom_control = bcmn_read32(bus->drv_cc.core,
					   BCMN_CC_SROM_CONTROL);
		return srom_control & BCMN_CC_SROM_CONTROL_PRESENT;
	}

	/* older chipcommon revisions use chip status register */
	chip_status = bcmn_read32(bus->drv_cc.core, BCMN_CC_CHIPSTAT);
	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4313:
		present_mask = BCMN_CC_CHIPST_4313_SPROM_PRESENT;
		break;

	case BCMN_CHIP_ID_BCM4331:
		present_mask = BCMN_CC_CHIPST_4331_SPROM_PRESENT;
		break;

	default:
		return true;
	}

	return chip_status & present_mask;
}

/*
 * Indicates that on-chip OTP memory is present and enabled.
 */
static bool bcmn_sprom_onchip_available(struct bcmn_bus *bus)
{
	u32 chip_status;
	u32 otpsize = 0;
	bool present;

	chip_status = bcmn_read32(bus->drv_cc.core, BCMN_CC_CHIPSTAT);
	switch (bus->chipinfo.id) {
	case BCMN_CHIP_ID_BCM4313:
		present = chip_status & BCMN_CC_CHIPST_4313_OTP_PRESENT;
		break;

	case BCMN_CHIP_ID_BCM4331:
		present = chip_status & BCMN_CC_CHIPST_4331_OTP_PRESENT;
		break;
	case BCMN_CHIP_ID_BCM43142:
	case BCMN_CHIP_ID_BCM43224:
	case BCMN_CHIP_ID_BCM43225:
		/* for these chips OTP is always available */
		present = true;
		break;
	case BCMN_CHIP_ID_BCM43131:
	case BCMN_CHIP_ID_BCM43217:
	case BCMN_CHIP_ID_BCM43227:
	case BCMN_CHIP_ID_BCM43228:
	case BCMN_CHIP_ID_BCM43428:
		present = chip_status & BCMN_CC_CHIPST_43228_OTP_PRESENT;
		break;
	default:
		present = false;
		break;
	}

	if (present) {
		otpsize = bus->drv_cc.capabilities & BCMN_CC_CAP_OTPS;
		otpsize >>= BCMN_CC_CAP_OTPS_SHIFT;
	}

	return otpsize != 0;
}

/*
 * Verify OTP is filled and determine the byte
 * offset where SPROM data is located.
 *
 * On error, returns 0; byte offset otherwise.
 */
static int bcmn_sprom_onchip_offset(struct bcmn_bus *bus)
{
	struct bcmn_device *cc = bus->drv_cc.core;
	u32 offset;

	/* verify OTP status */
	if ((bcmn_read32(cc, BCMN_CC_OTPS) & BCMN_CC_OTPS_GU_PROG_HW) == 0)
		return 0;

	/* obtain bit offset from otplayout register */
	offset = (bcmn_read32(cc, BCMN_CC_OTPL) & BCMN_CC_OTPL_GURGN_OFFSET);
	return BCMN_CC_SPROM + (offset >> 3);
}

int bcmn_sprom_get(struct bcmn_bus *bus)
{
	u16 offset = BCMN_CC_SPROM;
	u16 *sprom;
	static const size_t sprom_sizes[] = {
		SSBN_SPROMSIZE_WORDS_R4,
		SSBN_SPROMSIZE_WORDS_R10,
		SSBN_SPROMSIZE_WORDS_R11,
	};
	int i, err = 0;

	if (!bus->drv_cc.core)
		return -EOPNOTSUPP;

	if (!bcmn_sprom_ext_available(bus)) {
		bool sprom_onchip;

		/*
		 * External SPROM takes precedence so check
		 * on-chip OTP only when no external SPROM
		 * is present.
		 */
		sprom_onchip = bcmn_sprom_onchip_available(bus);
		if (sprom_onchip) {
			/* determine offset */
			offset = bcmn_sprom_onchip_offset(bus);
		}
		if (!offset || !sprom_onchip) {
			/*
			 * Maybe there is no SPROM on the device?
			 * Now we ask the arch code if there is some sprom
			 * available for this device in some other storage.
			 */
			err = bcmn_fill_sprom_with_fallback(bus, &bus->sprom);
			return err;
		}
	}

	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4331 ||
	    bus->chipinfo.id == BCMN_CHIP_ID_BCM43431)
		bcmn_chipco_bcm4331_ext_pa_lines_ctl(&bus->drv_cc, false);

	bcmn_debug(bus, "SPROM offset 0x%x\n", offset);
	for (i = 0; i < ARRAY_SIZE(sprom_sizes); i++) {
		size_t words = sprom_sizes[i];

		sprom = kcalloc(words, sizeof(u16), GFP_KERNEL);
		if (!sprom)
			return -ENOMEM;

		bcmn_sprom_read(bus, offset, sprom, words);
		err = bcmn_sprom_valid(bus, sprom, words);
		if (!err)
			break;

		kfree(sprom);
	}

	if (bus->chipinfo.id == BCMN_CHIP_ID_BCM4331 ||
	    bus->chipinfo.id == BCMN_CHIP_ID_BCM43431)
		bcmn_chipco_bcm4331_ext_pa_lines_ctl(&bus->drv_cc, true);

	if (err) {
		bcmn_warn(bus, "Invalid SPROM read from the PCIe card, trying to use fallback SPROM\n");
		err = bcmn_fill_sprom_with_fallback(bus, &bus->sprom);
	} else {
		if (bus->sprom.revision == 11)
			bcmn_sprom_extract_r11(bus, sprom);
		else
			bcmn_sprom_extract_r8(bus, sprom);
		kfree(sprom);
		/*
		 * The board's NVRAM may say more about this device than its
		 * SPROM has words for. Not having NVRAM here is the norm.
		 */
		bcm47xx_sprom_fill_bcma_nvram(bus, &bus->sprom);
	}

	return err;
}
