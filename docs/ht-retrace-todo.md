# HT-PHY retrace: what is left

The gate is `test/phy_ht/gates.sh --min-block 8 --gap-tol 2`, against the
R6400 v1's cold captures (BCM4331, wl 7.14.164.18). Numbers here are from
`cold-2g6-20`, window #173-#8731.

## Where the operations are

`test/phy_ht/gates.sh` scores `up`, from wl's first PHY operation after the
`up` mark to the `ssid` mark: on `cold-2g6-20`, 8559 of wl's operations, of
which 5382 are the PHY's and the radio's. `ssid` (26827) and `bss-up`
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
are one block, 5350 of wl's 5382 (99.41%), six values apart: the target
power, by design (`test/phy_ht/README.md`, "Exceptions"). wl's 1 s
watchdog rewrites 0x280/0x283 on every second turn, as b43's 1 s work now
does. What is left is the watchdog's third turn after `wl up`: a read of
table 13 entry 0x5f (#8520) and, after a core reset, the analog switch and
the radio off (#8590-#8613), wl's power saving while no BSS is up, which
b43 does not have.

The PHY's calls into the core (`b43_mac_suspend()`, the shared memory,
`b43_mac_switch_freq()`, the PLL) emit nothing in the harness: on the AC
`test/unit/main.c` mirrors them, here they are only counted.

## `ssid`

b43 runs what wl runs when the BSS starts (the HT-PHY's `bss_start` op;
flow `ap_start` in `test/phy_ht`): the later init, the TX power set twice,
a calibration pass, the TX power again and a second pass, then the 1 s
watchdog's work, temperature measurement included. Of wl's PHY and radio
operations from `ssid` to `beaconing` only the twelve of the analog
switches of wl's core down and up are missing, and the target power's
values differ, by design.

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
one. Where hot differs from cold, it goes here; `test/phy_ht` has no hot
gate yet.

## 40 MHz

b43's HT-PHY tunes `chandef->chan`, the primary channel, at 20 MHz whatever
the width; it does not run 40 MHz yet, while `main.c` advertises it. Run at
20 MHz on the centre channel against the cold 40 MHz captures (`cold-2g6l-40`,
primary 6, centre 8), b43's up already follows wl but for the points below,
and its start of the BSS too. What 40 MHz needs, from those captures:

- **The core's bandwidth.** wl moves the wrapper's IOCTL from 0x2055 to
  0x209d, 0x2093 and 0x2095 (`B43_BCMA_IOCTL_PHY_BW_40MHZ` with the clock
  forced and released) and switches the analog on again (#706-#720), before
  the radio. `phy.bw_clkbits` carries this for the AC.
- **The radio on the centre channel**, `chandef->center_freq1`.
- **The sideband**: after the radio's channel, a read-modify-write of 0x0a0,
  0x0887 for a lower primary (1l, 6l) and 0x0897 for an upper one (11u),
  and of 0x310 (#881-#884).
- **The gain control's table 0x25**: two pairs where 20 MHz has one, 0x6a
  and 0x29 on 6l; the offsets by channel are to be read off the hot 40 MHz
  capture as for 20 MHz.
- **The AFE setup** (`b43_phy_ht_afe_unk1()`): bit 0 cleared, not set, and
  table 8 entries 4 and 5 at 0xfc60 and 0x0c, not 0xa840 and 0x08.
- **Table 7** at 0x149, 0x159, 0x169 where 20 MHz writes 0x144, 0x154, 0x164.
- **The calibrations**: tones of 320 samples (sample depth 0x13f), the
  I/Q LO command N num 0x7987 instead of 0x6876, and bit 0x20 in the RF
  control 0x849 + 0x20 * core of the RX calibration (0x420, not 0x400).
  The tones' frequencies at 40 MHz are to be checked against the N-PHY's
  (5000 kHz for TX, 4000 kHz for RX in brcmsmac).

The TX power target is the same at 40 MHz on the R6400 (0x5a, 0x56 on a
primary of 1 or 11), already handled by the locale's caps.
