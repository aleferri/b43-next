# HT-PHY retrace: what is left

Against the R6400 v1's captures (BCM4331, wl 7.14.164.18), with
`test/phy_ht`: `gates.sh` on the cold ones, `hot.sh` on the hot ones; the
scores are in `test/phy_ht/README.md`. Operation numbers here are from
`cold-2g6-20`.

## The core

The harness compiles no core, so the `grezzo` line of `gates.sh` stays
near 62%. The larger unmatched runs of the `up` window are the core's:

| ops | what |
|---|---|
| #173-#697 | the core before the PHY: MMIO writes, shared memory, the ucode's bulk writes |
| #962-#992 | wrapper and chipcommon |
| #5738-#8731 | after the TX power control: MMIO reads, shared memory |

`bss-up` is mostly the core too: 2117 MMIO reads, 976 writes, 614 shared
memory accesses. The PHY's calls into the core (`b43_mac_suspend()`, the
shared memory, `b43_mac_switch_freq()`, the PLL) emit nothing: on the AC
`test/unit/main.c` mirrors them, here they are only counted.

## The hot cycle

wl initialises the PHY once at the first `wl up` after the module is
loaded; after a `wl down; wl up` the init comes with the BSS, a later init
followed by the calibrations. As for the AC ([`ac-phy.md`](ac-phy.md),
[`retrace-todo.md`](retrace-todo.md), "`do_full_init` after a down"), b43
defers this: `b43_phy_exit()` sets `do_full_init` on every down, so a down
and up takes the first init, and only `bss_start` runs the later one.
`test/phy_ht/hot.sh` scores that later init on every hot window. Open:

- The gain state. At the later init of the hot captures wl restores it
  (`b43_phy_ht_gain_state_write()`) twice, writes the clip thresholds,
  restores it twice more, after the table 0x1c writes, and does not write
  0x280 and 0x283 in the gain control setup; at the cold `ssid` it does
  the opposite, and b43 does as there. In these captures the channel
  changes whenever wl was down before, so which of the two decides is open.
- The target. wl's later init runs the TX power control with the target of
  the configuration before and sets the new channel's at the first TX
  power set after it. b43 computes the target on its TX power checks (a
  new power level, a TX status at most every 2 s, the 60 s work), not on
  the channel change: on ch 1 and 11, where wl's locale caps it at 0x56,
  b43 can keep the 0x5a of the channel before until the next check.

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
