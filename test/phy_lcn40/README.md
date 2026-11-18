# test/phy_lcn40 — the LCN40-PHY against wl's bus

`../phy_lcn` for the LCN40-PHY: its Makefile, bus (`trace.c`) and stubs
(`stubs.c`), with this directory's `flows.c`, which compiles `phy_lcn40.c`
and puts a BCM43142 (chip rev 1, core rev 33, LCN40-PHY rev 3, radio 2065
rev 0) on that bus. The capture is the HP 2230's under
`$TRACES/bcm43142-rev1/hp-2230`.

```sh
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to wl's first MAC enable
./lcn40-trace full OPS FIRST LAST [-v | -o TRACE]
```

`gates.sh` needs `$TRACE_TOOLS` for `mmio2ops` and `tracelib`; `GATE_TMP=DIR`
keeps the window's ops, the decoded SROM and b43's trace.

## The capture and the board

`traces/mmio-attach/attach.txt.gz` is an x86 mmiotrace of wl's attach and
`up`, without marks. The board values come from the SROM, rev 10:
`srom.txt`, through `../sprom/decode_r8`, as the `LCN_NVRAM` the harness
reads (`boardflags=0x400201`, `boardflags2=0x800`).

wl writes a PHY register in one 32-bit access to 0x3fc, the address in the
low half and the value in the high one, and in a read-modify-write the value
alone, after the read, as `phy_lcn40.c` does; mmio2ops and `trace.c` print
both as `PHY.WR`.

## From the PHY init through wl's first MAC enable

The window runs from wl's first PHY operation after its ucode upload to the
op before its first MAC enable, after its calibrations: #3395-#38599. b43
runs the PHY calls of its core in its order, as `../phy_lcn` does in flow
`full`. Which channel wl's init tunes is not established yet: `LCN_CHANNEL`
is 1 unless set.

On 2026-11-18, `--min-block 8 --gap-tol 2`:

| capture | raw | PHY+radio |
|---|---|---|
| attach | 724/32999 = 2.19% | 1888/29903 = 6.31% |

b43 follows wl through the radio's switch on and the static tables
(#3395-#5388), then its init stops: what comes next is the TX gain table
(table 7 from 0xc0). The raw line also counts the PHY_VER reads of the posted
writes' flush, which fall where wl's do only once b43 counts its flush as wl
does.

## Environment

`LCN_NVRAM` (the board's nvram), `LCN_CHANNEL` (1-14, default 1), `LCN_DEBUG=1`
(each read past the window on stderr).
