#!/bin/sh
# Scores b43's LCN40-PHY against the HP 2230's BCM43142 attach capture, from
# the PHY init through wl's first MAC enable, as ../phy_lcn/gates.sh does for
# the LCN-PHY: the capture decoded by mmio2ops, the board values from its
# SROM through ../sprom/decode_r8, b43_phy_init() run with its reads served
# from the window, ../unit/cmp_skip.py --bus --rmw on the two streams.
#
#   ./gates.sh --min-block 8 --gap-tol 2
#
# The window runs from wl's first PHY operation after its ucode upload to
# the op before its first MAC enable. GATE_TMP keeps the window's ops, the
# decoded SROM and b43's trace.
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

BOARD=$TRACES/bcm43142-rev1/hp-2230
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT

make -s -C ../sprom decode_r8 || exit 2
../sprom/decode_r8 "$BOARD/srom.txt" > "$T/nvram.txt" || exit 2
export LCN_NVRAM=$T/nvram.txt

ops=$T/attach.ops
zcat "$BOARD/traces/mmio-attach/attach.txt.gz" > "$T/attach.txt"
PYTHONPATH=$TRACE_TOOLS/src python3 -m b43trace.bus.mmio2ops "$T/attach.txt" \
	-o "$ops" 2>/dev/null || exit 2

first=$(awk '/ OBJ\.BULKW .* sel=0x0$/ { u = 1 }
	u && / (PHY|RAD)\./ { print substr($2, 2); exit }' "$ops")
last=$(awk -v i="$first" '{ n = substr($2, 2) + 0 }
	n > i && / MAC\.MCTRL val=0x[0-9a-f]*[13579bdf] *$/ { print n - 1; exit }' "$ops")

echo "== attach, ops $first:$last, channel ${LCN_CHANNEL:-1}, 20 MHz"
./lcn40-trace full "$ops" "$first" "$last" -o "$T/attach.trace"
python3 ../unit/cmp_skip.py "$ops" "$T/attach.trace" "$first:$last" \
	--board hp-2230 --bus --rmw $B | grep -A2 '^raw '
grep -E ' (PHY|RAD)\.' "$ops" > "$T/attach.phy.ops"
grep -E ' (PHY|RAD)\.' "$T/attach.trace" > "$T/attach.phy.trace"
python3 ../unit/cmp_skip.py "$T/attach.phy.ops" "$T/attach.phy.trace" \
	"$first:$last" --board hp-2230 --bus --rmw $B | grep -A2 '^raw ' |
	sed '1s/^raw      /PHY+radio/'
