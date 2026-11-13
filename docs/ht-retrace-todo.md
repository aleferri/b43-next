# HT-PHY retrace: what is left

The gate is `test/phy_ht/gates.sh --min-block 8 --gap-tol 2`, against the
R6400 v1's cold captures (BCM4331, wl 7.14.164.18). Numbers here are from
`cold-2g6-20`, window #173-#8731.

## Where the operations are

`test/phy_ht/gates.sh` scores `up`, from wl's first PHY operation after the
`up` mark to the `ssid` mark: on `cold-2g6-20`, 8559 of wl's operations, of
which 5376 are the PHY's and the radio's. `ssid` (26827) and `bss-up`
(4303) are what wl does when the BSS starts, b43's start of the AP, not
part of its up.

## `up`

Of the operations outside the PHY and the radio, the larger unmatched
runs are the core's, which this harness does not compile:

| ops | what |
|---|---|
| #173-#697 | the core before the PHY: MMIO writes, shared memory, the ucode's bulk writes |
| #962-#992 | wrapper and chipcommon |
| #5738-#8731 | after the TX power control: MMIO reads, shared memory |

In order, PHY and radio only, b43's operations of the cold ch 6 `up`
are one block, 5360 of wl's 5376 (99.70%), no value apart. wl's 1 s
watchdog rewrites 0x280/0x283 on every second turn, as b43's 1 s work now
does. What is left, 16 operations, is the watchdog's third turn after
`wl up`: a read of table 13 entry 0x5f (#8520) and, after a core reset, the
analog switch on, the radio off (0x810) and the analog switch off
(#8590-#8613), before the core goes into reset: wl's power saving while no
BSS is up, which b43 does not have.

The PHY's calls into the core (`b43_mac_suspend()`, the shared memory,
`b43_mac_switch_freq()`, the PLL) emit nothing in the harness: on the AC
`test/unit/main.c` mirrors them, here they are only counted.

## `ssid`

b43 runs what wl runs when the BSS starts (the HT-PHY's `bss_start` op;
flow `ap_start` in `test/phy_ht`): the later init, the TX power set twice,
a calibration pass, the TX power again and a second pass, then the 1 s
watchdog's work, temperature measurement included. Of wl's PHY and radio
operations from `ssid` to `beaconing` only the fourteen of wl's core up,
after the power saving of `up` (the analog switches and the radio's
power), are missing.

wl initialises the PHY for the channel again, without the radio init of
`up`, then calibrates:

- the gain tables (0x00, 0x01, 0x28, 0x02, 0x03, 0x29), tables 0x08, 0x25,
  0x04, the power fix (table 0x1a 0xe8, 0x07, 0x0d) three times;
- the TX power control init, the chain `test/phy_ht/check.sh` passes, four
  times (#10982, #12770, #13664, #25138);
- two passes of a tone calibration, #15344-#23859 and #26788-#35294, each 12
  tone loads into table 0x11 with their playback and measurement.

The tones are exactly those of the N-PHY's `b43_nphy_gen_load_samples()`:
160 samples at 20 MHz, 2500 kHz at amplitude 250 and 2000 kHz at 181, all
26 loads of the segment.

## `bss-up`

Mostly the core: 2117 MMIO reads, 976 writes, 614 shared memory accesses;
546 PHY accesses.

## The hot cycle

The hot capture (`hot-2g-20`, `wl down; chanspec; wl up; ssid; bss up` per
channel) shows where wl initialises the PHY after the first time:

| segment | wl's PHY and radio operations |
|---|---|
| `down-N` | 265 |
| `up-N` | 0 |
| `ssid-N` | 20003, or 20547 on the turns with the temperature measurement |

`wl up` does not touch the PHY after the first up since the module was
loaded; the init comes with the BSS, a later init (no tables, no radio
calibrations) followed by the calibrations, as at the cold `ssid`.

As for the AC ([`ac-phy.md`](ac-phy.md), [`retrace-todo.md`](retrace-todo.md),
"`do_full_init` after a down"), the cold bring-up comes first and the hot
one is deferred, not dropped: `b43_phy_exit()` sets `do_full_init` on every
down, so a down and up takes the first init, and only the start of the BSS
(`bss_start`), which does not go through `b43_phy_exit()`, runs the later
one. `test/phy_ht/hot.sh` scores that later init on the 25 `ssid-N`
windows of `hot-2g-20` and `hot-2g-40`, 99.38-99.67% of the PHY's and the
radio's operations (`test/phy_ht/README.md`). Where hot differs from cold:

- The power fix. wl fixes the TX gain at the last index it fixed: 40 until
  the RX calibration fixes 10, then 10, after a down and up too. b43 keeps
  the index as well (`tx_gain_idx`), from 40 at every core init.
- The gain state. At the later init wl restores it
  (`b43_phy_ht_gain_state_write()`) twice, writes the clip thresholds,
  restores it twice more, after the table 0x1c writes, and does not write
  0x280 and 0x283 in the gain control setup; at the cold `ssid` it does
  the opposite, and b43 does as there. In these captures the channel
  changes whenever wl was down before, so which of the two decides is open.
- The target. wl's later init runs the TX power control with the target of
  the configuration before and sets the new channel's at the first TX
  power set after it; the harness gives b43 the one before, which its sets
  keep (30 operations on ch 1 and 11 and the channel after 1). b43
  computes the target on its TX power checks (a new power level, a TX
  status, rate limited, the 60 s work), not on the channel change.

## 40 MHz

b43's HT-PHY runs 40 MHz as wl does on the R6400's cold 40 MHz captures:
the core's width at the PHY init (`b43_phy_ht_width()`), the 2059 on the
centre channel, the sideband, two table 0x25 pairs, the AFE setup, table
7's copies and the calibrations' tones and constants. `gates.sh` scores the
three 40 MHz cold captures as the 20 MHz ones; the up's PHY and radio
operations are 99.70% of wl's, the start of the BSS's 99.95% on 6l and 11u.

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
- two turns in a row over a threshold raise the value by 11, every further
  one again;
- otherwise, two turns after the last write the value goes down by 1, not
  below 0x3a: the 0x3a every second turn of a quiet channel;
- an init writes 0x3a and starts the two turns again, and keeps the turns
  over the threshold.

The threshold lies between 336 and 376: every count of 376 or more is over
it, every count of 336 or less is not. It is an inference from these
captures, not wl's code: brcmsmac has the N-PHY's constants (`NPHY_NOISE_*`
in `phy_n.c`, 400 to enter on glitches and bad PLCPs) but not their code,
and 400 does not fit 1l, where 0x45 follows a turn of 391. `hot-2g-20`
misses the last three turns of ch 6. On `hot-2g-40` the rule gives 46 of 70
turns: on 6l, 9u and 11u wl writes 0x45 at the second turn and 0x44 two
later with a handful of counts, and 2l, 4l, 5l, 7u and 8u depart from it
too; at 40 MHz something else raises the value, or the rule is another. A
port needs the harness to serve the counters and a capture that moves the
glitch rate on purpose.
