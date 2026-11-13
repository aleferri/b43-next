#!/bin/sh
# b43's whole bring-up on the R6400 v1's BCM4331 (HT-PHY), its core
# included, against one of the board's cold captures: probe, up, the AP's
# configuration and its start, then the capture's events. The capture is
# decoded with wl's flush reads and without folding its bulk writes, as b43
# makes them one by one; the firmware is wl's, from the same capture.
#
#   ./r6400.sh [CAPTURE]      e.g. cold-2g6-20, the default
#
# Prints the score of the window from `up` to `beaconing`, all the operations
# and the PHY's and radio's only. GATE_TMP keeps the working files.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
. "$REPO/test/paths.sh"
cd "$HERE"

CAP=${1:-cold-2g6-20}
BOARD=$TRACES/bcm4331-rev2/r6400v1
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT
b43trace() { PYTHONPATH=$TRACE_TOOLS/src python3 -m "b43trace.$@"; }

zcat "$BOARD/traces/mmio-cold/$CAP.bin.gz" > "$T/cap.bin"
b43trace bus.mmio2ops "$T/cap.bin" --format wl-mmio-trap --keep-flush \
	--no-bulk -o "$T/cap.m2o" 2>/dev/null
b43trace bus.fw_from_capture "$T/cap.bin" --rev 29 -o "$T/fw" 2>/dev/null
b43trace pipeline.timeline "$T/cap.m2o" "$T/cap.tl" >/dev/null

make -s BCM63XX=0 b43-trace >/dev/null 2>&1
ch=$(echo "$CAP" | sed 's/cold-2g\([0-9]*\).*/\1/')
B43_BOARD=r6400v1 B43_NVRAM=$BOARD/nvram.txt B43_FW_DIR=$T/fw \
	B43_READ_ORACLE=$T/cap.m2o B43_TIMELINE=$T/cap.tl B43_CHANNEL=$ch \
	B43_TRACE_OUT=$T/b43.trace ./b43-trace 2>"$T/b43.log"

a=$(awk "/MARK 'up'/ { print substr(\$2, 2) + 1; exit }" "$T/cap.m2o")
z=$(awk "/MARK 'beaconing'/ { print substr(\$2, 2) - 1; exit }" "$T/cap.m2o")
echo "== $CAP, ops $a:$z"
python3 ../unit/cmp_skip.py "$T/cap.m2o" "$T/b43.trace" "$a:$z" \
	--board r6400v1 --bus --min-block 8 --gap-tol 2 | grep -A2 '^grezzo'
grep -E ' (PHY|RAD)\.' "$T/cap.m2o" > "$T/cap.phy"
grep -E ' (PHY|RAD)\.' "$T/b43.trace" > "$T/b43.phy"
python3 ../unit/cmp_skip.py "$T/cap.phy" "$T/b43.phy" "$a:$z" \
	--board r6400v1 --bus --min-block 8 --gap-tol 2 | grep -A2 '^grezzo' |
	sed '1s/^grezzo   /PHY+radio/'
