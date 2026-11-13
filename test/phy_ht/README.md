# test/phy_ht — the HT-PHY against wl's bus

Compiles `phy_ht.c`, `tables_phy_ht.c`, `radio_2059.c` and `phy_common.c`
as they are, against the kernel headers (as `../integration` does), puts a
BCM4331 on a bus that turns their MMIO and agent accesses into the ops
`mmio2ops.py` prints, serves every read from the capture and compares the
two streams op by op.

```sh
./run.sh                 # all of it, on the linked repositories (../linked.sh)
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to BSS up, scored
./hot.sh --min-block 8 --gap-tol 2     # the start of the BSS on every channel
./check.sh               # the TX power control init, op for op
./ht-trace FLOW OPS FIRST LAST [-v | -o TRACE]
```

## From the PHY init through up

`gates.sh` scores the six cold captures, 20 and 40 MHz, as `../unit/gates.sh` does the
AC-PHY's: the window runs from wl's first PHY operation after `up` to the
`ssid` mark, b43 runs its up on the capture's channel (flow `full`) with
the window as read oracle, and `../unit/cmp_skip.py --bus` scores the two;
it needs `$TRACE_TOOLS` for `tracelib`. The line to quote is `grezzo`,
with its two parameters. What wl does when the BSS starts, at `ssid` and
`bss up`, is not part of b43's up and is not in the window.

On 2026-11-12, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| cold-2g1-20 | 5340/8496 = 62.85% | 5360/5376 = 99.70% |
| cold-2g6-20 | 5340/8617 = 61.97% | 5360/5376 = 99.70% |
| cold-2g11-20 | 5340/8496 = 62.85% | 5360/5376 = 99.70% |
| cold-2g1l-40 | 5346/8540 = 62.60% | 5375/5391 = 99.70% |
| cold-2g6l-40 | 5345/8746 = 61.11% | 5374/5390 = 99.70% |
| cold-2g11u-40 | 5346/8540 = 62.60% | 5375/5391 = 99.70% |

The second row compares the PHY's and radio's operations only: the core's
MMIO, shared memory and wrapper accesses are outside a harness that
compiles no core, and its calls into the core are counted on stderr. `full`
makes the PHY calls of b43's core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`,
the TX power check of mac80211's first config and the adjustment it
queues, then the HT-PHY's 1 s work once per turn of wl's 1 s watchdog in
the window (each turn reads MMIO 0x4d0 once; `ap_start` too).

The 16 of wl's PHY and radio operations left are its power saving while no
BSS is up, which b43 does not have: at the watchdog's third turn after
`wl up` a read of table 13 entry 0x5f (#8520 on `cold-2g6-20`) and, after
a core reset, the analog switch on, the radio off (0x810) and the analog
switch off (#8590-#8613), before the core goes into reset. The core's
operations are in `../../docs/ht-retrace-todo.md`.

`check.sh` takes `$TRACES/bcm4331-rev2/r6400v1`: the board values from its
`nvram.txt`, the windows from its `traces/mmio-*`. A window runs from the
first table 0x1a read of `b43_phy_ht_tx_power_fix()` that is followed by the
control turned off, to the write that turns it on: the tail of
`b43_phy_ht_op_init()`, flow `pctl_init`. The cold captures have one in `up`
and one in `ssid`, the hot ones one in every `ssid-N`: every 2.4 GHz channel
at 20 MHz, 1l-7l and 5u-11u at 40.

A flow passes only if both streams are equal, no read goes past the capture
and none of the capture's is left over, and b43 printed nothing.

All 37 windows pass.

## The start of the BSS

When beaconing starts b43 initialises the HT-PHY again (its `bss_start`
op), sets the TX power twice, runs a calibration pass (TX I/Q and LO,
then RX I/Q), sets the TX power again and runs a second pass, as wl does
at `ssid`; the 1 s work then runs on wl's turns, the temperature
measurement on the ticks wl's timer has reached (`HT_WD_TICK0`). `gates.sh` scores that too:
flow `ap_start`, on the PHY state the up left (the RC calibration's results
and the table 0x25 pair, read off the capture's up), against the window from
the `ssid` mark to `beaconing`, PHY and radio only. b43's operations there
are wl's in order, but for the fourteen of wl's core down and up (the
analog switches and the radio's power), which b43 does not make, and on 1l
the values the watchdog writes to 0x280 and 0x283; the calibrations match
wl's to the value, coefficients included.

| capture | PHY+radio |
|---|---|
| cold-2g1-20 | 20468/20482 = 99.93% |
| cold-2g6-20 | 20475/20489 = 99.93% |
| cold-2g11-20 | 20460/20474 = 99.93% |
| cold-2g1l-40 | 28792/28826 = 99.88% |
| cold-2g6l-40 | 28795/28809 = 99.95% |
| cold-2g11u-40 | 28795/28809 = 99.95% |

## The start of the BSS, hot

The hot captures cycle `wl down; chanspec; wl up; ssid; bss up` through the
channels, and wl initialises the PHY only at `ssid`: the later init, on a
channel other than the init before. b43 takes a full init after a down
(`../../docs/ht-retrace-todo.md`, "The hot cycle"), but its `bss_start` op
is that later init, and `hot.sh` scores it on every `ssid-N` window, to
`down-N`, PHY and radio only: the channel setup, the calibrations and the
temperature measurements of 25 configurations.

| MHz | window | PHY+radio |
|---|---|---|
| 20 | ssid-1 | 19899/20023 = 99.38% |
| 20 | ssid-2 | 20442/20566 = 99.40% |
| 20 | ssid-3 | 19903/20009 = 99.47% |
| 20 | ssid-4 | 20452/20562 = 99.47% |
| 20 | ssid-5 | 19911/20017 = 99.47% |
| 20 | ssid-6 | 20454/20564 = 99.47% |
| 20 | ssid-7 | 19905/19999 = 99.53% |
| 20 | ssid-8 | 20439/20533 = 99.54% |
| 20 | ssid-9 | 19920/20014 = 99.53% |
| 20 | ssid-10 | 20442/20536 = 99.54% |
| 20 | ssid-11 | 19899/20023 = 99.38% |
| 40 | ssid-1l | 28223/28353 = 99.54% |
| 40 | ssid-2l | 28761/28901 = 99.52% |
| 40 | ssid-3l | 28250/28352 = 99.64% |
| 40 | ssid-4l | 28790/28884 = 99.67% |
| 40 | ssid-5l | 28248/28342 = 99.67% |
| 40 | ssid-6l | 28771/28877 = 99.63% |
| 40 | ssid-7l | 28233/28327 = 99.67% |
| 40 | ssid-5u | 28781/28875 = 99.67% |
| 40 | ssid-6u | 28239/28333 = 99.67% |
| 40 | ssid-7u | 28775/28881 = 99.63% |
| 40 | ssid-8u | 28239/28341 = 99.64% |
| 40 | ssid-9u | 28764/28870 = 99.63% |
| 40 | ssid-10u | 28246/28340 = 99.67% |
| 40 | ssid-11u | 28745/28881 = 99.53% |

The rest falls in four kinds, on every window:

- the fourteen of wl's core down and up, twenty on 1l;
- at the later init wl restores the gain state four times, with the clip
  thresholds between, after the table 0x1c writes (76 operations), and does
  not write 0x280 and 0x283 in the gain control setup, where b43 writes
  them as wl does at the cold `ssid` (4);
- where the target differs from the window before's (on ch 1 and 11, and
  on the channel after 1) wl's first TX power set writes the new target,
  b43 the one before (30);
- the values the watchdog writes to 0x280 and 0x283 where wl raises them
  (`../../docs/ht-retrace-todo.md`, "The watchdog's 0x280 and 0x283").

## Exceptions

None: every value is compared. The TX power target is brcmsmac's chain
(`b43_phy_ht_op_recalc_txpower()`) with wl's locale caps on ch 1 and 11; a
later init starts from the target the configuration before left
(`HT_PREV_CHANNEL`, which `check.sh` and `hot.sh` take from the capture's
sequence), as wl does before it computes the new one, and fixes the TX gain
at the index the RX calibration of the BSS before left (`HT_TX_GAIN_IDX`,
10 after a BSS start, 40 before any).

## Flows

`pctl_init`, and its pieces for the windows of `cold-2g6-20` `up`:
`power_fix` (#5238–5270), `pctl_off` (#5271–5273), `idle_tssi`
(#5274–5497), `tssi_setup` (#5727–5735), `pctl_on` (#5736–5737).
