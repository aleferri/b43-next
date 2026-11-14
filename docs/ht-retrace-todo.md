# HT-PHY retrace: what is left

Against the R6400 v1's captures (BCM4331, wl 7.14.164.18), with
`test/phy_ht`: `gates.sh` on the cold ones, `hot.sh` on the hot ones; the
scores are in `test/phy_ht/README.md`. Operation numbers here are from
`cold-2g6-20`.

## The core

`test/integration/r6400.sh` runs b43's core with the HT-PHY against the
cold captures, from the probe to the BSS's start; `test/phy_ht` compiles no
core but the watchdog turn's calls into it. From `up` to `beaconing`,
2026-11-13:

| capture | grezzo | PHY+radio+MAC |
|---|---|---|
| cold-2g6-20 | 65.06% | 96.51% |

The second score has the PHY's and the radio's operations, MACCTL and the
core's I/O control with its forced gated clock. wl's power saving while no
BSS is up is not scored (`drop_power_save()` in `test/unit/compare.py`).
`grezzo` follows the traffic, 991 interrupts on ch 1 against 217 on ch 6:
the rest is the core's attach, init, interrupts and RX, not divided by phase
yet.

What is left of the PHY's, the radio's and the MAC's operations on
`cold-2g6-20`, in operations of the larger side:

| ops | what | needs |
|---|---|---|
| 565 | the temperature turns: wl measures at its timer's ticks, b43 at its own, which the harness does not place | the harness |
| 88 | the BSS start: wl's core down and up, its resets and the radio's power | a decision on a core reset at the BSS start |
| 71 | the probe and the core's init: b43's attach, which the capture has not, and its first reset with G mode in the I/O control, where wl's has it after | an attach capture |

MACCTL is written as wl writes it on the HT-PHY's core (no G mode, which
the I/O control carries; `DISCPMQ` cleared on an AP), and the MAC init with
the MAC-PHY clock comes before the PHY, as wl's.

Of the MAC's operations b43's are wl's but for:

- the core resets: on `cold-2g6-20`, one channel throughout (the shared
  memory's channel, 0x00a0, is 6 at every write), wl's I/O control goes
  through a reset (0x07, 0x05, 0x4d, 0x43, 0x45) four times in the first
  17 ms of `up` (#7-#219) and three in the first 9 ms of `ssid`
  (#20300-#20451). The four between them, every 2 s while no BSS is up,
  are its power saving, not scored. b43 resets three times in the whole
  run, all before its PHY work: twice at the probe, once at the core's
  init, and not at the BSS's start;
- the MAC suspend: wl reads MACINTSTATUS before it writes MACCTL, b43 after;
- the RX ring's index, whose value is b43's ring's, not wl's.

## The hot cycle

wl initialises the PHY once at the first `wl up` after the module is
loaded; after a `wl down; wl up` the init comes with the BSS, a later init
followed by the calibrations. As for the AC ([`ac-phy.md`](ac-phy.md),
[`retrace-todo.md`](retrace-todo.md), "`do_full_init` after a down"), b43
defers this: `b43_phy_exit()` sets `do_full_init` on every down, so a down
and up takes the first init, and only `bss_start` runs the later one.
`test/phy_ht/hot.sh` scores that later init on every hot window.

On a later init on another channel than the last init's wl restores its
stored gain state (`b43_phy_ht_gain_state_write()`) twice, writes the clip
thresholds, restores it twice more, and does not write 0x280 and 0x283 in
the gain control setup; on the same channel it does as at a first init.
Erik's `attach-2x-ch11` capture of the R6400 v1 (2026-11-14, not in the
traces repository yet) decides it: its second cycle, `wl down; wl up` on
channel 11 again, has 3 writes of table 7's address at 0x106 and 4 of 0x280
and 0x283 in its `ssid`, as the first cycle and the cold captures, where
every hot window, which changes channel, has 7 and 14 to 18. It is wl's
`curr_home_channel` (brcmsmac's `phy_int.h`), not the down. b43 keeps the
channel and type of the last init across a down and restores on another.

## The watchdog's 0x280 and 0x283

0x280 and 0x283 are, at the N-PHY's addresses, `CRSMINPOWERL0` and
`CRSMINPOWERU0` (b43's `phy_n.h`, brcmsmac's `phy_n.c`): the CRS minimum
power of the lower and the upper sideband. b43 keeps them as wl's watchdog
does in the captures (`b43_phy_ht_crsmin_update()`), by a rule inferred
from them:

- a turn's count is the OFDM glitches and bad PLCPs of the four macstat
  counters wl reads, d(0x010e) - d(0x0158) + d(0x010c) - d(0x015e), since
  the turn before or, after the counters were reset, since then;
- two turns in a row with a count of 376 or more raise an index by 16,
  every further one again;
- otherwise, two turns after the last write the index goes down by 1, not
  below 0: the 0x3a every second turn of a quiet channel;
- an init writes 0x3a and sets the index to 0, starts the two turns again
  and keeps the turns over the threshold, across a down too;
- the value written is floor(58 + 11 i / 16).

| shm | counter (brcmsmac's `struct macstat`) |
|---|---|
| 0x010c | `rxbadplcp` |
| 0x010e | `rxcrsglitch` |
| 0x0158 | `bphy_rxcrsglitch` |
| 0x015e | `bphy_badplcp` |

On the captures the threshold lies between 336 and 376; b43 takes 376, the
lowest count they show over it. The index stops at 56, the N-PHY's
`NPHY_NOISE_CRSMINPWR_ARRAY_MAX_INDEX_REV_7` in brcmsmac, which no capture
reaches. Turn by turn, `test/phy_ht` writes wl's values on all 76 turns of
the cold windows and on 99 of the 125 of the hot ones; `hot.sh` hands each
window the last two turns before it.

Open: on `hot-2g-20` the last three turns of ch 6, which no other count or
threshold of the four counters gives either (wl goes down to 0x4f where the
rule raises to 0x5b), and at 40 MHz, in standby, 6l, 9u and 11u, where wl
writes 0x45 at the second turn and 0x44 two later after turns with a handful
of counts, and 2l, 4l, 5l, 7u and 8u.