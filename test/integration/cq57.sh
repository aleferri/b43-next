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
# Prints the score of the window from the capture's start to the op before
# wl's first MAC enable, after its calibrations; b43's trace is cut at its
# own first enable after the PAPD calibration. Both scores, all the
# operations and the PHY's, radio's and MAC's (PHYSEL below), as r6400.sh.
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
b43trace bus.mmio2ops "$T/cap.txt" --no-bulk -o "$T/cap.m2o" 2>/dev/null
b43trace bus.fw_from_capture "$T/cap.txt" --format mmiotrace --rev 24 \
	-o "$T/fw" 2>/dev/null

make -s BCM63XX=0 b43-trace >/dev/null 2>&1
B43_BOARD=cq57 B43_NVRAM=$T/nvram.txt B43_FW_DIR=$T/fw \
	B43_READ_ORACLE=$T/cap.m2o B43_CHANNEL=1 \
	B43_TRACE_OUT=$T/b43.trace ./b43-trace 2>"$T/b43.log"

PHYSEL=' (PHY|RAD)\.| MAC\.MCTRL | WRAP\.(RD|WR) +off=0x0408 '
z=$(awk '/ MAC\.MCTRL val=0x[0-9a-f]*[13579bdf] *$/ { print substr($2, 2) - 1; exit }' \
	"$T/cap.m2o")
papd=$(grep -n 'addr=0x0676' "$T/b43.trace" | tail -1 | cut -d: -f1)
awk -v p="$papd" 'NR > p && / MAC\.MCTRL val=0x[0-9a-f]*[13579bdf] *$/ { exit }
	{ print }' "$T/b43.trace" > "$T/b43.cut"
echo "== attach, ops 1:$z"
python3 ../unit/cmp_skip.py "$T/cap.m2o" "$T/b43.cut" "1:$z" \
	--board compaq-cq57 --bus --rmw --min-block 8 --gap-tol 2 2>/dev/null |
	grep -A2 '^grezzo'
grep -E "$PHYSEL" "$T/cap.m2o" > "$T/cap.phy"
grep -E "$PHYSEL" "$T/b43.cut" > "$T/b43.phy"
python3 ../unit/cmp_skip.py "$T/cap.phy" "$T/b43.phy" "1:$z" \
	--board compaq-cq57 --bus --rmw --min-block 8 --gap-tol 2 2>/dev/null |
	grep -A2 '^grezzo' | sed '1s/^grezzo       /PHY+radio+MAC/'
