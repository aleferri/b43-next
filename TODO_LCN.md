# LCN-PHY: what is left

The LCN-PHY is measured by `test/phy_lcn` against the Compaq CQ57's BCM4313
attach capture (LCN-PHY rev 1, radio 2064 rev 1, SROM rev 8, internal PA, BT
FEM switch); `#n` are the capture's operations as `gates.sh` decodes them.
The scores are in `test/phy_lcn/README.md`.

## Missing from b43

- **The rest of wl's 1 s watchdog** (the bulk decoding, from #110735 on):
  b43's `pwork_1sec` makes only the PHY's estimate and sense values. wl's
  turn on core rev 24 starts with 0x158, the shared memory at 0x15fa,
  0x1630, 0x1632 and 0x1570, REG 0x6c2, the DMA channels and 0x224, not
  `b43_watchdog_poll()`'s 0x4d0, which the LCN-PHY does not call; it reads
  0x4a4 once more after the sense values, then the 32-bit counters at
  0x06c0-0x06d4 (high, low, high), 0x010e, 0x010c and the TSF. It skips the
  PHY's part on a few seconds around its scans. No noise sample: wl never
  writes MACCMD, and nothing of brcmsmac's `calib_modes(PHY_PERICAL_WATCHDOG)`
  (the temperature based recalibration) shows on this board.
- **The block every 256 ms**: shared memory 0x008e, 0x0e50-0x0e54, a MAC
  suspend, an RMW of 0x448, table 21 read from entry 0, then 0x6da,
  0x5a6, 0x5a2, 0x5a5 and 0x583-0x586 written as in #8317-#8381 (#402429
  on). The writes are the setup of brcmsmac's `wlc_lcnphy_samp_cap()`
  (0x5a6 = 5, 0x5a2 = mode 3 in both fields, 0x5a5, 0x583-0x586), into
  table 21, the sample play buffer; the shared memory cells are at the
  word 0x008e points to, plus 0x26-0x28. What wl does with the samples is
  not known.

## Firmware

`b43trace fw-from-capture <attach.txt> --format mmiotrace --rev 24 -o DIR`
rebuilds `ucode24_lcn.fw` (9962 words), `lcn0initvals24.fw` (561 writes)
and `lcn0bsinitvals24.fw` (35) from the capture, the names `b43` asks for
the BCM4313; wl's revinfo says ucode 0x340006e (832.110). The bsinitvals are
the band registers and 15 shared memory cells after them, written blind
(#12051-#12070 of the `--no-bulk` decoding): the IFS registers, 0x000c,
0x000e, the slot time, the beacon TSF offset, the synth PU delay,
0x1670-0x1672 and the eight cells 0x0770-0x080a at stride 0x16. A run of
blind writes ends before the OBJADDR select of the driver's access that
follows it. The files come from the proprietary driver: keep them out of
the tree.

## The shared memory

b43 no longer zeroes it on the LCN-PHY, as wl: every cell wl writes, b43
writes too (the whole bsinitvals, the MAC config block, the coreinit cells,
the beacon listen interval, the five host flag words at the bsinit). The
host flags are the core's shadow, BTCOEX and EDCF in it before the bsinit
as in wl, ANTDIV from `set_rx_antenna`. What still differs at the end of
the attach window, cells b43 writes with other values:

- **Host flags without a source**: HOSTF3 0x2000 and 0x0100, HOSTF4 0x0040,
  HOSTF5 0x0001 (brcmsmac's MHF5_4313_GPIOCTRL, set there only with
  BFL_FEM; the CQ57 has BFL_FEM_BT) and 0x0080.

Settled: the basic rate map at the init is wl's (`brcms_c_set_ratetable()`
with the mandatory rates, `b43_basic_rates_init()`), from the direct maps
scanned once after RFATT (`b43_rate_dirmap_read()`, #31533, as the AC-PHY's), and 0x0480-0x04ff, no
table of the 832 microcode, is left alone as on the AC-PHY; the TX FIFOs
are brcmsmac's for core 24 (`b43_txfifo_fixup()`);
the PHY TX control words keep the initvals' and get only their antenna, the
LCN-PHY's automatic one being the last RX antenna (0, brcmsmac's
PHY_TXC_LCNPHY_ANT_LAST). The EDCA parameters (0x0242-0x02ae) are the ones
mac80211 hands b43 with `conf_tx` at the interface's add, where wl keeps the
initvals' until it associates; PRMAXTIME stays b43's 1, its firmware never
answering probes. MIMO_MAXSYM and the A-MPDU watchdog (0x00ba, 0x003c) wl
raises to 0xffff and 10 for its 64K A-MPDUs (#33248), as
`b43_chip_init_mac()` does with HT, which the LCN-PHY has not in b43.

## The whole of b43 on the virtual chip

`test/integration/cq57.sh` runs b43, its core included, against the attach
capture (profile `cq57`; bcma reads the SROM off the chip, in
`bcmn_bus_register()`'s order; the firmware rebuilt from the capture), and
scores the window from the capture's start to the op before wl's first MAC
enable (#1-#33281 of the `--no-bulk` decoding) against b43's trace cut at
its own first enable after the PAPD. On 2026-11-18, `--min-block 8
--gap-tol 2 --anchor-values`:

| ops | score |
|---|---|
| all | 28312/33110 = 85.51% |
| PHY, radio, MACCTL, I/O control | 14737/16732 = 88.08% |

What is left in the control operations, in the order they come:

- **The bus scan** (#1-#69): wl reads the chip id and the EROM; the harness
  builds the cores by hand.
- **The OTP** (#105-#117): wl reads words 0 and 0x14 of the SROM and the OTP
  registers before the SROM.
- **The PMU and the core resets**: wl's PMU and PLL sequences on chipcommon
  differ, and it cycles the core's wrapper more often; it sets the PCIe
  MDIO up again after the resets.
- **After the up**: wl's block on 0x49e and chipcommon (about 1245 ops).
- **The station's events**: `pipeline.timeline` reads an AP's (watchdog,
  templates, BSS up) and finds none here. The scans (channels 1-13, 15 of
  them), the three associations (#290980, #742070, #1055976 of the bulk
  decoding) and wl's watchdog while associated need their markers and
  their mac80211 calls.

## Operations of unknown purpose

wl makes them, b43 does not; no source explains them, and most leave the
register as it was, so their masks cannot be read off the capture.

- RMW of 0x4d0 and 0x478 at the init's start (#1679).
- Reads of 0x423, 0x550, 0x434, 0x432 and two RMW of 0x55d before the
  2064's init (#6045).
- Three more accesses of 0x4eb after the 2064's 0x4eb setup (#6069).
- Reads of 0x4a5, 0x40d and 0x4a2 and an RMW of 0x4a5 in the TSSI setup,
  after 0x4a8 (#7083).
- RMW of 0x525 after every write of the TX power control's mode.
- RMW of 0x448 after the tune, before the AFE toggle (#8251).
- The block after the TX filters (#8317-#8381): shared memory reads, RMW
  of 0x434 and 0x423, 0x550, a pulse of 0x44a, 0x6da = 0x2048, writes of
  0x5a6, 0x5a2, 0x5a5 and 0x583-0x586.
- RMW of 0x416 and 0x420 after 0x424/0x425 on the spur avoidance channels
  (5-8, 13).
- Two RMW of 0x6d1 after the calibration's results on a channel switch.
- wl's MAC writes around the idle TSSI (0x6b4, 0x6b8, 0x6f0): the core's,
  outside the harness.

## Modelled from the capture, to be confirmed

What b43 makes where wl's capture shows the operations but not their
reason, and where brcmsmac, when it has the step, differs. Each entry says
what the capture shows, what b43 does with it, and what would tell.

### Tables and init

- **TX gain table of the internal PA** (`b43_lcntab_tx_gain_tbl_2ghz_rev0`):
  wl's 128 entries at #3333, pad and BB multiplier taken off the capture.
  brcmsmac's `dot11lcnphy_2GHz_gaintable_rev0` differs on 103 of them, the
  pad on 70; whether wl's is a newer table or this board's is not known.
  The PA gain (0x60) and the fixed gm (15) are brcmsmac's and wl's.
- **RF power by pad gain** (`b43_lcntab_rfpower_ipa`): wl reads only the
  gain entry (0xc0+i) and writes 0x240+i; over the 128 entries the value is
  a function of the pad alone, consistent on every entry, 31 pads (1-31).
  brcmsmac computes it from the BB multiplier, which wl does not read. Pad
  0 never occurs in this table; with a FEM b43 keeps the old code.
- **Switch control table of a BT FEM board without a FEM**
  (`b43_lcntab_sw_ctl_4313_bt_ipa_rev0`): wl's at #4484. brcmsmac's
  `dot11lcn_sw_ctrl_tbl_4313_ipa_rev0_combo` starts the same and differs
  from the 7th entry: taken as a newer version of it, chosen by brcmsmac's
  flags (`BFL_FEM_BT` without `BFL_FEM`). brcmsmac's RFSEQ entry 4 = 100 of
  BT boards is not in the capture and not written.
- **0x416 in the BU tweaks**: wl reads and writes it twice, 0x9fa6 to 0x9fa4
  to 0x9ba4 (#5826). Modelled as the low byte set to 0xa4 and the high byte
  to 0x9b, the pattern brcmsmac's rev 2 baseband init uses on the same
  register (two byte fields). A board whose register starts elsewhere would
  show whether the masks are the bytes.
- **Radio 0x01e at the end of the 2064's init**: 0x06 to 0x02 (#6120),
  modelled as bit 2 cleared.
- **The radio writes at the end of the 2064's init** (#6109-#6124): wl's,
  written as they are, only without a FEM, as brcmsmac's last three
  (0x032, 0x033, 0x039) are.

### TX power control

- **Where the hardware control starts**: index 180 and 0 packets
  (`B43_LCN_TX_PWR_CTRL_START_IDX`, `_NPT`), what wl writes to 0x4a4 and
  0x4a5 when it turns the control on (#8117). brcmsmac starts from 140 and
  1. wl may compute them; another board or a later turn-on would tell.
- **Only `b43_phy_lcn_set_tx_pwr_by_index()` loads an index's gains**, after
  turning the control off; turning it off alone loads none. Every case of
  the capture fits: one transition and a load at #7421 (index 52, the idle
  TSSI's restore) and #10465 (index 30, the TX I/Q-LO calibration), one
  transition and no load at #8674 and in every recalculation.
- **The idle TSSI's restore loads the index, then restores the mode**
  (#7421, #7485); brcmsmac restores only the mode.
- **The TX I/Q-LO calibration loads index 30 twice** (#10465, #10726), where
  brcmsmac turns the control off and sets fixed target gains, then loads it.
- **0x4a9 bit 15 off when the control turns on**, after the gain override
  (#8130 and every turn-on, the register at 0): brcmsmac's
  `wlc_lcnphy_set_tx_pwr_soft_ctrl()` sets it in the same place, for the
  index the software control imposes.
- **The target at the init is 0** (0x4a7, #8110): wl's; brcmsmac sets
  `LCN_TARGET_PWR` (60). The rate table is written, there and when the
  control turns on, only once a recalculation has computed it.

- **0x4d0 bits 6-14 = 0x1f8 in the TSSI setup** (#7289): wl's; brcmsmac
  writes 0.
- **The TSSI mux after the PA**: brcmsmac's, with wl's write of radio 0x07f
  = 0 after 0x11a and without the two 0x4d7 bits at its start, which wl sets
  in the idle TSSI estimate instead (#7028-#7058).

### TX power recalculation

wl's steps are brcmsmac's `wlc_phy_txpower_recalc_target_lcnphy()`: the
control off, the 20 offsets at 0x340 (4 CCK, 8 OFDM, 8 MCS) and the target,
the control on, which writes both again (#22422 after the init, #87399 after
the switch to channel 2). The values, in qdBm, on the CQ57:

| channels | target | CCK | OFDM, MCS 0-6 | MCS 7 |
|---|---|---|---|---|
| 1-11 | 58 | +12 | -2 | 0 |
| 12-13 | 40 | +10 | 0 | 0 |

- **The offsets' sign**: power = target - offset, as brcmsmac's
  `-tx_power_offset`; the other way MCS 7 would be stronger than MCS 0-6,
  against the SROM.
- **OFDM and MCS**: the SROM's (66, 64, MCS 7 62), capped by a locale row
  (64 on 1-11, 44 on 12-13), less a backoff of 4 where brcmsmac has 6; the
  target is the lowest of them. The rows are of this board's ccode (XV);
  another ccode would show others.
- **CCK, approximated**: wl's CCK has a logic of its own, which one capture
  does not determine. b43 takes the SROM's CCK (68) without the backoff,
  capped by a CCK row (52 on 12-13), with brcmsmac's CCK offset of
  `wlc_lcnphy_set_tx_pwr_soft_ctrl()` (22): offset = target - power + 22. An
  offset of 14 with the backoff and a row of 48, or of 18 with the backoff
  and a row of 52, give the same bytes on this board.
- **The target is written every time**, unchanged or not (#22726, #87658);
  brcmsmac writes it only when it changes. When it changes, the control
  starts again from 1 packet, wl's 0x4a5 from #22731 on, and from the index
  it had: brcmsmac's `wlc_lcnphy_txpower_reset_npt()` resets both, but in
  the scan at #450197-#482262 of the bulk decoding the target goes from 58
  to 40 and back and every turn-on starts from 0x98, the watchdog's.
- **The watchdog's estimate** (`b43_phy_lcn_tx_pwr_update_npt()`), every
  second with the control on: once the frames since the last estimate or
  turn-on exceed 2^npt, the npt read back goes up by one to at most 2, and
  0x473 less its bit 0 becomes the start in 0x4a4 at once and at the next
  turn-on. Every turn-on writes npt 1, never the watchdog's 2: wl keeps no
  npt from it, where brcmsmac's keeps the one it read. brcmsmac neither
  raises the npt nor writes the start there, and calls it only when the
  control turns off. The capture's 156 turns fit; whether the 2 is a
  ceiling or a value wl sets is not known.
- **0x476 and 0x477**, read by the watchdog right after the estimate:
  brcmsmac's tempsense registers, read without its setup as
  `wlc_lcnphy_tempsense_new(pi, 0)` does. b43 keeps them in `lcn->sense`;
  that they are a temperature on a board without the temperature based
  control is not shown.
- **The regulatory limit less the antenna gain, and the user's**, applied to
  both chains as the HT-PHY and brcmsmac (`channel.c`) do: not in the
  capture, whose channels have none in the harness. The CQ57's SROM has an
  antenna gain of 8 dB (`ag0`): under a 20 dBm limit, 12 dBm, against wl's
  15 on channels 1-11.
### Warm-up and PAPD calibration

After the init's calibration, with the control on (#17725-#20981), as wl:
brcmsmac has neither, so the capture is the only source, and all 7 of them
are on channel 1.

- **The warm-up**: the offsets cleared, the target at 0x40, 50 dummy frames
  (`b43_dummy_transmission()`) each followed by the control's index
  (`b43_phy_lcn_get_current_tx_pwr_idx()`), between deaf modes. 50 in every
  warm-up of the capture, also when the index settles after 3 (#184388): no
  stop condition. The PAPD loads the index it settles at (#19632, 0x44 from
  0x473 = 0x88).
- **The PAPD's helpers are b43's**: the RX gain override, `tx_pu`, the RX
  gain by distribution (brcmsmac's, now with biq2, lna1 and the TR switch),
  `set_tx_pwr_by_index`, the AFE clock init, the TR switch override, the TX
  filter latch. 0x671 = the index's RF power times 8, as 0x6a6 in
  `b43_phy_lcn_load_tx_pwr_index()`.
- **The engine's setup is wl's on channel 1**, written as it is
  (`b43_phy_lcn_papd_setup[]`, the 8 samples of table 21, the radio
  loopback values): which values depend on the channel, the board or the
  index is not known. A capture on another channel would tell.
- **Table 24's bit 19** (0x80000) is set in every write and absent in the
  reads back: taken as a write flag. The post-processing copies entry 5
  into 0-4, 0x3e into 0x3f and 1 into 0, as wl.
- **The PAPD compensation** is 0x676 bit 7, 0x6da bit 0 and three nibbles
  of 0x86c (4 each): wl turns it off before the calibration, in that order,
  one RMW per nibble, and on after it. At the attach it is off already, so
  the RMWs leave the registers as they are; at #187615 it is on and they
  clear 0x676 0xb8a0 to 0xb820, 0x6da 0x2049 to 0x2048, 0x86c 0x444 to 0.
  These are the only bits of the three that change in the capture.
- **Around the PAPD, from brcmsmac's fields and the capture's changes**:
  0x4eb bits 0-2 = 3 (2 with a FEM) and bits 3-5 = 3, the fields of
  brcmsmac's init, restored with 0x4ea to its 0x4688 and 0xb; 0x631 |= 0x15,
  as brcmsmac's RX I/Q calibration; radio 0x0ff bit 4 and 0x007 bit 0 off,
  the bits the I/Q-LO loopback turns on (the only ones that change in the
  capture); radio 0x0d7 bit 3 on after the calibration (likewise).
- **wl's RX gain** writes, after brcmsmac's fields, lna1 again into 0x4e6
  bits 3-4 (as brcmsmac's other `set_rx_gain()`), then clears 0x4ae bits
  12-15, the only bits of 0x4ae that change in its 77 calls.
- **The registers wl reads after the calibration** (0x671, 0x673-0x676,
  0x679, 0x6da) are written nowhere else in the capture: not a save. b43
  reads them for the calibration statistics only (`papd/regs`).
- **Not understood yet**, each with what the capture gives:
  - an RMW of 0x4b1 in wl's RX gain, after the TR switch: of 0x4b1 only
    bits 11-12 (lna1) ever change; brcmsmac clears bit 15 there from rev 2;
  - an RMW of 0x4b0 before the first RX gain (#19587), and of 0x6c9 before
    the setup writes it (#19984): neither changes in the capture;
  - the TX gains read before the index is loaded (#19629), not restored:
    brcmsmac's calibrations restore theirs when the control was off, and
    every PAPD of the capture starts with it on;
  - a second read of 0x4d7 before its RMWs;
  - radio 0x128 (an RMW that never changes) and 0x116 = 0 before the
    radio's restore, which writes 0x116 again.
  Each shifts the harness's reads of its register.

### Channel

- **The 2064's 0x03d by channel**: 0xa on 1-3, 0x9 on 4-7, 0x8 on 8-13, as
  every tune of the capture writes it; brcmsmac writes 0x3. Channel 14 is
  not in the capture and gets 0x8.
- **0x942 in the spur avoidance**: wl writes 0xf, 0, 0xf turning it on and
  0, 0xf, 0 turning it off, after the 0 of the channel tweaks; brcmsmac
  writes 7 or 0 once. Written as wl does.
- **0x424/0x425 on the spur avoidance channels** (5-8, 13): 0x25 and 0x590f,
  wl's on every tune of the capture; brcmsmac's 0x1f and 0x590a.
- **0x780 before the channel tweaks**: 0 to 0x400 to 0x600 at the init
  (#8142), 0x600 left as it is on every switch; modelled as bits 10 and 9
  set.
- **The pulses of the AFE overrides after a tune**: bits 0 then 1 of 0x43b
  and 0x43c, override on, value on, value off, override off (#8317, and on
  every switch). The name is b43's.
- **The TX filters**: wl's coefficients are brcmsmac's CCK type 25 and OFDM
  type 2 with the numerator negated (b0, b1, b2 of each section), the
  values 16-bit sign extended; kept under those types. Without a FEM wl
  loads OFDM 2, brcmsmac a type 3 its table lacks. wl writes the first
  coefficient twice and latches the filter (0x6da, 0x6d3).
- **No second tune after the init**: wl tunes once, inside the init, before
  the calibration; `b43_phy_init()` skips its tune for the LCN-PHY as for
  the HT-PHY.

### Calibrations

- **A read of 0x4a4 in the TX I/Q-LO calibration**, after 0x6da = 0xffff
  and before the first command, in all four of the capture (#11071 the
  first, 0x10b4 every time): wl's, its use not known. b43 makes it too,
  so the harness's later reads of 0x4a4 get their own values.
- **The periodic calibration starts from the control's index** (0x4a4,
  0x473, #8457), as wl; brcmsmac takes its cached index.
- **The periodic calibration's preamble** (#8419-#8444), all 4 of the
  capture: deaf, the PAPD compensation off, the TX I/Q-LO coefficients
  cleared (radio 0x089-0x08c = 0x88, `wlc_lcnphy_set_tx_iqlo_coeffs()`'
  value for 0, then LOCC and IQCC). Off, the compensation also keeps the
  harness's reads of 0x676 in wl's order for the PAPD.
- **The order of the init's calibration**: deaf, the idle TSSI estimate and
  the TSSI to power table, the TX I/Q-LO calibration, the RX I/Q one, as
  wl (#8417 on). brcmsmac's `wlc_lcnphy_periodic_cal()` runs the I/Q
  calibrations first.
- **The TX I/Q-LO commands**: wl's seven (0x8421 and 0x8423 where brcmsmac
  has 0x8434), each with brcmsmac's number.
- **The RX I/Q calibration's tones**: amplitudes 60 and 120 (#13249,
  #13377), brcmsmac's 20 and 40; the gain search and the ratio test are
  brcmsmac's. The compensation is `b43_rx_iq_comp()` of `calibrate.c`,
  shared with N, HT and AC, rounding the root of b as wl does: on the CQ57
  wl's estimate (#17295-#17300) gives b = 0x3e6 rounded, 0x3e5 truncated,
  and wl writes 0x3e6 (#17308). Not a hypothesis any more.
- **The results a channel switch loads**: the TX I/Q coefficients (sign
  extended from 10 bits, as wl writes them there, 0xfff1, where after the
  calibration it writes 0x03f1), the LO, the radio's LO (0x089-0x08c, read
  back after the calibration) and the RX compensation, in wl's order
  (#86227-#87398).

## Not covered by the capture

- Boards with a FEM: the external PA's TX gain table, the RF power table
  (b43 keeps the older code and its rewrite), the switch control tables of
  the ePA and BT ePA boards, the TSSI after the external PA, the OFDM
  filter 0.
- Boards with the temperature based TX power control
  (`tempsense_option` other than 3): `b43_phy_lcn_sense_setup()` is the old
  port and has TODOs.
- Channel 14, LCN-PHY revs 0 and 2, 5 GHz.
- The polling of 0x451 (I/Q-LO calibration) and 0x481 (I/Q estimate):
  hardware timing, b43 polls fewer times than wl.

## The harness

- `test/phy_lcn/gates.sh` scores the attach, the second scan's channel
  switches and the PHY's part of every watchdog turn; the first scan
  (#45250-#82148) and the associations are not scored.
- The capture has no marks: the windows are found by patterns, see
  `gates.sh`.
