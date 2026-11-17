/*
 * decode_r8.c — a rev 8 or 9 SROM through the tree's bcmn_sprom_extract_r8(),
 * printed as an nvram, one name=value a line, under the names of the stock
 * driver's nvram: what bcma hands b43 on a board without one, for the PHY
 * harnesses that take their board values from an nvram.
 *
 *   decode_r8 SROM_DUMP
 */
#include "data_load.h"
#include "ssbn_sprom.h"

#include <stdio.h>
#include <string.h>

extern void run_extract_r8(struct bcmn_bus *bus, const u16 *sprom);

static void kv(const char *name, unsigned long v)
{
	printf("%s=0x%lx\n", name, v);
}

static void kvn(const char *fmt, int i, unsigned long v)
{
	char name[32];

	snprintf(name, sizeof(name), fmt, i);
	kv(name, v);
}

int main(int argc, char **argv)
{
	static u16 srom[SROM_MAX_WORDS];
	struct bcmn_bus bus;
	const struct ssbn_sprom *s = &bus.sprom;
	int n, i, k;

	if (argc != 2) {
		fprintf(stderr, "usage: %s SROM_DUMP\n", argv[0]);
		return 2;
	}
	n = srom_load(argv[1], srom, SROM_MAX_WORDS);
	if (n <= 0)
		return 2;
	memset(&bus, 0, sizeof(bus));
	/* bcmn_sprom_valid(): the revision is the last word's low byte */
	bus.sprom.revision = srom[n - 1] & 0xff;
	if (bus.sprom.revision < 8 || bus.sprom.revision > 10) {
		fprintf(stderr, "%s: SROM rev %d, not 8-10\n", argv[1],
			bus.sprom.revision);
		return 1;
	}
	run_extract_r8(&bus, srom);

	kv("sromrev", s->revision);
	printf("macaddr=%02x:%02x:%02x:%02x:%02x:%02x\n", s->il0mac[0],
	       s->il0mac[1], s->il0mac[2], s->il0mac[3], s->il0mac[4],
	       s->il0mac[5]);
	kv("boardrev", s->board_rev);
	kv("boardtype", s->board_type);
	kv("boardflags", (unsigned long)s->boardflags_hi << 16 | s->boardflags_lo);
	kv("boardflags2",
	   (unsigned long)s->boardflags2_hi << 16 | s->boardflags2_lo);
	printf("ccode=%.2s\n", s->alpha2[0] ? s->alpha2 : "");
	kv("aa2g", s->ant_available_bg);
	kv("aa5g", s->ant_available_a);
	kv("ag0", (u8)s->antenna_gain.a0);
	kv("ag1", (u8)s->antenna_gain.a1);
	kv("ag2", (u8)s->antenna_gain.a2);
	kv("ag3", (u8)s->antenna_gain.a3);
	kv("txchain", s->txchain);
	kv("rxchain", s->rxchain);
	kv("antswitch", s->antswitch);
	kv("leddc", s->leddc_on_time << 8 | s->leddc_off_time);
	kv("ledbh0", s->gpio0);
	kv("ledbh1", s->gpio1);
	kv("ledbh2", s->gpio2);
	kv("ledbh3", s->gpio3);

	kv("pa0maxpwr", s->maxpwr_bg);
	kv("pa0itssit", s->itssi_bg);
	kv("pa0b0", s->pa0b0);
	kv("pa0b1", s->pa0b1);
	kv("pa0b2", s->pa0b2);
	kv("pa1maxpwr", s->maxpwr_a);
	kv("pa1itssit", s->itssi_a);
	kv("pa1lomaxpwr", s->maxpwr_al);
	kv("pa1himaxpwr", s->maxpwr_ah);
	kv("pa1b0", s->pa1b0);
	kv("pa1b1", s->pa1b1);
	kv("pa1b2", s->pa1b2);
	kv("pa1lob0", s->pa1lob0);
	kv("pa1lob1", s->pa1lob1);
	kv("pa1lob2", s->pa1lob2);
	kv("pa1hib0", s->pa1hib0);
	kv("pa1hib1", s->pa1hib1);
	kv("pa1hib2", s->pa1hib2);
	kv("opo", s->opo);

	for (i = 0; i < 4; i++) {
		kvn("txpid2ga%d", i, s->txpid2g[i]);
		kvn("txpid5gla%d", i, s->txpid5gl[i]);
		kvn("txpid5ga%d", i, s->txpid5g[i]);
		kvn("txpid5gha%d", i, s->txpid5gh[i]);
	}
	for (i = 0; i < 4; i++) {
		const struct ssbn_sprom_core_pwr_info *c = &s->core_pwr_info[i];

		kvn("maxp2ga%d", i, c->maxpwr_2g);
		kvn("itt2ga%d", i, c->itssi_2g);
		kvn("maxp5ga%d", i, c->maxpwr_5g);
		kvn("maxp5gla%d", i, c->maxpwr_5gl);
		kvn("maxp5gha%d", i, c->maxpwr_5gh);
		kvn("itt5ga%d", i, c->itssi_5g);
		for (k = 0; k < 3; k++) {
			char f[24];

			snprintf(f, sizeof(f), "pa2gw%da%%d", k);
			kvn(f, i, c->pa_2g[k]);
			snprintf(f, sizeof(f), "pa5glw%da%%d", k);
			kvn(f, i, c->pa_5gl[k]);
			snprintf(f, sizeof(f), "pa5gw%da%%d", k);
			kvn(f, i, c->pa_5g[k]);
			snprintf(f, sizeof(f), "pa5ghw%da%%d", k);
			kvn(f, i, c->pa_5gh[k]);
		}
	}

	kv("tssipos2g", s->fem.ghz2.tssipos);
	kv("extpagain2g", s->fem.ghz2.extpa_gain);
	kv("pdetrange2g", s->fem.ghz2.pdet_range);
	kv("triso2g", s->fem.ghz2.tr_iso);
	kv("antswctl2g", s->fem.ghz2.antswlut);
	kv("tssipos5g", s->fem.ghz5.tssipos);
	kv("extpagain5g", s->fem.ghz5.extpa_gain);
	kv("pdetrange5g", s->fem.ghz5.pdet_range);
	kv("triso5g", s->fem.ghz5.tr_iso);
	kv("antswctl5g", s->fem.ghz5.antswlut);

	kv("tri2g", s->tri2g);
	kv("tri5g", s->tri5g);
	kv("tri5gl", s->tri5gl);
	kv("tri5gh", s->tri5gh);
	kv("rxpo2g", (u8)s->rxpo2g);
	kv("rxpo5g", (u8)s->rxpo5g);
	kv("rssismf2g", s->rssismf2g);
	kv("rssismc2g", s->rssismc2g);
	kv("rssisav2g", s->rssisav2g);
	kv("bxa2g", s->bxa2g);
	kv("rssismf5g", s->rssismf5g);
	kv("rssismc5g", s->rssismc5g);
	kv("rssisav5g", s->rssisav5g);
	kv("bxa5g", s->bxa5g);

	kv("rawtempsense", s->rawtempsense);
	kv("measpower", s->measpower);
	kv("tempsense_slope", s->tempsense_slope);
	kv("tempcorrx", s->tempcorrx);
	kv("tempsense_option", s->tempsense_option);
	kv("freqoffset_corr", s->freqoffset_corr);
	kv("iqcal_swp_dis", s->iqcal_swp_dis);
	kv("hw_iqcal_en", s->hw_iqcal_en);
	kv("tempthresh", s->tempthresh);
	kv("tempoffset", s->tempoffset);
	kv("phycal_tempdelta", s->phycal_tempdelta);
	kv("temps_period", s->temps_period);
	kv("temps_hysteresis", s->temps_hysteresis);

	if (s->revision == 8) {
		kv("cck2gpo", s->cck2gpo);
		kv("ofdm2gpo", s->ofdm2gpo);
		kv("ofdm5glpo", s->ofdm5glpo);
		kv("ofdm5gpo", s->ofdm5gpo);
		kv("ofdm5ghpo", s->ofdm5ghpo);
		for (i = 0; i < 8; i++) {
			kvn("mcs2gpo%d", i, s->mcs2gpo[i]);
			kvn("mcs5gpo%d", i, s->mcs5gpo[i]);
			kvn("mcs5glpo%d", i, s->mcs5glpo[i]);
			kvn("mcs5ghpo%d", i, s->mcs5ghpo[i]);
		}
		kv("cddpo", s->cddpo);
		kv("stbcpo", s->stbcpo);
		kv("bw40po", s->bw40po);
		kv("bwduppo", s->bwduppo);
	}
	return 0;
}
