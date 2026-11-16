// SPDX-License-Identifier: GPL-2.0
/*
 * The user side: prints what cases.c hands over, one line per case. A
 * header is printed whole with TXHDR_FULL=1, otherwise as a 64-bit FNV-1a
 * digest, which keeps expected.txt short; the full lines of two builds
 * show which bytes moved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void txhdr_run(void);

static int full;

static unsigned long long fnv(unsigned long long h, const unsigned char *p,
			      unsigned int len)
{
	while (len--) {
		h ^= *p++;
		h *= 0x100000001b3ULL;
	}
	return h;
}

static void hex(const unsigned char *p, unsigned int len)
{
	while (len--)
		printf("%02x", *p++);
}

void txhdr_emit(const char *name, int ret, const unsigned char *hdr,
		unsigned int len, const unsigned char *tail,
		unsigned int tail_len)
{
	printf("%s: %d ", name, ret);
	if (full) {
		hex(hdr, len);
		putchar(' ');
	} else {
		printf("%016llx ", fnv(0xcbf29ce484222325ULL, hdr, len));
	}
	hex(tail, tail_len);
	putchar('\n');
}

void txhdr_value(const char *name, unsigned int value)
{
	printf("%s: %u\n", name, value);
}

void txhdr_text(const char *name, const char *text)
{
	printf("%s: %s\n", name, text);
}

/* Reached from the header formats' other members, never from a header. */
#define UNEXPECTED(sym) \
	void sym(void) { fprintf(stderr, "unexpected: " #sym "\n"); abort(); }
UNEXPECTED(b43_ampdu_txstatus)
UNEXPECTED(b43_dma_handle_txstatus)
UNEXPECTED(b43_dma_tx_resume)
UNEXPECTED(b43_dma_tx_suspend)
UNEXPECTED(b43_macstat_update)
UNEXPECTED(b43_phy_txpower_check)
UNEXPECTED(b43_pio_handle_txstatus)
UNEXPECTED(b43_pio_tx_resume)
UNEXPECTED(b43_pio_tx_suspend)
UNEXPECTED(b43_ps_rx)
UNEXPECTED(b43_tsf_read)
UNEXPECTED(b43_txstatus_drain_ac)
UNEXPECTED(b43_txstatus_drain_fw4)
UNEXPECTED(b43_txstatus_poll_ac)
UNEXPECTED(b43_txstatus_poll_fw4)
UNEXPECTED(b43dbg)
UNEXPECTED(b43warn)
UNEXPECTED(dev_kfree_skb_any_reason)
UNEXPECTED(ieee80211_channel_to_freq_khz)
UNEXPECTED(ktime_get)
UNEXPECTED(pskb_expand_head)
UNEXPECTED(skb_pull)
UNEXPECTED(skb_put)
UNEXPECTED(skb_queue_tail)

int main(void)
{
	full = getenv("TXHDR_FULL") && *getenv("TXHDR_FULL") == '1';
	txhdr_run();
	return 0;
}
