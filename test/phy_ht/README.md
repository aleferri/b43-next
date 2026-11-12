# test/phy_ht — the HT-PHY against wl's bus

Compiles `phy_ht.c`, `tables_phy_ht.c`, `radio_2059.c` and `phy_common.c`
as they are, against the kernel headers (as `../integration` does), puts a
BCM4331 on a bus that turns their MMIO and agent accesses into the ops
`mmio2ops.py` prints, serves every read from the capture and compares the
two streams op by op.

```sh
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to BSS up, scored
./check.sh               # the TX power control init, op for op
./ht-trace FLOW OPS FIRST LAST [-v | -o TRACE]
```

## From the PHY init through up

`gates.sh` scores the cold 20 MHz captures as `../unit/gates.sh` does the
AC-PHY's: the window runs from wl's first PHY operation after `up` to the
`ssid` mark, b43 runs its up on the capture's channel (flow `full`) with
the window as read oracle, and `../unit/cmp_skip.py --bus` scores the two;
it needs `$TRACE_TOOLS` for `tracelib`. The line to quote is `grezzo`,
with its two parameters. What wl does when the BSS starts, at `ssid` and
`bss up`, is not part of b43's up and is not in the window.

On 2026-11-12, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| cold-2g1-20 | 5334/8502 = 62.74% | 5354/5382 = 99.48% |
| cold-2g6-20 | 5334/8623 = 61.86% | 5354/5382 = 99.48% |
| cold-2g11-20 | 5334/8502 = 62.74% | 5354/5382 = 99.48% |

The second row compares the PHY's and radio's operations only: the core's
MMIO, shared memory and wrapper accesses are outside a harness that
compiles no core, and its calls into the core are counted on stderr. `full`
makes the PHY calls of b43's core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`,
the TX power check of mac80211's first config and the adjustment it
queues, then the HT-PHY's 1 s work once per turn of wl's 1 s watchdog in
the window (each turn reads MMIO 0x4d0 once; `ap_start` too). What is left is mapped in `../../docs/ht-retrace-todo.md`.

`check.sh` takes `$TRACES/bcm4331-rev2/r6400v1`: the board values from its
`nvram.txt`, the windows from its `traces/mmio-*`. A window runs from the
first table 0x1a read of `b43_phy_ht_tx_power_fix()` that is followed by the
control turned off, to the write that turns it on: the tail of
`b43_phy_ht_op_init()`, flow `pctl_init`. The cold captures have one in `up`
and one in `ssid`, the hot ones only on their first channel.

A flow passes only if both streams are equal, no read goes past the capture
and none of the capture's is left over, and b43 printed nothing.

All 14 windows pass.

## The start of the BSS

When beaconing starts b43 initialises the HT-PHY again (its `bss_start`
op), sets the TX power twice, runs a calibration pass (TX I/Q and LO,
then RX I/Q), sets the TX power again and runs a second pass, as wl does
at `ssid`; the 1 s work then runs on wl's turns, the temperature
measurement on the ticks wl's timer has reached (`HT_WD_TICK0`). `gates.sh` scores that too:
flow `ap_start`, on the PHY state the up left (the RC calibration's results
and the table 0x25 pair, read off the capture's up), against the window from
the `ssid` mark to `beaconing`, PHY and radio only. b43's operations there
are wl's in order, but for the twelve of the analog switches of wl's core
down and up, which b43 does not make, and the target power's values; the
calibrations match wl's to the value, coefficients included.

| capture | PHY+radio |
|---|---|
| cold-2g1-20 | 20450/20500 = 99.76% |
| cold-2g6-20 | 20457/20507 = 99.76% |
| cold-2g11-20 | 20442/20492 = 99.76% |

## Exceptions

Compared by class and address only (`value_free[]` in `trace.c`):

- `TXPCTL_TARG_PWR` and `TXPCTL_TARG_PWR2`: the target is brcmsmac's chain
  (`b43_phy_ht_op_recalc_txpower()`), 0x20 until the first recalculation
  and 0x5a after it on the R6400 at 20 MHz, wl's values. They are compared
  by address only where wl's target is one b43 does not compute: the
  40 MHz windows, and ch 1 and 11, where wl's locale caps it at 0x56
  (92 qdBm before the margin; the hot capture has the cap on ch 1 and 11
  only).

## Flows

`pctl_init`, and its pieces for the windows of `cold-2g6-20` `up`:
`power_fix` (#5238–5270), `pctl_off` (#5271–5273), `idle_tssi`
(#5274–5497), `tssi_setup` (#5727–5735), `pctl_on` (#5736–5737).
