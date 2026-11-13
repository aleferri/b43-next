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
| cold-2g1-20 | 5473/7511 = 72.87% | 5337/5547 = 96.21% |
| cold-2g6-20 | 5469/7636 = 71.62% | 5337/5547 = 96.21% |
| cold-2g11-20 | 5469/7515 = 72.77% | 5337/5547 = 96.21% |
| cold-2g1l-40 | 5473/7561 = 72.38% | 5347/5582 = 95.79% |
| cold-2g6l-40 | 5481/7758 = 70.65% | 5346/5581 = 95.79% |
| cold-2g11u-40 | 5476/7558 = 72.45% | 5347/5582 = 95.79% |

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
DMA channels' status), a MAC suspend with the 0x280 and 0x283 rewrite and
the temperature in it, the noise sample asked for (0x0308-0x0312 cleared,
`MACCMD` 0x10), the statistics (the four macstat counters, the block at
0x06c0 and its 32-bit counters), the TSF, and the sample read back after its
interrupt (0x008c, 0x0308-0x0314). What differs is in
`../../docs/ht-retrace-todo.md`, "The core".

Of the MAC's operations, wl's core resets and the MAC control of its init
(#17-#260 on `cold-2g6-20`) are the core's, which the harness does not make;
without them the I/O control's reads b43 makes around its PHY work take
values of wl's resets (0x45, 0x07), and b43's forced gated clock writes
follow from them. `../integration/r6400.sh` runs the core and measures them.
The 16 of wl's PHY and radio operations left are its power saving while no
BSS is up, which b43 does not have: at the watchdog's third turn after
`wl up` a read of table 13 entry 0x5f (#8520 on `cold-2g6-20`) and, after a
core reset, the analog switch on, the radio off (0x810) and the analog
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
the `ssid` mark to `beaconing`, whole and PHY, radio and MAC. b43's PHY
and radio operations there are wl's in order, but for the fourteen of wl's core down and up (the
analog switches and the radio's power), which b43 does not make, and on 1l
the values the watchdog writes to 0x280 and 0x283; the calibrations match
wl's to the value, coefficients included.

| capture | grezzo | PHY+radio+MAC |
|---|---|---|
| cold-2g1-20 | 21307/37787 = 56.39% | 20551/21271 = 96.62% |
| cold-2g6-20 | 21388/28492 = 75.07% | 20554/21286 = 96.56% |
| cold-2g11-20 | 21316/33714 = 63.23% | 20543/21261 = 96.62% |
| cold-2g1l-40 | 29715/46516 = 63.88% | 28873/29617 = 97.49% |
| cold-2g6l-40 | 29696/36792 = 80.71% | 28870/29606 = 97.51% |
| cold-2g11u-40 | 29704/40633 = 73.10% | 28870/29604 = 97.52% |

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
| 20 | ssid-1 | 20486/31169 = 65.73% | 20001/20769 = 96.30% |
| 20 | ssid-2 | 21035/28149 = 74.73% | 20552/21312 = 96.43% |
| 20 | ssid-3 | 20466/26498 = 77.24% | 19992/20766 = 96.27% |
| 20 | ssid-4 | 21041/26262 = 80.12% | 20545/21323 = 96.35% |
| 20 | ssid-5 | 20479/25614 = 79.95% | 20000/20774 = 96.27% |
| 20 | ssid-6 | 20942/27434 = 76.34% | 20547/21327 = 96.34% |
| 20 | ssid-7 | 20478/26561 = 77.10% | 19992/20758 = 96.31% |
| 20 | ssid-8 | 21021/29900 = 70.30% | 20534/21292 = 96.44% |
| 20 | ssid-9 | 20402/26984 = 75.61% | 20007/20773 = 96.31% |
| 20 | ssid-10 | 21024/26344 = 79.81% | 20537/21295 = 96.44% |
| 20 | ssid-11 | 20486/28890 = 70.91% | 20001/20767 = 96.31% |
| 40 | ssid-1l | 28807/38855 = 74.14% | 28309/29120 = 97.21% |
| 40 | ssid-2l | 29273/36586 = 80.01% | 28865/29649 = 97.36% |
| 40 | ssid-3l | 28825/35465 = 81.28% | 28335/29109 = 97.34% |
| 40 | ssid-4l | 29364/34507 = 85.10% | 28881/29643 = 97.43% |
| 40 | ssid-5l | 28817/34031 = 84.68% | 28331/29101 = 97.35% |
| 40 | ssid-6l | 29246/35166 = 83.17% | 28860/29640 = 97.37% |
| 40 | ssid-7l | 28799/36547 = 78.80% | 28316/29086 = 97.35% |
| 40 | ssid-5u | 29355/34577 = 84.90% | 28872/29634 = 97.43% |
| 40 | ssid-6u | 28807/35024 = 82.25% | 28322/29094 = 97.35% |
| 40 | ssid-7u | 29355/36654 = 80.09% | 28866/29640 = 97.39% |
| 40 | ssid-8u | 28813/37824 = 76.18% | 28324/29098 = 97.34% |
| 40 | ssid-9u | 29249/37122 = 78.79% | 28853/29631 = 97.37% |
| 40 | ssid-10u | 28819/34272 = 84.09% | 28329/29099 = 97.35% |
| 40 | ssid-11u | 29349/37366 = 78.54% | 28849/29627 = 97.37% |

Of the PHY's and the radio's operations, the rest falls in three kinds, on
every window, beside the MAC's, as at the up:

- the fourteen of wl's core down and up, twenty on 1l;
- at the later init wl restores the gain state four times, with the clip
  thresholds between, after the table 0x1c writes (76 operations), and does
  not write 0x280 and 0x283 in the gain control setup, where b43 writes
  them as wl does at the cold `ssid` (4);
- the values the watchdog writes to 0x280 and 0x283 where wl raises them
  (`../../docs/ht-retrace-todo.md`, "The watchdog's 0x280 and 0x283").

b43's noise sample and statistics operations pair with wl's, values
included, but for up to 13 of about 500 on a window, and about 70 on hot
ssid-6, ssid-9, 2l, 6l and 9u: there wl reads the counters a second time
in the first turns, and the turns pair one off.

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
