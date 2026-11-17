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

- 0x416 in the BU tweaks as two byte writes (low 0xa4, high 0x9b), after
  brcmsmac's rev 2 pattern of the same register.
- Radio 0x01e at the end of the 2064's init: only bit 2 cleared.
- The hardware control's start: index 180 and 0 packets (brcmsmac: 140, 1).
- Turned off from the software mode, the control loads the gains of the
  current index again; the idle TSSI estimate restores the index before it
  restores the mode.
- 0x942's writes in the spur avoidance (0xf, 0, 0xf on; 0, 0xf, 0 off).
- The AFE overrides' pulses after a tune (bits 0 and 1 of 0x43b/0x43c).
- 0x780's two bits before the channel tweaks.
- Radio 0x03d by channel (0xa to 3, 0x9 to 7, 0x8 after); channel 14 is not
  in the capture.

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
