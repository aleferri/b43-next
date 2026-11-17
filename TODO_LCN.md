# LCN-PHY: what is left

The LCN-PHY is measured by `test/phy_lcn` against the Compaq CQ57's BCM4313
attach capture (LCN-PHY rev 1, radio 2064 rev 1, SROM rev 8, internal PA, BT
FEM switch); `#n` are the capture's operations as `gates.sh` decodes them.
The scores are in `test/phy_lcn/README.md`.

## Missing from b43

- **The watchdog's work**: brcmsmac's `calib_modes(PHY_PERICAL_WATCHDOG)`
  (the temperature, the glacial calibration, the VCO calibration) and the
  noise sample; what wl does while associated on channel 1 (from
  #108038, between its scans) is not analysed yet. wl never writes MACCMD
  in the capture, so no background noise request: brcmsmac's polled sample
  (`wlc_lcnphy_rx_signal_power()` between deaf modes) would be the other
  way, but where the capture turns the deaf mode off and on (#18967) it
  reads nothing between.
- **`agc_temp_init`**: wl reads 0x4df, table 17 at 59-60, 0x434, 0x424,
  0x425 and table 13 at 28-29 after the PMU (#8239-#8251); b43 neither
  reads nor keeps them, and nothing uses them yet.

## Firmware

`b43trace fw-from-capture <attach.txt> --format mmiotrace --rev 24 -o DIR`
rebuilds `ucode24_lcn.fw` (9962 words), `lcn0initvals24.fw` (562 writes)
and `lcn0bsinitvals24.fw` (5) from the capture, the names `b43` asks for
the BCM4313; wl's revinfo says ucode 0x340006e. To check: the initvals run
ends with a write of OBJADDR (0x160 = 0x10026), the select of the read
that follows it, not an initval; and whether the band group is the
LCN-PHY's. The files come from the proprietary driver: keep them out of
the tree.

## Not run yet

- `test/integration` (the whole of b43 on a virtual chip, as `r6400.sh`
  does for the HT-PHY) has no LCN board: a profile in
  `test/board_profile.h` (BCM4313, chip rev 1, core rev 24, PCI 0x4727,
  SROM rev 8), the core's reads of this capture as the oracle, and the
  firmware above.

## Operations of unknown purpose

wl makes them, b43 does not; no source explains them, and most leave the
register as it was, so their masks cannot be read off the capture.

- RMW of 0x4d0 and 0x478 at the init's start (#1677).
- Reads of 0x423, 0x550, 0x434, 0x432 and two RMW of 0x55d before the
  2064's init (#5952).
- Three more accesses of 0x4eb after the 2064's 0x4eb setup (#5976).
- Reads of 0x4a5, 0x40d and 0x4a2 and an RMW of 0x4a5 in the TSSI setup,
  after 0x4a8 (#6958).
- RMW of 0x525 after every write of the TX power control's mode.
- A read of 0x4a4 in the TX I/Q-LO calibration, after 0x6da = 0xffff and
  before the first command (#10844). The harness serves each register's
  reads in wl's order, so every later read of 0x4a4 gets the value of the
  one before it.
- RMW of 0x448 after the tune, before the AFE toggle (#8096).
- The block after the TX filters (#8162-#8226): shared memory reads, RMW
  of 0x434 and 0x423, 0x550, a pulse of 0x44a, 0x6da = 0x2048, writes of
  0x5a6, 0x5a2, 0x5a5 and 0x583-0x586.
- The calibration's preamble (#8272-#8289): RMW of 0x676, 0x6da and three
  of 0x86c, radio 0x089-0x08c = 0x88, the LO and I/Q coefficients cleared.
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
  wl's 128 entries at #3265, pad and BB multiplier taken off the capture.
  brcmsmac's `dot11lcnphy_2GHz_gaintable_rev0` differs on 103 of them, the
  pad on 70; whether wl's is a newer table or this board's is not known.
  The PA gain (0x60) and the fixed gm (15) are brcmsmac's and wl's.
- **RF power by pad gain** (`b43_lcntab_rfpower_ipa`): wl reads only the
  gain entry (0xc0+i) and writes 0x240+i; over the 128 entries the value is
  a function of the pad alone, consistent on every entry, 31 pads (1-31).
  brcmsmac computes it from the BB multiplier, which wl does not read. Pad
  0 never occurs in this table; with a FEM b43 keeps the old code.
- **Switch control table of a BT FEM board without a FEM**
  (`b43_lcntab_sw_ctl_4313_bt_ipa_rev0`): wl's at #4416. brcmsmac's
  `dot11lcn_sw_ctrl_tbl_4313_ipa_rev0_combo` starts the same and differs
  from the 7th entry: taken as a newer version of it, chosen by brcmsmac's
  flags (`BFL_FEM_BT` without `BFL_FEM`). brcmsmac's RFSEQ entry 4 = 100 of
  BT boards is not in the capture and not written.
- **0x416 in the BU tweaks**: wl reads and writes it twice, 0x9fa6 to 0x9fa4
  to 0x9ba4 (#5740). Modelled as the low byte set to 0xa4 and the high byte
  to 0x9b, the pattern brcmsmac's rev 2 baseband init uses on the same
  register (two byte fields). A board whose register starts elsewhere would
  show whether the masks are the bytes.
- **Radio 0x01e at the end of the 2064's init**: 0x06 to 0x02 (#6027),
  modelled as bit 2 cleared.
- **The radio writes at the end of the 2064's init** (#6016-#6031): wl's,
  written as they are, only without a FEM, as brcmsmac's last three
  (0x032, 0x033, 0x039) are.

### TX power control

- **Where the hardware control starts**: index 180 and 0 packets
  (`B43_LCN_TX_PWR_CTRL_START_IDX`, `_NPT`), what wl writes to 0x4a4 and
  0x4a5 when it turns the control on (#7962). brcmsmac starts from 140 and
  1. wl may compute them; another board or a later turn-on would tell.
- **Only `b43_phy_lcn_set_tx_pwr_by_index()` loads an index's gains**, after
  turning the control off; turning it off alone loads none. Every case of
  the capture fits: one transition and a load at #7289 (index 52, the idle
  TSSI's restore) and #10248 (index 30, the TX I/Q-LO calibration), one
  transition and no load at #8512 and in every recalculation.
- **The idle TSSI's restore loads the index, then restores the mode**
  (#7289, #7353); brcmsmac restores only the mode.
- **The TX I/Q-LO calibration loads index 30 twice** (#10248, #10502), where
  brcmsmac turns the control off and sets fixed target gains, then loads it.
- **0x4a9 bit 15 off when the control turns on**, after the gain override
  (#7975 and every turn-on, the register at 0): brcmsmac's
  `wlc_lcnphy_set_tx_pwr_soft_ctrl()` sets it in the same place, for the
  index the software control imposes.
- **The target at the init is 0** (0x4a7, #7955): wl's; brcmsmac sets
  `LCN_TARGET_PWR` (60). The rate table is written, there and when the
  control turns on, only once a recalculation has computed it.

- **0x4d0 bits 6-14 = 0x1f8 in the TSSI setup** (#7157): wl's; brcmsmac
  writes 0.
- **The TSSI mux after the PA**: brcmsmac's, with wl's write of radio 0x07f
  = 0 after 0x11a and without the two 0x4d7 bits at its start, which wl sets
  in the idle TSSI estimate instead (#6903-#6933).

### TX power recalculation

wl's steps are brcmsmac's `wlc_phy_txpower_recalc_target_lcnphy()`: the
control off, the 20 offsets at 0x340 (4 CCK, 8 OFDM, 8 MCS) and the target,
the control on, which writes both again (#22119 after the init, #86970 after
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
- **The target is written every time**, unchanged or not (#22414, #87221);
  brcmsmac writes it only when it changes. When it changes, the control
  starts again from index 180 and 1 packet, wl's 0x4a5 from #22419 on;
  brcmsmac's `wlc_lcnphy_txpower_reset_npt()`.
- **The regulatory limit less the antenna gain, and the user's**, applied to
  both chains as the HT-PHY and brcmsmac (`channel.c`) do: not in the
  capture, whose channels have none in the harness. The CQ57's SROM has an
  antenna gain of 8 dB (`ag0`): under a 20 dBm limit, 12 dBm, against wl's
  15 on channels 1-11.
### Warm-up and PAPD calibration

After the init's calibration, with the control on (#17456-#20678), as wl:
brcmsmac has neither, so the capture is the only source, and all 7 of them
are on channel 1.

- **The warm-up**: the offsets cleared, the target at 0x40, 50 dummy frames
  (`b43_dummy_transmission()`) each followed by the control's index
  (`b43_phy_lcn_get_current_tx_pwr_idx()`), between deaf modes. 50 in every
  warm-up of the capture, also when the index settles after 3 (#183672): no
  stop condition. The PAPD loads the index it settles at (#19349, 0x44 from
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
  the RMWs leave the registers as they are; at #186871 it is on and they
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
  - an RMW of 0x4b0 before the first RX gain (#19304), and of 0x6c9 before
    the setup writes it (#19693): neither changes in the capture;
  - the TX gains read before the index is loaded (#19346), not restored:
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
  (#7987), 0x600 left as it is on every switch; modelled as bits 10 and 9
  set.
- **The pulses of the AFE overrides after a tune**: bits 0 then 1 of 0x43b
  and 0x43c, override on, value on, value off, override off (#8162, and on
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

- **The periodic calibration starts from the control's index** (0x4a4,
  0x473, #8302), as wl; brcmsmac takes its cached index.
- **The order of the init's calibration**: deaf, the idle TSSI estimate and
  the TSSI to power table, the TX I/Q-LO calibration, the RX I/Q one, as
  wl (#8262 on). brcmsmac's `wlc_lcnphy_periodic_cal()` runs the I/Q
  calibrations first.
- **The TX I/Q-LO commands**: wl's seven (0x8421 and 0x8423 where brcmsmac
  has 0x8434), each with brcmsmac's number.
- **The RX I/Q calibration's tones**: amplitudes 60 and 120 (#13015,
  #13142), brcmsmac's 20 and 40; the gain search and the ratio test are
  brcmsmac's. The compensation is `b43_rx_iq_comp()` of `calibrate.c`,
  shared with N, HT and AC, rounding the root of b as wl does: on the CQ57
  wl's estimate (#17033-#17038) gives b = 0x3e6 rounded, 0x3e5 truncated,
  and wl writes 0x3e6 (#17046). Not a hypothesis any more.
- **The results a channel switch loads**: the TX I/Q coefficients (sign
  extended from 10 bits, as wl writes them there, 0xfff1, where after the
  calibration it writes 0x03f1), the LO, the radio's LO (0x089-0x08c, read
  back after the calibration) and the RX compensation, in wl's order
  (#85798-#86969).

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

- `test/phy_lcn/gates.sh` scores the attach and the second scan's channel
  switches; the first scan (#44938-#81737), the associations and the
  watchdog are not scored.
- The capture has no marks: the windows are found by patterns, see
  `gates.sh`.
