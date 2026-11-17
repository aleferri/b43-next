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
| attach | 2304/20375 = 11.31% | 2299/17688 = 13.00% |

b43 prints two messages, the TX gain table and the switch control table it
has none of for a board without a FEM, and makes 4191 operations to wl's
20375. In wl's order, what b43 does not make:

- after the static tables, which are wl's to the value: the RF sequencer
  entries, the TX gain table of the internal PA, the switch control table
  and the RF power table (#3260-#5247);
- in the baseband init, a TX gain set by index (#5647-#5702);
- sixteen radio writes at the end of the 2064's init (#6016-#6031), and
  what wl does between it and the channel setup (#6032-#7992);
- in the channel setup, the 2064's PLL steps b43 leaves as TODO
  (#8054-#8095), and the CCK and OFDM filters, whose coefficients differ
  from b43's;
- everything after the channel setup: 11169 PHY and radio operations from
  #8206.

## Flows

- `full`: above.

## Environment

`LCN_NVRAM` (the board's nvram), `LCN_CHANNEL` (1-14, default 1), `LCN_DEBUG=1`
(each read past the window on stderr).
