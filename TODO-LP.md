# LP-PHY open items

What is suspected in `phy_lp.c` and not yet settled, one item per entry, with
the evidence and the check that closes it. Closed items are not kept.

The evidence is the code itself, the reverse-engineered specs at
bcm-v4.sipsolutions.net (an archive of 2016, partly updated to wl 5.10.56.46,
partly older than the code and in places wrong) and brcmsmac's LCN-PHY, a
sibling with the same register block, and one capture: wl's attach of the
HP 550's BCM4312 (PHY rev 1, radio 2062 rev 2, boardflags `0x00060A01`,
`rssisav2g` 0) in b43-next-traces, `bcm4312-rev1/hp-550`: init, tunes to 7
and 1, scans of channels 1-11. `test/phy_lp` runs b43's init against it.

More captures would cover the rest:

- a rev 2 chip (BCM4325, radio 2063): init and a channel switch, with the
  crystal frequency.
- BCM5354 (PHY rev 0): init only.
- a dual-band BCM4325 on 5 GHz: wl's attach, a tune to a channel of each
  sub-band (36, 100, 149) and the SROM dump. b43 keeps the LP-PHY on
  2 GHz (`b43_wireless_core_attach()`), so every 5 GHz path in `phy_lp.c`
  is the specs' and has never run.

Hardware TX power control runs on boards without NOPA; the items marked
*unreachable* are the ones it still does not reach.

## Suspects

An entry that opens with *Changed, to confirm* has its fix in the tree, made
by the commit that marked it. Revert that commit if the capture disagrees;
delete the entry when it agrees.

### `rssi_gs` in AFE_RSSI_CTL_0 on rev0/1

Changed, to confirm: `gs` is written. The specs write
`vf | vc << 4 | gs << 10 | 0xA000` (Rev0BasebandInit, steps 25-26).

Check: the write to PHY 0x43D in an init on a board whose `rssisav2g` is
not 0. The BCM4312's is 0: wl writes `0xA00F`, `vf` 0xF and `vc` 0, which
confirms the rest of the layout.

### Mask of the second write to TR_LOOKUP_1

Changed, to confirm: `0xC0FF`, as on the seven writes next to it. The specs
have `0x3F00` (Rev0BasebandInit, step 34.2), taken as a mask that was not
inverted.

Check: PHY 0x44E after init on a rev 1 board with boardflags `0x00400000`
(FEM_BT): low six bits `0x0A` and bits 8-13 `0x09` mean `0xC0FF`.

### When TR lookups 1-4 are copied to 5-8

Changed, to confirm: the copy is made on rev 1 without FEM_BT (`0x00400000`),
as in the specs (step 38), and no longer with PAREF (`0x00040000`).

Check: PHY 0x4C7-0x4CA against 0x44E, 0x44F, 0x4BB, 0x4BC on a rev 1 board
whose PAREF and FEM_BT flags are both set or both clear. The BCM4312 has
PAREF and not FEM_BT: wl copies, as both conditions do.

### Flag of the 5354 PR45960 workaround

Changed, to confirm: PHASESHIFT (`0x00100000`) as in the specs, step 39,
instead of FEM_BT.

Check: on a 5354 in the 4712S package, whether PHY 0x4BF = 5 and
0x4BE = 0xFFFF are written, with the board's flags.

### TX filter cap on channel 14

Changed, to confirm: the `+ 5` is skipped on rev 1 when the channel is 14, as
in the specs (SetRCCap, step 3).

Check: radio 0x4F (north) on the BCM4312 tuned to 14. The capture has
channels 1-11 only, where wl adds the 5: `rc_cap` 0x89 gives TX_CTL_A
0x89.

### Start of the rev2 TX RC calibration

Changed, to confirm, against the letter of the specs: the TX half writes its
`0x7D` to radio 0x5A as the RX half does (step 10), not to 0x52 (RCCalRev2,
step 21).

Check: which radio register gets `0x7D` after the second 0x5D/0x5E pair.

Also open: the poll is 10000 times `msleep(1)`; the specs poll every 10 us
for 10 s at most.

### Hardware TX power control

On boards without NOPA, as wl on the BCM4312 (the specs' TXPwrCtrlInit
gives it to boards with NOPA). The per-rate targets are brcmsmac's chain
over the board's PPR, as for the HT-PHY: wl's locale row for the CCK rates
(17 dBm on channels 1-11), regulatory and user limits, less 1.5 dB, at
least 10 dBm. On the HP 550 they are wl's, CCK 62 and OFDM 60,
but b43 writes them as the lowest, 60, in PHY 0x4A7 and offsets 2 and 0
above it, where wl writes 36 and offsets 26 and 24.

Open:

- the CCK row is the one of the locale the capture was taken in; other
  locales, and channels 12-14, which have no row, are not captured.
- wl's base of 36 comes from no rate b43 has.
- rev2 (table 7, PHY 0x4D0) runs the same code without a capture.
- 5 GHz takes the specs' PA coefficients per sub-band, a TSSI index of 64
  and no CCK limit; unreached while b43 keeps the band off.

### Initial TX power index override (unreachable)

Changed, to confirm: the override starts at `-1`, which means none in the
code. What wl starts from is not known and a capture does not show it.

### PA gain from the rev2 TX gain table

Changed, to confirm: Set PA Gain is called on rev2+, as in the specs
(SetTXPwrbyIndex, step 14), and no longer on rev0/1.

Check: PHY 0x4FB and 0x4FD written after 0x4B5 when wl sets a power index
on a rev 2 chip. On the BCM4312 (rev 1) wl never reaches either register,
which agrees with the rev0/1 half.

### Layout of the PA gain in PHY 0x4FB

Changed, to confirm: bits 8-14 for the get and both sets, taken from the
LCN-PHY, not from any LP-PHY evidence. The specs give three layouts
(GetPAGain bits 0-6, SetPAGain bits 6-12, SetTXGain bits 8-14).

Check: the value wl writes to 0x4FB on 2 GHz, where the table's PA gain is
`0x7F`: `0x7Fxx` is bits 8-14, `0x1FC0 | pad` is bits 6-12.

### 2063 PLL words

Changed, to confirm: the words follow Radio/2063ChannelTune and agree with a
separate implementation of it.

Check: radio 0x7B-0x7E, 0x70-0x73, 0x68, 0x69 after a tune. For a 26 MHz
crystal on channel 1 the specs give 0x7B = `0x09`, 0x7C low three bits 5,
0x7D bits 5-6 clear and low nibble 6, 0x7E = `0x57`, 0x70 low five bits 7,
0x71 = `0xBB`, 0x72 = `0x13`, 0x73 = `0xB1`, 0x68 low six bits `0x30` with
bit 6 clear, 0x69 low five bits 6 with bit 5 clear.

Also open in the same function:

- the count: the specs do not subtract 1, brcmsmac's 2064 tune does. 0x7E
  one lower than above means it is subtracted.
- radio 0x81 bit 0: the specs set and clear it with bit 1 (steps 78-79);
  the code only handles bit 1.
- radio 0x75 bit 1: the specs test `value1a > 45` on a Q16 value, always
  true; the code tests `val1 == 45`, never true.
- the channel table has the 12 columns of the older driver; the updated
  specs have 15, other registers, and write PHY 0x4F0/0x4F1 in the tune.

### Field mask in `lpphy_sync_stx()`

Changed, to confirm: only the field is copied. The specs OR the whole
shifted register in; wl's `mod_phy_reg()` cuts the value to the mask.

Check: PHY 0x4F2-0x4F8 after the 2063 init, against the radio registers
0x3D-0x52 read in the same capture.

Missing: the specs put writes of `0xFF` to radio 0x04-0x08 before the loop
and clear them after (SYNCHSTX, steps 1-5 and 7-11); the masks given there do
not agree with each other. The capture shows the real ones.

### TX filter on the 2063

`b43_lpphy_op_switch_channel()` runs the filter setup and the gain table
adjust for the 2062 only; the specs run both for either radio
(SetChanSpecLPPHY, steps 6-7). The rev2 branch of
`lpphy_set_analog_filter()` is the old one (radio 0x55 = `0x3F`) and is never
reached; the updated specs write PHY 0x448, 0x4EA, 0x4EB and a digital
filter instead (TxFilterInit, step 4).

Check: what wl writes after a 2063 tune.

### Overrides around the RX IQ calibration

`lpphy_rx_iq_cal()` sets bit `0x800` in PHY 0x44C and 0x44D and at the end
clears `0x800` in 0x43C, leaving 0x44C/0x44D as set; it clears bit 0 of
0x43B at the start, where the LCN-PHY sets it. All of it is in the specs
(RXIQCal, steps 10-13 and 23-24).

Check: the writes to 0x43B, 0x43C, 0x44C, 0x44D around a noise calibration
(the 0x49E writes mark it), and 0x44C/0x44D after it, on rev0 or rev2: the
calibration of rev1 only sets the table's compensation, as wl does.

### Overflow in the RX IQ estimate

`ipwr << (20 - prod_msb)` in `lpphy_calc_rx_iq_comp()` leaves 31 bits when
the IQ product is more than about 2^11 times smaller than the I power: the
coefficient is then computed from a wrapped divisor. The specs do the same
in 32 bits.

Check: by computation on `i_pwr`, `q_pwr`, `iq_prod` taken from a capture
(PHY 0x483-0x488 after the estimate), comparing the coefficient wl writes to
0x49E with the one the code would.

### TX I/Q-LO calibration beyond the BCM4312

`lpphy_tx_iqlo_cal()` follows wl's rev1 runs, whose coefficients it
reproduces. Without evidence:

- rev0 (the 5354 among them) runs the same steps; the specs' commands,
  older than wl's, differ.
- the target gains of boards with HGPA or NOPA are the specs' branch that
  the capture shows inverted; the 5354 rev3 entry and the 5 GHz one are
  the specs'.
- rev2 has no TX I/Q-LO calibration: its branch of TXIqloCal (PHY 0x43E,
  0x4D0, 0x503, radio 0x121) is not written.
- wl runs a third calibration, with the recalibration steps, after its
  second tune to channel 1; b43's own tune after `b43_lpphy_op_init()`
  calibrates nothing until the periodic work.

Check: a rev0 or rev2 capture, and the periodic calibration after 60 s.

## Hunches

Noticed while reading, with nothing to weigh them against: no spec page read,
no sibling code, no computation. They are places to look first, not claims.
The `SPEC FIXME` notes already in `phy_lp.c` are not repeated.

### Band data on SDIO

`ssbn_sdio_get_invariants()` fills the SPROM with `0xFF` and reads the MAC,
the PA parameters and the maximum powers from the CIS. `tri2g`, `bxa2g`,
`rxpo2g` and the three `rssi*2g` stay `0xFF`, so on an SDIO chip the rev2
init writes `0xFF` to the low byte of INPUT_PWRDB and `0xFFFF` to
AFE_RSSI_CTL_1. Where wl takes them from there is not known.

### Digital filter state saved twice

`lpphy_save_dig_flt_state()` saves the nine filter registers and loads fixed
coefficients over them. The rev2 init calls it; `lpphy_calibration()` calls
it again and restores at the end. From the second call on, what is saved is
the fixed coefficients: the restore changes nothing and the values of the
init are gone. Whether wl's save loads anything is not known.

### RSSI of received frames

`b43_rx()` sends LP-PHY frames through `b43_rssi_postprocess()`, the G-PHY
conversion of the JSSI byte. Nothing says the LP-PHY reports the same
quantity.

### Crystal entry of the 2062 init

`lpphy_2062_init()` takes the first entry of `freqdata_tab` above the
reference frequency. The table ends at 19200 kHz: the 25 MHz of the 5354
matches nothing and gets the last entry from the fallback. The 20 MHz of
the BCM4312 gets it too, and there wl writes the same values.

### Arithmetic of the rev0/1 RC calibration

The squared error is kept in 32 bits and wraps when a tone measures more than
about 16 times the first. If the first tone measures 0, every cap scores the
same and 128 is taken. On wl's measurements of the BCM4312 the errors stay
under 2^27 and the cap is wl's, 0x89.

## Not reviewed

Not compared with the specs or with anything else: the rev2 baseband init,
the 2063 radio init, the software rfkill, the digital filter save and
restore around the periodic calibration, `lpphy_pr41573_workaround()`, and
the rev2 tables in `tables_lpphy.c`.

Compared with wl's bus on the BCM4312 only (`test/phy_lp`), where they agree
but for the entries above: the 2062 radio init, the rev0/1 RC calibration and
the loopback search, `lpphy_disable_crs()` and its restore, the periodic
calibration and its TX I/Q-LO part, `lpphy_tx_pctl_init_hw()` and the rev0/1
tables.
