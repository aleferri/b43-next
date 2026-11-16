# test/txhdr — the TX headers of xmit.c

Compiles `b43-next/xmit.c` as it is against the kernel headers (6.18 or
later, as `../integration`), with `kstubs.c` in place of what it calls in
mac80211, cfg80211 and the rest of b43, and runs `b43_generate_txhdr()` and
the A-MPDU limits on the devices, rates and frames of `cases.c`: legacy,
N-PHY with and without HT, HT-PHY, AC at 2.4 GHz and at 80 MHz, every
legacy rate, HT MCS 0-31, VHT, protection, keys, A-MPDU members.

```sh
make check          # against expected.txt
make dump           # every header in full, to diff two trees
make expected       # expected.txt from this tree
```

A line is the case, the return, the header's FNV-1a digest (whole with
`dump`), the Duration left in the frame and the rates left in the TX info.
The stand-ins return values made of their arguments, so a header that asks
mac80211 for another duration or bitrate changes too. A change of
`xmit.c` that should keep the headers keeps `make check` quiet; one that
changes them changes `expected.txt` in the same commit.
