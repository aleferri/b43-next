# test/phy_lcn — the LCN-PHY against wl's bus

Compiles `phy_lcn.c`, `tables_phy_lcn.c` and the files every PHY links
against as they are, against the kernel headers (as `../integration` does),
puts a BCM4313 (chip rev 1, core rev 24, LCN-PHY rev 1, radio 2064 rev 1) on
a bus that turns their MMIO, agent and ChipCommon accesses into the ops
`mmio2ops.py` prints, serves every read from the capture and compares the
two streams. It is `../phy_n` for the LCN-PHY; the capture is the Compaq
CQ57's under `$TRACES/bcm4313-rev1/compaq-cq57`.

```sh
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to wl's first MAC enable
./lcn-trace FLOW OPS FIRST LAST [-v | -o TRACE]
```

`gates.sh` needs `$TRACE_TOOLS` for `mmio2ops` and `tracelib`; `GATE_TMP=DIR`
keeps the window's ops, the decoded SROM and b43's trace.

## The capture and the board

`traces/mmio-attach/attach.txt.gz` is an x86 mmiotrace of wl's attach and
`up`, without marks. `gates.sh` decodes it with `mmio2ops.py`, which takes
BAR0 from the trace's `MAP` line. The board values come from the SROM:
`srom.txt`, through `../sprom/decode_r8`, the tree's rev 8 extractor, as the
`LCN_NVRAM` the harness reads. The board has no FEM (`boardflags=0x402201`):
an internal PA.

## From the PHY init through wl's first MAC enable

The window runs from the first of wl's three analog switches before the PHY
init (those of its two core resets and the init's own: b43 makes the same
three in flow `full`) to the op before wl's first MAC enable, after its
calibrations: #1089-#22546. wl's attach tunes channel 1, and b43 runs on it
the PHY calls of its core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`, and
the TX power check of mac80211's first config. `../unit/cmp_skip.py --bus
--rmw` scores the two streams; the line to quote is `grezzo`, with its two
parameters. The second row compares the PHY's and radio's operations only:
the core's MMIO, shared memory and wrapper accesses are outside a harness
that compiles no core, and its calls into the core are counted on stderr.

On 2026-11-17, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| attach | 14932/20132 = 74.17% | 15026/16571 = 90.68% |

The init follows wl's through the tables, the baseband and radio init, the
hardware TX power control, the channel and the PMU, then the calibration of
the init: the idle TSSI, the TX I/Q-LO and the RX I/Q calibrations, the TX
power warm-up and the PAPD calibration, and the TX power recalculation of
mac80211's first config. What b43 does not make yet, in wl's order:

- a few operations of unknown purpose here and there, which no source
  explains: an RMW of 0x4d0 and 0x478 at the start, of 0x55d before the
  2064's init, of 0x4eb after it, of 0x525 after the TX power control's mode;
- wl polls the I/Q-LO calibration's status (0x451) more often than b43,
  which offsets the reads around each command;
- wl reads 0x4a4 once in the I/Q-LO calibration (#10844): every later read
  of 0x4a4 b43 makes is served the value of the one before it.

## The channel switches

The capture holds wl's scans over channels 1-13 and its associations, all
on channel 1. `gates.sh` scores every switch of the second scan, where wl
has its calibration from the init and loads it again on each channel:
from the switch's first PHY operation to the MAC's enable, flow `switch`,
with the calibration's results wl holds there (`calres.py`). On 2026-11-17,
`--min-block 8 --gap-tol 2`, PHY and radio:

| channels | PHY+radio |
|---|---|
| 2-4, 9-12 | 1604/1609 = 99.69% |
| 5-8 | 1606/1617 = 99.32% |
| 13 | 1606/1618 = 99.26% |

What b43 does not make there is the RMWs of unknown purpose of 0x6d1 and
0x525 (`TODO_LCN.md`).

## Flows

- `full`: above; the TX power check's adjust runs where b43 queues it.
- `switch`: `b43_switch_channel()` and `channel_calibrate`, as
  `b43_op_config()` makes them, on `LCN_CHANNEL` after an init, the
  calibration's results from `LCN_TXIQCC_A`, `LCN_TXIQCC_B`, `LCN_TXDIDQ`,
  `LCN_LOFT0`-`3`, `LCN_RXIQCOMP_A` and `_B`.

## Environment

`LCN_NVRAM` (the board's nvram), `LCN_CHANNEL` (1-14, default 1), `LCN_DEBUG=1`
(each read past the window on stderr).
