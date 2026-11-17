# LCN-PHY: what is left

The LCN-PHY is measured by `test/phy_lcn` against the Compaq CQ57's BCM4313
attach capture (LCN-PHY rev 1, radio 2064 rev 1, SROM rev 8, internal PA, BT
FEM switch); `#n` are the capture's operations as `gates.sh` decodes them.
The scores are in `test/phy_lcn/README.md`.

## Missing from b43

- **TX power recalculation** (`op_recalc_txpower` and `op_adjust_txpower`
  are empty). After a channel switch wl turns the control off, writes 20
  entries of the rate table at 0x340 (0xc each) and the target at 0x4a7
  (0x3a), and turns the control back on, writing the table and the target
  again (#86970 on channel 2, about 320 operations). brcmsmac's
  `wlc_lcnphy_txpower_recalc_target()` has the same steps, but the values
  do not follow from the SROM's maxima and offsets as brcmsmac computes
  them. At the init the target is left at 0, as wl does.
- **The TX power control's warm-up** after the init's calibration: wl turns
  the control off and on, then sends 50 dummy frames and reads the control's
  index (0x473) after each, while it settles from 0xb2 to 0x86-0x88 (#17454,
  about 2000 operations, most of them the core's). The stop condition is not
  known. brcmsmac does not have it.
- **The PAPD calibration** after it (#19403-#22546: the AFE clock in PAPD
  mode, then about 1600 PHY operations). brcmsmac's `wlc_lcnphy_a1()` and
  its callers are the reference.
- **The watchdog's work**: brcmsmac's `calib_modes(PHY_PERICAL_WATCHDOG)`
  (the temperature, the glacial calibration, the VCO calibration) and the
  noise sample; what wl does while associated on channel 1 (from
  #108038, between its scans) is not analysed yet.
- **`agc_temp_init`**: wl reads 0x4df, table 17 at 59-60, 0x434, 0x424,
  0x425 and table 13 at 28-29 after the PMU (#8239-#8251); b43 neither
  reads nor keeps them, and nothing uses them yet.

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
- **Turning the control off reloads the current index's gains**: from the
  software mode wl does (#7300, the gains of index 52 after the idle TSSI
  estimate, which had set 127). From the hardware mode the capture shows
  both: no reload at #8512, a reload of index 30's gains at #10454. b43
  reloads whenever it leaves another mode for off. To set the idle TSSI's
  restore right, it restores the index before it restores the mode.
- **`b43_phy_lcn_set_tx_pwr_by_index()` loads the gains itself only if the
  control was off**: otherwise turning it off already did, and wl does not
  load them twice.
- **The target at the init is 0** (0x4a7, #7955): wl's; brcmsmac sets
  `LCN_TARGET_PWR` (60). The target comes with the TX power recalculation,
  which is missing (above).
- **0x4d0 bits 6-14 = 0x1f8 in the TSSI setup** (#7157): wl's; brcmsmac
  writes 0.
- **The TSSI mux after the PA**: brcmsmac's, with wl's write of radio 0x07f
  = 0 after 0x11a and without the two 0x4d7 bits at its start, which wl sets
  in the idle TSSI estimate instead (#6903-#6933).

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
