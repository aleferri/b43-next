#!/bin/sh
# b43's whole bring-up on the Compaq CQ57's BCM4313 (LCN-PHY), its core
# included, against the board's attach capture, an x86 mmiotrace of wl's
# attach and up: probe, up and the station's interface. The station's
# events (scans, associations) are not replayed yet: pipeline.timeline reads
# an AP's. The firmware is
# wl's, from the same capture; the SROM is the board's, decoded as an NVRAM.
#
#   ./cq57.sh
#
# GATE_TMP keeps the working files.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
. "$REPO/test/paths.sh"

cd "$HERE"

BOARD=$TRACES/bcm4313-rev1/compaq-cq57
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT
b43trace() { PYTHONPATH=$TRACE_TOOLS/src python3 -m "b43trace.$@"; }

make -s -C ../sprom decode_r8
../sprom/decode_r8 "$BOARD/srom.txt" > "$T/nvram.txt"
zcat "$BOARD/traces/mmio-attach/attach.txt.gz" > "$T/cap.txt"
b43trace bus.mmio2ops "$T/cap.txt" -o "$T/cap.m2o" 2>/dev/null
b43trace bus.fw_from_capture "$T/cap.txt" --format mmiotrace --rev 24 \
	-o "$T/fw" 2>/dev/null

make -s BCM63XX=0 b43-trace >/dev/null 2>&1
B43_BOARD=cq57 B43_NVRAM=$T/nvram.txt B43_FW_DIR=$T/fw \
	B43_READ_ORACLE=$T/cap.m2o B43_CHANNEL=1 \
	B43_TRACE_OUT=$T/b43.trace ./b43-trace 2>"$T/b43.log"
