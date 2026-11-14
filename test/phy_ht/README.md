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

On 2026-11-13, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio+MAC |
|---|---|---|
| cold-2g1-20 | 5481/7293 = 75.15% | 5340/5485 = 97.36% |
| cold-2g6-20 | 5477/7418 = 73.83% | 5340/5485 = 97.36% |
| cold-2g11-20 | 5477/7297 = 75.06% | 5340/5485 = 97.36% |
| cold-2g1l-40 | 5481/7343 = 74.64% | 5350/5520 = 96.92% |
| cold-2g6l-40 | 5482/7524 = 72.86% | 5349/5519 = 96.92% |
| cold-2g11u-40 | 5484/7340 = 74.71% | 5350/5520 = 96.92% |

The second row compares the PHY's and the radio's operations, and the MAC's
around them: MACCTL, which the PHY's work suspends, and the core's I/O
control (the wrapper's 0x408), whose forced gated clock wl toggles around
it. The rest of the core's MMIO and wrapper accesses are outside a harness
that compiles no core, and its calls into the core are counted on stderr.
Not the ones of the watchdog's turn: the 16-bit shared memory accessors, and
copies of main.c's `b43_mac_suspend()`, `b43_mac_enable()`, `b43_tsf_read()`
and `b43_watchdog_poll()` (but its RX ring's index), make their operations
as on the bus. Their reads are served from each of wl's turns on (each reads
MMIO 0x4d0 once), since wl reads the core's registers and shared memory at
its inits and in its interrupt handling too; GEN_IRQ_REASON says suspended
while MACCTL's enable bit is clear, the microcode's answer. `full` makes the
PHY calls of b43's core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`,
the TX power check of mac80211's first config and the adjustment it queues,
then the HT-PHY's 1 s work once per turn of wl's 1 s watchdog in the window,
each followed by the noise sample's interrupt (`ap_start` too).

The turn is wl's: the core's poll (0x4d0, the radio switch at 0x158, the
DMA channels' status), a MAC suspend with the CRS minimum power (0x280 and
0x283) in it, the noise sample asked for (0x0308-0x0312 cleared, `MACCMD`
0x10), the statistics (the four macstat counters, the block at 0x06c0 and
its 32-bit counters), the TSF, and the sample read back after its
interrupt (0x008c, 0x0308-0x0314); the temperature, on its ticks, in a MAC
suspend of its own. What differs is in `../../docs/ht-retrace-todo.md`,
"The core".

Of the MAC's operations, wl's core resets and the MAC control of its init
(#17-#260 on `cold-2g6-20`) are the core's, which the harness does not
make; without them the I/O control's reads b43 makes around its PHY work
take values of wl's resets (0x45, 0x07), and b43's forced gated clock
writes follow from them. `../integration/r6400.sh` runs the core and
measures them. wl's power saving while no BSS is up, which b43 does not
have, is not scored (`drop_power_save()` in `../unit/compare.py`): at the
watchdog's third turn after `wl up` a read of table 13 entry 0x5f (#8520 on
`cold-2g6-20`), then the DMA stopped, a core reset with the analog switch
on, the radio off (0x810) and the analog switch off (#8590-#8613), and the
core into reset, out of it and back every 2 s until the next
configuration.

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
the `ssid` mark to `beaconing`, whole and PHY, radio and MAC. b43's PHY
and radio operations there are wl's in order, but for the fourteen of wl's core down and up (the
analog switches and the radio's power), which b43 does not make; the
calibrations match wl's to the value, coefficients included, and so do the
values the watchdog writes to 0x280 and 0x283.

| capture | grezzo | PHY+radio+MAC |
|---|---|---|
| cold-2g1-20 | 21314/37780 = 56.42% | 20585/21237 = 96.93% |
| cold-2g6-20 | 21396/28484 = 75.12% | 20594/21246 = 96.93% |
| cold-2g11-20 | 21324/33706 = 63.26% | 20577/21227 = 96.94% |
| cold-2g1l-40 | 29722/46509 = 63.91% | 28917/29573 = 97.78% |
| cold-2g6l-40 | 29703/36785 = 80.75% | 28910/29566 = 97.78% |
| cold-2g11u-40 | 29711/40626 = 73.13% | 28910/29564 = 97.79% |

## The start of the BSS, hot

The hot captures cycle `wl down; chanspec; wl up; ssid; bss up` through the
channels, and wl initialises the PHY only at `ssid`: the later init, on a
channel other than the init before. b43 takes a full init after a down
(`../../docs/ht-retrace-todo.md`, "The hot cycle"), but its `bss_start` op
is that later init, and `hot.sh` scores it on every `ssid-N` window, to
`down-N`, whole and PHY, radio and MAC: the channel setup, the
calibrations, the temperature measurements and the watchdog's turns of 25
configurations.

| MHz | window | grezzo | PHY+radio+MAC |
|---|---|---|---|
| 20 | ssid-1 | 20565/31162 = 65.99% | 20095/20747 = 96.86% |
| 20 | ssid-2 | 21116/28140 = 75.04% | 20642/21294 = 96.94% |
| 20 | ssid-3 | 20543/26501 = 77.52% | 20094/20744 = 96.87% |
| 20 | ssid-4 | 21126/26253 = 80.47% | 20647/21297 = 96.95% |
| 20 | ssid-5 | 20556/25617 = 80.24% | 20102/20752 = 96.87% |
| 20 | ssid-6 | 21026/27430 = 76.65% | 20642/21312 = 96.86% |
| 20 | ssid-7 | 20557/26554 = 77.42% | 20086/20736 = 96.87% |
| 20 | ssid-8 | 21102/29891 = 70.60% | 20624/21274 = 96.94% |
| 20 | ssid-9 | 20480/26978 = 75.91% | 20101/20751 = 96.87% |
| 20 | ssid-10 | 21105/26335 = 80.14% | 20627/21277 = 96.95% |
| 20 | ssid-11 | 20565/28883 = 71.20% | 20095/20745 = 96.87% |
| 40 | ssid-1l | 28887/38847 = 74.36% | 28403/29098 = 97.61% |
| 40 | ssid-2l | 29352/36579 = 80.24% | 28954/29632 = 97.71% |
| 40 | ssid-3l | 28908/35458 = 81.53% | 28433/29087 = 97.75% |
| 40 | ssid-4l | 29445/34506 = 85.33% | 28964/29640 = 97.72% |
| 40 | ssid-5l | 28896/34028 = 84.92% | 28419/29089 = 97.70% |
| 40 | ssid-6l | 29326/35158 = 83.41% | 28954/29618 = 97.76% |
| 40 | ssid-7l | 28877/36541 = 79.03% | 28410/29064 = 97.75% |
| 40 | ssid-5u | 29436/34568 = 85.15% | 28962/29616 = 97.79% |
| 40 | ssid-6u | 28886/35017 = 82.49% | 28416/29072 = 97.74% |
| 40 | ssid-7u | 29435/36650 = 80.31% | 28960/29622 = 97.77% |
| 40 | ssid-8u | 28892/37825 = 76.38% | 28413/29089 = 97.68% |
| 40 | ssid-9u | 29329/37114 = 79.02% | 28947/29609 = 97.76% |
| 40 | ssid-10u | 28898/34265 = 84.34% | 28423/29077 = 97.75% |
| 40 | ssid-11u | 29430/37357 = 78.78% | 28943/29605 = 97.76% |

Of the PHY's and the radio's operations, the rest falls in two kinds, on
every window, beside the MAC's, as at the up:

- the fourteen of wl's core down and up, twenty on 1l;
- the values the watchdog writes to 0x280 and 0x283 on the turns the rule
  does not give, ch 6 at 20 MHz and some at 40
  (`../../docs/ht-retrace-todo.md`, "The watchdog's 0x280 and 0x283").

b43's noise sample and statistics operations pair with wl's, values
included, but for up to 13 of about 500 on a window, and about 70 on hot
ssid-6, ssid-9, 2l, 6l and 9u: there wl reads the counters a second time
in the first turns, and the turns pair one off.

## Exceptions

Every value is compared. Of wl's operations, its power saving while no BSS
is up is not scored, as above. The TX power target is brcmsmac's chain
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
