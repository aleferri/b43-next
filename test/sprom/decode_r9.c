/*
 * decode_r9.c — what SROM rev 9 adds to rev 8, the power per rate that
 * replaced rev 8's power offsets, through the tree's
 * bcmn_sprom_extract_r8(), as decode_r8 prints the rest. Rev 10 keeps it.
 *
 *   decode_r9 SROM_DUMP
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

int main(int argc, char **argv)
{
	static u16 srom[SROM_MAX_WORDS];
	struct bcmn_bus bus;
	const struct ssbn_sprom *s = &bus.sprom;
	int n;

	if (argc != 2) {
		fprintf(stderr, "usage: %s SROM_DUMP\n", argv[0]);
		return 2;
	}
	n = srom_load(argv[1], srom, SROM_MAX_WORDS);
	if (n <= 0)
		return 2;
	memset(&bus, 0, sizeof(bus));
	bus.sprom.revision = srom[n - 1] & 0xff;
	if (bus.sprom.revision < 9 || bus.sprom.revision > 10) {
		fprintf(stderr, "%s: SROM rev %d, not 9-10\n", argv[1],
			bus.sprom.revision);
		return 1;
	}
	run_extract_r8(&bus, srom);

	kv("cckbw202gpo", s->cckbw202gpo);
	kv("cckbw20ul2gpo", s->cckbw20ul2gpo);
	kv("legofdmbw202gpo", s->legofdmbw202gpo);
	kv("legofdmbw20ul2gpo", s->legofdmbw20ul2gpo);
	kv("legofdmbw205glpo", s->legofdmbw205glpo);
	kv("legofdmbw20ul5glpo", s->legofdmbw20ul5glpo);
	kv("legofdmbw205gmpo", s->legofdmbw205gmpo);
	kv("legofdmbw20ul5gmpo", s->legofdmbw20ul5gmpo);
	kv("legofdmbw205ghpo", s->legofdmbw205ghpo);
	kv("legofdmbw20ul5ghpo", s->legofdmbw20ul5ghpo);
	kv("mcsbw202gpo", s->mcsbw202gpo);
	kv("mcsbw20ul2gpo", s->mcsbw20ul2gpo);
	kv("mcsbw402gpo", s->mcsbw402gpo);
	kv("mcsbw205glpo", s->mcsbw205glpo);
	kv("mcsbw20ul5glpo", s->mcsbw20ul5glpo);
	kv("mcsbw405glpo", s->mcsbw405glpo);
	kv("mcsbw205gmpo", s->mcsbw205gmpo);
	kv("mcsbw20ul5gmpo", s->mcsbw20ul5gmpo);
	kv("mcsbw405gmpo", s->mcsbw405gmpo);
	kv("mcsbw205ghpo", s->mcsbw205ghpo);
	kv("mcsbw20ul5ghpo", s->mcsbw20ul5ghpo);
	kv("mcsbw405ghpo", s->mcsbw405ghpo);
	kv("mcs32po", s->mcs32po);
	kv("legofdm40duppo", s->legofdm40duppo);
	return 0;
}
