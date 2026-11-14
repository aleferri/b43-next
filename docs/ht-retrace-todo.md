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
and up takes the first init, and the later one is a retune's
(`b43_phy_recycle()`). The calibrations wl runs after it are the HT-PHY's
`channel_calibrate`, after every channel switch. `test/phy_ht/hot.sh`
scores that later init on every hot window.

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

## 802.11n

wl's HT capabilities are in its beacon template, written at the BSS start
of the cold captures (template RAM through 0x130/0x134):

| width | capabilities | A-MPDU | RX MCS |
|---|---|---|---|
| 20 | 0x083d: LDPC, SM power save off, greenfield, short GI 20, A-MSDU 7935 | 0x17: 64 KiB, 4 us | 0-23 |
| 40 | 0x187f: as at 20, and 20/40, short GI 40, DSSS/CCK at 40 | 0x17 | 0-23, 32 |

No STBC, TX or RX. In its shared memory the ACK/CTS, probe response and
beacon PHY TX control words are 0x01c0, the three cores of the SROM's
txchain 7 in the antenna field.

The HT-PHY's 2.4 GHz band announces HT at 20 MHz as wl's but A-MSDU 7935:
MCS 0-23 received, LDPC and greenfield received, the short guard interval.
It sends MCS 0-23, a stream per TX core at most, one stream CDD, with BCC,
on the SROM's TX cores. The header is the pre-AC one, as the 928's
two-word TX status on the R6400 v1 shows (Erik's traffic capture, 0x170
and 0x174 only), and the HT-PHY takes the BCM43227's HT path in it, TX and
RX (`b43_ht_enabled()`). Not tried on hardware.

Not there yet:

- 40 MHz. The PHY's init at 40 is scored as at 20, but b43 sets the core's
  width at the PHY init only (`b43_phy_ht_width()`), so the BSS start's.
  A station's channel at 40, or another width on a running BSS, needs it
  without one, which no capture shows.
- A-MPDU. wl sets the shared memory as brcmsmac does for it (`MIMO_MAXSYM`
  0xffff, `WATCHDOG_8TU` 10); how the 928 sends and acknowledges one needs
  a traffic capture.
- A-MSDUs over 3839 bytes. wl announces 7935 and writes `MAXRXFRMLEN`
  0x0800, so its microcode would hand them over in pieces, as the
  BCM43227's (`b43_rx_amsdu()`, under `b43_nphy_ht_enabled()`): a traffic
  capture again. With 3839 announced b43 leaves `MAXRXFRMLEN` to the
  microcode.
