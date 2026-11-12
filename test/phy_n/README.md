# test/phy_n — the N-PHY against wl's bus

Compiles `phy_n.c`, `tables_nphy.c`, `radio_2055.c`, `radio_2056.c`,
`radio_2057.c`, `phy_common.c` and `ppr.c` as they are, against the kernel
headers (as `../integration` does), puts a BCM43217 (chip rev 0, core rev 30,
N-PHY rev 17, radio 2057 rev 14) on a bus that turns their MMIO and agent
accesses into the ops `mmio2ops.py` prints, serves every read from the
capture and compares the two streams. It is `../phy_ht` for the N-PHY; the
captures are the RT-AC56U's under `$TRACES/bcm43217-rev0/rt-ac56u`.

```sh
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to `wl up`, scored
./n-trace FLOW OPS FIRST LAST [-v | -o TRACE]
./regions.py OPS TRACE FIRST:LAST      # where the two streams part, largest first
make N_FN=1 && N_FN=1 ./n-trace full OPS FIRST LAST -o TRACE
./fnmap.py OPS TRACE FIRST:LAST        # each b43 call and the wl ops it matches
```

`gates.sh` needs `$TRACE_TOOLS` for `tracelib`; `GATE_TMP=DIR` keeps the
window's ops and b43's trace for `regions.py`.

## From the PHY init through up

The window runs from wl's first PHY operation after the `up` mark to the
`ssid` mark. b43 runs, on the capture's channel at 20 MHz (flow `full`), the
PHY calls of its core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`,
and the TX power check of mac80211's first config. `../unit/cmp_skip.py
--bus` scores the two streams; the line to quote is `grezzo`, with its two
parameters. The second row compares the PHY's and radio's operations only:
the core's MMIO, shared memory and wrapper accesses are outside a harness
that compiles no core, and its calls into the core are counted on stderr.

On 2026-11-12, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| cold-2g1-20 | 9602/32733 = 29.33% | 9622/28406 = 33.87% |
| cold-2g6-20 | 9607/32193 = 29.84% | 9627/28403 = 33.89% |
| cold-2g11-20 | 9604/32160 = 29.86% | 9624/28411 = 33.87% |

What is left is mapped in `../../docs/n-retrace-todo.md`.

Every run prints, on stderr:

- **reads past the window**: 6 on every capture, all of them reads b43
  makes and wl does not, in this window: ChipCommon `chipcontrol` (0x028,
  b43 sets bit 6 on a board with an external LNA; wl never accesses it),
  PHY 0x009, 0x0c01 and 0x0c0a, MMIO 0x492, and one radio 0x35e read more
  than wl's. They are served from b43's own writes, or 0.
- **radio accesses through 0x3f6/0x3fa**: b43's N-PHY reaches the radio
  through the registers of core revs before 24; wl, on this core rev 30,
  through 0x3d8/0x3da. The harness prints both as `RAD.*`, so the stream
  compares the radio registers; the count says how many go the other way.
- **b43 messages**: none. Before the 2.4 GHz EPA TX gain table of radio
  2057 rev 14, "No 2GHz EPA gain table available for this device", twice.

## How the harness differs from ../phy_ht

- **Table reads are served per cell.** PHY 0x072 selects the table and the
  offset, 0x074 is a 32-bit cell's high half and 0x073 its low half, and
  the offset advances on each 0x073 access. A read of 0x073 or 0x074 is
  served from wl's reads of the same (table, offset, half), not from wl's
  reads of the register in wl's order: b43 reads its tables in another
  order, and a per-register queue gave it other cells' values (on these
  captures 1022 table writes with a wrong value, that were wl's zeros of
  table 15's I/Q and LO results copied into tables 26/27 at 320 and 448).
  On ../phy_ht's captures the per-cell oracle gives the same scores and
  `check.sh` results as the per-register one.
- **ChipCommon is on the bus**, for `bcmn_cc_set32()` of the N-PHY init:
  its accesses are `CC.RD`/`CC.WR` ops, served by `CC` key.
- **The nvram** is the CFE's `0:name=value` as well as `pci/1/1/name`.

## Flows

- `full`: above.
- `N_PERICAL=0` (any flow) overrides `nphy->perical`, which
  `b43_nphy_op_prepare_structs()` sets to 2: with 2, `b43_phy_initn()` runs
  neither the TX I/Q-LO nor the RX I/Q calibration. An experiment, not
  b43's flow; see the todo.

## Environment

`N_NVRAM` (the board's nvram file), `N_CHANNEL` (1-14, default 6),
`N_PERICAL`, `N_DEBUG=1` (each read past the window on stderr), `N_FN=1`
(function markers, `----FN:name----` and `----/FN:name----`, in the trace;
the binary must be built with `make N_FN=1`, which instruments the
functions of `phy_n.c` but not the register accessors; `cmp_skip.py` skips
the markers, so the score is the same).
