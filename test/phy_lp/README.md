# test/phy_lp — the LP-PHY against wl's bus

Compiles the LP-PHY's files, the ones every PHY links (`files.mk`) and
`ssb-next/driver_chipcommon_pmu.c` as they are, against the kernel headers
(as `../phy_n` does), puts a BCM4312 (chip rev 1, core rev 15, LP-PHY rev 1,
radio 2062 rev 2) on an SSB bus that turns their MMIO and ChipCommon accesses
into the ops `mmio2ops` prints, serves every read from the capture and
compares the two streams. It is `../phy_n` for the LP-PHY; the capture is the
HP 550's under `$TRACES/bcm4312-rev1/hp-550`.

```sh
make K=... KG=...        # the kernel headers, see ../phy_n/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to the MAC enable, scored
./lp-trace FLOW OPS FIRST LAST [-v | -o TRACE]
make LP_FN=1 && LP_FN=1 ./lp-trace full OPS FIRST LAST -o TRACE
```

`gates.sh` needs `$TRACE_TOOLS`: the capture is the raw mmiotrace of `wl`'s
attach, and `b43trace.bus.mmio2ops` decodes it at each run. `GATE_TMP=DIR`
keeps the window's ops and b43's trace.

## From the PHY init to the MAC enable

The window runs from wl's first PHY operation after its MAC control write
`0x44020402` to the operation before the first MAC enable: the tables, the
baseband and 2062 init, a tune to channel 7, the RC calibration, two tunes to
channel 1 and the calibrations around them. b43 runs, on channel 1 (flow
`full`), the PHY calls of its core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, then
`b43_phy_init()`. `../unit/cmp_skip.py --bus --rmw` scores the two streams;
the line to quote is `grezzo`, with its two parameters. The second row
compares the PHY's and radio's operations only: the core's MMIO, shared
memory and ChipCommon accesses outside the PMU's LDOs are outside a harness
that compiles no core, and its calls into the core are counted on stderr.

On 2026-11-16, `--min-block 8 --gap-tol 2`:

| capture | all | PHY+radio |
|---|---|---|
| attach | 21703/25247 = 85.96% | 21888/23761 = 92.12% |

What is left: after its idle TSSI read wl enters the TX power control
through mode 0xc000 and leaves it again, with the offsets cleared, before it
takes hardware control (354 PHY operations from #37063); b43 takes it
straight away.

## The station

The rest of the capture, to 155 s: wl with the MAC up scans channels 1-11
again and again and stays on channel 1, the BSS's. Flow `station` runs the
init and then each of wl's channel switches, the channels its writes to MMIO
0x3f0 give (`LP_SCAN`), as `b43_op_config()` runs them: the MAC suspended,
the channel set, `b43_switch_channel()`, the MAC enabled. NetworkManager
takes the interface down and up at each connection change: where wl's core
resets are followed by a PHY init, the flow runs `op_prepare_structs()` and
the init again, as `b43_wireless_core_init()`. Only the PHY and radio row
counts: the rest of the window is the MAC's traffic.

| capture | PHY+radio |
|---|---|
| attach, station | 53869/61463 = 87.64% |

Every switch lines up with wl's: the tune, the gain table, the TX power
control left and taken again. So do the two downs and ups, where wl and b43
apply the calibrations they keep instead of calibrating. What is left:

- **wl's power control cycle after an up** (438 and 682 operations from
  #59298 and #74208): wl leaves hardware control and takes it again once
  more, with the offsets cleared and written back.

Every run prints, on stderr, the reads past the window (served from b43's
own writes, or 0), the window's reads b43 did not take, b43's messages and
the calls into the core.

## How the harness differs from ../phy_n

- **Reads**: a register the hardware does not change, one whose reads in
  the window differ from wl's last write fewer than three times (its PHY
  resets), is served from b43's own last write once b43 has written it; the
  others, and every register before b43 writes it, from wl's reads in order.
- **The table port** is PHY 0x455 (table id << 10 | offset), 0x457 (high
  half) and 0x456 (low half, which advances the offset); reads are served per
  cell, as `../phy_n` does.
- **The radio** is reached through 0x3f6/0x3fa, and a read of LP-PHY rev 0/1
  addresses register | 0x100: a read the capture does not have is served
  from b43's write to the register without that bit.
- **The board** comes from its SROM dump (`srom.txt`), through ssb-next's
  `sprom_extract_r8()`, which the Makefile cuts out of `ssb-next/pci.c`; the
  PCI subsystem IDs are its words 2 and 3.
- **ChipCommon** is on the bus for `ssbn_pmu_set_ldo_voltage()` and
  `ssbn_pmu_set_ldo_paref()`, through ssb-next's own code.

## Environment

`LP_SROM` (the SROM dump, set by `gates.sh`), `LP_CHANNEL` (1-14, default
1), `LP_SCAN` (the channels of flow `station`, comma separated),
`LP_MAX_POWER` (cfg80211's limit of the channel, dBm, default 20),
`LP_CALSTAT=1` (the calibration statistics on stderr, as `../phy_n` prints
them), `LP_DEBUG=1` (each read past the window on stderr), `LP_FN=1`
(function markers in the trace; the binary must be built with
`make LP_FN=1`).
