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
| cold-2g1-20 | 46.76% | 93.92% |
| cold-2g6-20 | 64.12% | 93.88% |
| cold-2g11-20 | 53.13% | 93.92% |

The second score has the PHY's and the radio's operations, MACCTL and the
core's I/O control with its forced gated clock. wl's power saving while no
BSS is up is not scored (`drop_power_save()` in `test/unit/compare.py`).
`grezzo` follows the traffic, 991 interrupts on ch 1 against 217 on ch 6:
the rest is the core's attach, init, interrupts and RX, not divided by phase
yet.

Of the MAC's operations b43's are wl's but for:

- the core resets: on `cold-2g6-20`, one channel throughout (the shared
  memory's channel, 0x00a0, is 6 at every write), wl's I/O control goes
  through a reset (0x07, 0x05, 0x4d, 0x43, 0x45) four times in the first
  17 ms of `up` (#7-#219) and three in the first 9 ms of `ssid`
  (#20300-#20451). The four between them, every 2 s while no BSS is up,
  are its power saving, not scored. b43 resets
  three times in the whole run, all before its PHY work: twice at the probe,
  once at the core's init, and not at the BSS's start;
- MACCTL's bit 31 (`B43_MACCTL_GMODE`): wl reads it set and never writes
  it, 0x04160403 against 0x84160403 read. brcmsmac sets G mode through the
  core's I/O control (`SICF_GMODE`) and only reads `MCTL_GMODE` back after
  a core reset; b43 writes it at the core reset and the chip init, and its
  read-modify-writes keep it;
- MACCTL's `DISCPMQ`: wl sets it through the init and clears it when the
  AP starts (0x44020403, then 0x04160403); b43 keeps it, as it implements
  no power management queue;
- the MAC suspend: wl reads MACINTSTATUS before it writes MACCTL, b43 after;
- the RX ring's index, whose value is b43's ring's, not wl's.

## The hot cycle

wl initialises the PHY once at the first `wl up` after the module is
loaded; after a `wl down; wl up` the init comes with the BSS, a later init
followed by the calibrations. As for the AC ([`ac-phy.md`](ac-phy.md),
[`retrace-todo.md`](retrace-todo.md), "`do_full_init` after a down"), b43
defers this: `b43_phy_exit()` sets `do_full_init` on every down, so a down
and up takes the first init, and only `bss_start` runs the later one.
`test/phy_ht/hot.sh` scores that later init on every hot window. Open is
the gain state: at the later init of the hot captures wl restores it
(`b43_phy_ht_gain_state_write()`) twice, writes the clip thresholds,
restores it twice more, after the table 0x1c writes, and does not write
0x280 and 0x283 in the gain control setup; at the cold `ssid` it does the
opposite, and b43 does as there. In these captures the channel changes
whenever wl was down before, so which of the two decides is open.

What it restores is what wl's interference state keeps as stored defaults
(`struct interference_info`, left in brcmsmac's `phy_int.h`): the CRS
minimum power, 0x280 and 0x283, and the initial gain's RF sequence, table
7 (the N-PHY's RFSEQ) at 0x106, where brcmsmac writes `rfseq_init_gain`.
The same structure has `curr_home_channel`, which points to a reset on a
change of channel rather than on a down: an inference. A hot cycle on one
channel decides it, the R6400's `hot.sh` with the same chanspec repeated
(`hot.sh hot-2g-6 6 6 6`): four restores there mean the down, none the
channel.

## The watchdog's 0x280 and 0x283

0x280 and 0x283 are, at the N-PHY's addresses, `CRSMINPOWERL0` and
`CRSMINPOWERU0` (b43's `phy_n.h`, brcmsmac's `phy_n.c`): the CRS minimum
power of the lower and the upper sideband. The low bytes the watchdog
writes are not a constant: on the cold ch 6 and 11 they stay 0x3a, on 1l
wl writes 0x45, 0x44, 0x4f and 0x4e, in the hot captures up to 0x5b (ch 3
and 5 at 20 MHz). b43 writes 0x3a on every second turn; the cold 1l start
of the BSS shows the difference (99.88%), and so do the hot windows where
wl raises them.

In every turn wl reads four shared memory counters and the 0x06c0-0x06e2
block. By brcmsmac's `struct macstat` (`d11.h`, from `M_UCODE_MACSTAT`,
0xe0) the four are:

| shm | counter |
|---|---|
| 0x010c | `rxbadplcp` |
| 0x010e | `rxcrsglitch` |
| 0x0158 | `bphy_rxcrsglitch` |
| 0x015e | `bphy_badplcp` |

A turn writes 0x280 and 0x283 before it reads them, so the value follows
from the counts of the turns before. A rule that gives every turn wl
writes on and every value in the six cold captures (76 turns) and in 52
of the 55 turns of `hot-2g-20`:

- a turn's count is the OFDM glitches and bad PLCPs, d(0x010e) -
  d(0x0158) + d(0x010c) - d(0x015e), the counts since the reset after a
  core reset;
- two turns in a row over a threshold raise an index by 16, every further
  one again;
- otherwise, two turns after the last write the index goes down by 1, not
  below 0: the 0x3a every second turn of a quiet channel;
- an init writes 0x3a and sets the index to 0, starts the two turns again
  and keeps the turns over the threshold;
- the value written is floor(58 + 11 i / 16): 0x3a at 0, 0x43-0x45 at
  14-16, 0x4e-0x50 at 30-32, 0x5b at 48, the values the captures have.

The threshold lies between 336 and 376: every count of 376 or more is over
it, every count of 336 or less is not. It is an inference from these
captures, not wl's code. brcmsmac keeps the N-PHY's names but not their
code: `crsminpwr_index` in `struct interference_info` (`phy_int.h`),
`NPHY_NOISE_NOASSOC_CRSIDX_INCR` 16 (8 with a station associated), up to
`NPHY_NOISE_CRSMINPWR_ARRAY_MAX_INDEX_REV_7` 56, and 400 to enter on
glitches and bad PLCPs (`phy_n.c`), which does not fit 1l, where 0x45
follows a turn of 391. The structure also keeps a moving average of the
bad PLCPs over 8 turns, which the rule does not need. Counting every read
of the four counters, the init's and the BSS start's too, instead of the
turns' fits worse: 129 of 201 turns against 174.

`hot-2g-20` misses the last three turns of ch 6. On `hot-2g-40` the rule
gives 46 of 70 turns: on 6l, 9u and 11u wl writes 0x45 at the second turn
and 0x44 two later, at the start of the turn as always, after turns with a
handful of counts, and 2l, 4l, 5l, 7u and 8u depart from it too. Something
else raises the value at 40 MHz, or the rule is another. b43 reads the four
counters on every turn and the harness serves them; the rule needs a capture
with an interferer switched on and off on purpose.
