#!/bin/sh
# Scores b43's LP-PHY against the HP 550's BCM4312 attach capture, from the
# PHY init to the first MAC enable, as ../phy_n/gates.sh does for the N-PHY.
# The capture is the raw mmiotrace: mmio2ops of $TRACE_TOOLS decodes it. The
# window runs from wl's first PHY operation after its MAC control write
# 0x44020402 to the operation before the first write that enables the MAC.
# b43 runs b43_phy_init() on channel 1, the channel wl leaves the init on,
# its reads served from the window, and ../unit/cmp_skip.py --bus --rmw
# scores the two streams.
#
#   ./gates.sh --min-block 8 --gap-tol 2
#
# GATE_TMP keeps the window's ops and b43's trace. Two rows: all the
# operations, and the PHY's and radio's only, the perimeter of this harness,
# which compiles no core.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
. "$REPO/test/paths.sh"
cd "$(dirname "$0")"

B=
while [ $# -gt 0 ]; do
	case $1 in
	--min-block|--gap-tol) B="$B $1 $2"; shift 2 ;;
	*) break ;;
	esac
done
[ -n "$B" ] || { echo "usage: $0 --min-block N --gap-tol M"; exit 2; }

BOARD=$TRACES/bcm4312-rev1/hp-550
export LP_SROM=$BOARD/srom.txt
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT

ops=$T/attach.ops
zcat "$BOARD/traces/mmio-attach/attach.txt.gz" > "$T/attach.txt"
PYTHONPATH=$TRACE_TOOLS/src python3 -m b43trace.bus.mmio2ops "$T/attach.txt" \
	-o "$ops" 2>/dev/null
first=$(awk '/MAC\.MCTRL val=0x44020402$/ { u = 1; next } u && / PHY\./ { print substr($2, 2); exit }' "$ops")
last=$(awk -v f="$first" 'substr($2, 2) + 0 > f && /MAC\.MCTRL val=0x[0-9a-f]*[13579bdf]$/ { print substr($2, 2) - 1; exit }' "$ops")
echo "== attach, ops $first:$last, channel 1"
LP_CHANNEL=1 ./lp-trace full "$ops" "$first" "$last" -o "$T/attach.trace"
python3 ../unit/cmp_skip.py "$ops" "$T/attach.trace" "$first:$last" \
	--board hp-550 --bus --rmw $B | grep -A2 '^grezzo'
grep -E ' (PHY|RAD)\.' "$ops" > "$T/attach.phy.ops"
grep -E ' (PHY|RAD)\.' "$T/attach.trace" > "$T/attach.phy.trace"
python3 ../unit/cmp_skip.py "$T/attach.phy.ops" "$T/attach.phy.trace" \
	"$first:$last" --board hp-550 --bus --rmw $B | grep -A2 '^grezzo' |
	sed '1s/^grezzo   /PHY+radio/'
