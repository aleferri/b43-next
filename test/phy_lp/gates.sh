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
# which compiles no core. Then the station: the same init and wl's channel
# switches after it, to the end of the capture, PHY and radio only.
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
# score_phy TRACE FIRST LAST: the PHY's and radio's operations only
score_phy() {
	grep -E ' (PHY|RAD)\.' "$1" > "$1.phy"
	python3 ../unit/cmp_skip.py "$ops.phy" "$1.phy" "$2:$3" \
		--board hp-550 --bus --rmw $B | grep -A2 '^grezzo' |
		sed '1s/^grezzo   /PHY+radio/'
}
grep -E ' (PHY|RAD)\.' "$ops" > "$ops.phy"

echo "== attach, ops $first:$last, channel 1"
LP_CHANNEL=1 ./lp-trace full "$ops" "$first" "$last" -o "$T/attach.trace"
python3 ../unit/cmp_skip.py "$ops" "$T/attach.trace" "$first:$last" \
	--board hp-550 --bus --rmw $B | grep -A2 '^grezzo'
score_phy "$T/attach.trace" "$first" "$last"

# The station: the rest of the capture, wl's channel switches with the MAC
# up (its writes of the channel to MMIO 0x3f0), the scans and the BSS's
# channel, run as b43_op_config() runs them, and NetworkManager's interface
# down and up at each connection change: core resets (TMSLOW 0x00030001)
# and a PHY init, whose tune is the init's own (channel 0 in the list).
# PHY and radio only: the rest of the window is the MAC's traffic.
end=$(tail -1 "$ops" | awk '{ print substr($2, 2) }')
scan=$(awk -v l="$last" '
	function hex(s,  i, n) {
		n = 0
		s = tolower(substr(s, 3))
		for (i = 1; i <= length(s); i++)
			n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
		return n
	}
	substr($2, 2) + 0 <= l { next }
	/ REG\.WR off=0x0f98 val=0x00030001$/ { reset = 1 }
	/ PHY\.WR addr=0x0455 val=0x0800$/ && reset {
		printf "%s0", sep; sep = ","; reset = 0; own = 1
	}
	/ REG\.WR off=0x03f0 / {
		if (own) { own = 0; next }
		split($6, a, "="); printf "%s%d", sep, hex(a[2]); sep = ","
	}' "$ops")
echo "== station, ops $first:$end, $(echo "$scan" | tr ',' '\n' | grep -vc '^0$') switches, $(echo "$scan" | tr ',' '\n' | grep -c '^0$') downs and ups"
LP_CHANNEL=1 LP_SCAN=$scan ./lp-trace station "$ops" "$first" "$end" \
	-o "$T/station.trace"
score_phy "$T/station.trace" "$first" "$end"
