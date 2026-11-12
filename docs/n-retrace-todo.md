# N-PHY retrace: what is left

The gate is `test/phy_n/gates.sh --min-block 8 --gap-tol 2`, against the
RT-AC56U's cold captures (BCM43217, N-PHY rev 17, radio 2057 rev 14,
external PA and LNA; wl 7.14.164.18 under DD-WRT r66477). Numbers here are
from `cold-2g6-20`, window #189-#31089.

## Where the operations are

`up` is 30901 of wl's operations, of which 27188 are the PHY's and the
radio's; b43's `full` flow makes 7517 PHY and radio operations. `ssid`
(#31090-#73468, about 42k) and `bss-up` are what wl does when the BSS
starts; b43's N-PHY has no `bss_start` op, and they are not scored yet.

## `up`, PHY and radio, in order

`test/phy_n/regions.py` on the gate's files lists where the streams part.
In wl's order:

| wl ops | at | wl | b43 | what |
|---|---|---|---|---|
| 19 | #952 | `STRA_2U` 0x1df = 0x1591, 0x1e1, ... | - | CRS registers b43 does not write |
| 44 against 14 | #3202 | `TXF_20CO_*` 0x186-0x196, 0x2c6-0x2c7 | 0x2c6 only | the 20 MHz TX filter coefficients |
| 37 against 5 | #3349 | table 7 0x152-... (0x2b48, 0x3389, ...) | 0x32f | table 7 entries b43 does not write |
| 313 | #3450 | 0x0c75 = 1, then 0x021/0x2a8 polled, gain tables 0-3, 7, 8 | - | an RX gain block (inference: AGC/clip setup) |
| 18 | #4007 | `AFECTL_OVER1` 0x08f read-back, 0x0aa, table 7 0x110, table 15 0x57 | - | |
| 158 against 58 | #8020 | 0x020/0x2a7, 0x021/0x2a8, 0x280 reads | radio 0x16 ... | |
| 360 | #9871 | tables 26/27 at 64-147 | - | the adjusted power table, written again |
| 2598 | #10628 | `TXPCTL_CMD` 0x1e7, `RSSI1`/`RSSI2` 0x219/0x21a, five tone loads in table 17, tables 26/27 at 64-147 | - | the TX power control's TSSI measurement (inference) |
| 16202 against 6 | #13518 | `IQLOCAL_CMD` 0x0c0 polled, table 15, table 17, then | - | the first TX I/Q-LO calibration pass, and |
| (of which 15125) | #14630 | `C1_TXPCTL_STAT` 0x1ed, `IQLOCAL_CMD` 0x0c0 (1078 reads), `IQEST_CMD` 0x129 (171 reads), table 15 (388 writes), table 17 (eight tone loads), tables 26/27 0x040-0x23f (2944 writes each) | - | **the TX I/Q-LO and RX I/Q calibrations** |

Before #4933 and between the gaps, b43's operations are wl's in the same
order: the radio 2057 init and channel tune (radio writes 0x082, 0x0c4,
0x149, 0x1b7 and 0x1c2 apart, and 0x047/0x0e3 written with other values),
most of the PHY init, the tables' static contents and the TX gain table
(tables 26 and 27 at 192, #4933). The equal blocks hold 7035 operations.

## b43's calls against wl's ops

`test/phy_n/fnmap.py` (with `N_FN=1`) places each of b43's calls in wl's
stream, `cold-2g6-20`:

| b43 | wl |
|---|---|
| the analog switches, `b43_radio_2057_init()`, the channel | #189-#951 |
| `b43_phy_initn()`, from the workarounds through `b43_nphy_tx_power_ctrl()` | #978-#8016, 6560 of its 6606 ops matched |
| ... of which `b43_nphy_rssi_cal()` | #5447-#6799 |
| `b43_phy_init()`'s channel switch after the init | none: wl does not tune again |
| `b43_nphy_op_recalc_txpower()`: `tx_power_ctl_setup()` | #9595-#10627 |
| ... `tx_power_ctrl()` | #13348-#13517 |

So the init is in place, and what b43 lacks comes after it: #8020-#9594
(mostly the core; on the PHY, a save and restore of the gains, 0x280/0x283
and the RSSI calibration values 0x224-0x22f), #10628-#13347 (between the
power control setup and its enable) and the calibrations from #13518.

## wl's calibration at `up`, step by step

The same steps as brcmsmac's `wlc_phy_cal_perical_nphy_run()` (open source,
an older wl), on `cold-2g6-20`:

| wl | step |
|---|---|
| #10448-#10457 | the original power index from `C1/C2_TXPCTL_STAT` 0x1ed/0x1ee, the original TX gain from table 7 at 0x110 (0x101a on both cores) |
| #10628 | TX power control off (0x1e7 0xe019 -> 0x0019) |
| #10628-#13342 | **the TX gain pre-calibration** (b43: `/* TODO N PHY Pre Calibrate TX Gain */`) |
| #13343-#13517 | the power index read again, the TX power control |
| #13518-... | the TX I/Q-LO calibration at the gain the pre-calibration found: table 7 at 0x110 = 0x10fa on both cores (#13553) |
| ...-#31089 | the RX I/Q calibration and the save |

**The pre-calibration** is not brcmsmac's: for rev >= 7 brcmsmac sets a
fixed index, and its `wlc_phy_cal_txgainctrl_nphy()` estimates the power
from 0x1e9 and the TX power tables, which wl does not read here. wl 7.14,
in five rounds of one tone each (table 17, 160 samples):

1. the TSSI path set up as for the idle TSSI (radio 0x175-0x17c and
   0x195-0x19c, the RSSI select), the tone, one `RSSI1`/`RSSI2` read;
2. then per core, two rounds: the baseband multiplier of table 15 at 87
   (and 95) set to that core's byte only (0x4000 for core 1, 0x0040 for
   core 2, from 0x4040), the TX gain forced through `RFCTL_TXG1`/`TXG2`
   (0x07c/0x07f, enabled by bit 12 of 0x0e7/0x0ec), the tone, ten
   `RSSI1`/`RSSI2` reads.

The forced gains are a two-step search on the PAD field (rev 7 layout: txgm
14:12, pga 11:8, pad 7:3, ipa 2:0), the same on both cores:

| capture | first round | second round | result (table 7, 0x110) |
|---|---|---|---|
| 20 MHz, ch 1, 6, 11 and the hot sweep | 0x10c2 (pad 24) | 0x10fa (pad 31) | 0x10fa |
| 40 MHz, 6l | 0x109a (pad 19) | 0x10c2 (pad 24) | 0x10c2 |

The step from the first round to the second follows from the RSSI read in
it; the rule is the next thing to fit, with the harness serving the same
RSSI samples to a port.

## Why b43 does not calibrate

`b43_nphy_op_prepare_structs()` sets `nphy->perical = 2` ("avoid
additional rssi cal on init (like wl)"). With 2, `b43_phy_initn()` takes the
`else if (nphy->mphase_cal_phase_id == 0)` branch, "N PHY Periodic
Calibration with arg 3", which is empty: the init runs neither
`b43_nphy_cal_tx_iq_lo()` nor `b43_nphy_cal_rx_iq()`, and nothing else
calls them. wl runs both at `up`, at #13518-#31089.

`N_PERICAL=0` lets the init run them, as an experiment: b43 then makes 24
`IQLOCAL_CMD` accesses and no `IQEST_CMD`, before wl's TX power control
measurement instead of after it, and the PHY+radio score falls to 20.28%.
The calibrations need their place in the flow (wl's periodic calibration
with arg 3, at the end of the init) and the TX gain table they start from.

## Not b43's PHY, but on the bus

- **ChipCommon `chipcontrol` bit 6.** `b43_phy_initn()` sets it on a board
  with `B43_BFL_EXTLNA` on 2.4 GHz (0x028: read 0, write 0x40). wl does not
  access 0x028 in any of the RT-AC56U's captures, the traced part of the
  attach included. On this
  board the bit cuts the second core's RX (openwrt/openwrt#25922).
- **The radio registers.** b43 reaches the radio through 0x3f6/0x3fa; wl,
  on core rev 30, through 0x3d8/0x3da (`B43_MMIO_RADIO24_*`).
- **The PMU chip control register 1** (0x650 = 1, 0x654 = 0x2100: the
  external PA bits 13 and 8) is read, written and read back by wl at `up`
  and `ssid`. b43 does not set them; they are outside this harness, which
  compiles no bcma.

## Done

- The 2.4 GHz EPA TX gain table of radio 2057 rev 14: wl's 128 cells at
  tables 26 and 27, 192, are the same on ch 1-11 at 20 and 40 MHz, at `up`
  and at `ssid`, on both cores, and in all eleven cycles of the hot sweep.
  PHY+radio 23.47% -> 25.42%.

## Next

1. The calibrations' place and contents (#10628-#31089): run them where
   wl does (the perical branch), port the pre-calibration above, then
   compare the TX I/Q-LO and RX I/Q calibrations op by op.
2. The small blocks: CRS (#952), TX filter (#3202), table 7 (#3349), the RX
   gain block (#3450).
3. A `bss_start` flow against `ssid`, as `test/phy_ht` has.
4. The 40 MHz captures (`cold-2g6l-40`, ...), once the flow sets the width.
