/*
 * decode_r10.c — what SROM rev 10 adds to rev 9, through the tree's
 * bcmn_sprom_extract_r8(), as decode_r8 and decode_r9 print the rest.
 *
 *   decode_r10 SROM_DUMP
 */
#include "data_load.h"
#include "ssbn_sprom.h"

#include <stdio.h>
#include <string.h>

extern void run_extract_r8(struct bcmn_bus *bus, const u16 *sprom);

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
	if (bus.sprom.revision != 10) {
		fprintf(stderr, "%s: SROM rev %d, not 10\n", argv[1],
			bus.sprom.revision);
		return 1;
	}
	run_extract_r8(&bus, srom);

	printf("cckPwrOffset=0x%x\n", s->cckpwroffset);
	printf("swctrlmap_2g=0x%x,0x%x,0x%x,0x%x,0x%x\n", s->swctrlmap_2g[0],
	       s->swctrlmap_2g[1], s->swctrlmap_2g[2], s->swctrlmap_2g[3],
	       s->swctrlmap_2g[4]);
	return 0;
}
