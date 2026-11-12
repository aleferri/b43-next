# N-PHY retrace: what is left

The gate is `test/phy_n/gates.sh --min-block 8 --gap-tol 2`, against the
RT-AC56U's cold captures (BCM43217, N-PHY rev 17, radio 2057 rev 14,
external PA and LNA; wl 7.14.164.18 under DD-WRT r66477). Numbers here are
from `cold-2g6-20`, window #189-#31089, main c702e11.

## Where the operations are

`up` is 30901 of wl's operations, of which 27188 are the PHY's and the
radio's; b43's `full` flow makes 6985 PHY and radio operations. `ssid`
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
| 514 | #4933 | tables 26 and 27 at 192, 128 32-bit cells each | - | **the TX gain table**: b43 has none for rev 17 with an external PA ("No 2GHz EPA gain table available") |
| 158 against 58 | #8020 | 0x020/0x2a7, 0x021/0x2a8, 0x280 reads | radio 0x16 ... | |
| 360 | #9871 | tables 26/27 at 64-147 | - | the adjusted power table, written again |
| 2598 | #10628 | `TXPCTL_CMD` 0x1e7, `RSSI1`/`RSSI2` 0x219/0x21a, five tone loads in table 17, tables 26/27 at 64-147 | - | the TX power control's TSSI measurement (inference) |
| 1072 against 1 | #13518 | `IQLOCAL_CMD` 0x0c0 polled, table 15, table 17 | - | the first TX I/Q-LO calibration pass |
| 15125 | #14630 | `C1_TXPCTL_STAT` 0x1ed, `IQLOCAL_CMD` 0x0c0 (1078 reads), `IQEST_CMD` 0x129 (171 reads), table 15 (388 writes), table 17 (eight tone loads), tables 26/27 0x040-0x23f (2944 writes each) | - | **the TX I/Q-LO and RX I/Q calibrations** |

Before #4933 and between the gaps, b43's operations are wl's in the same
order: the radio 2057 init and channel tune (radio writes 0x082, 0x0c4,
0x149, 0x1b7 and 0x1c2 apart, and 0x047/0x0e3 written with other values),
most of the PHY init and the tables' static contents. The equal blocks
hold 6496 operations.

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

## Next

1. The TX gain table for N-PHY rev 17 with an external PA, from the 514
   operations at #4933: 128 cells per core, the same on all three channels
   (to check on the hot sweeps).
2. The calibrations' place and contents (#10628-#31089).
3. The small blocks: CRS (#952), TX filter (#3202), table 7 (#3349), the RX
   gain block (#3450).
4. A `bss_start` flow against `ssid`, as `test/phy_ht` has.
5. The 40 MHz captures (`cold-2g6l-40`, ...), once the flow sets the width.
