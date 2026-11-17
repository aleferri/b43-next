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

# The steps after the init, as windows of their own: wl's channel switches
# with the MAC up, from its MAC suspend to its MAC enable around each write
# of the channel to MMIO 0x3f0 (the scans and the BSS's channel), up to the
# interface's first down and up, and that up's PHY init (core resets,
# TMSLOW 0x00030001, then a PHY init from its first table write to the MAC
# enable). Flow station runs them as b43_op_config() and
# b43_wireless_core_init() do and marks where each begins; each row scores
# b43's steps against wl's windows, PHY and radio only.
awk -v l="$last" '
	function hex(s,  i, n) {
		n = 0
		s = tolower(substr(s, 3))
		for (i = 1; i <= length(s); i++)
			n = n * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
		return n
	}
	{ n = substr($2, 2) + 0 }
	n <= l { next }
	/ MAC\.MCTRL / {
		if ($5 ~ /[02468ace]$/) { susp = n; next }
		if (open) { print kind, start, n - 1, ch; open = 0 }
		next
	}
	/ REG\.WR off=0x0f98 val=0x00030001$/ { reset = 1; next }
	/ PHY\.WR addr=0x0455 val=0x0800$/ && reset {
		reset = 0; own = 1; kind = "U"; start = n; ch = 0; open = 1
		next
	}
	/ REG\.WR off=0x03f0 / {
		if (own) { own = 0; next }
		split($6, a, "="); kind = "S"; start = susp; ch = hex(a[2])
		open = 1
	}' "$ops" > "$T/steps"
# The switches before the first up, and that up
awk '$1 == "U" { print; exit } { print }' "$T/steps" > "$T/steps.run"
scan=$(awk '{ printf "%s%d", sep, $4; sep = "," }' "$T/steps.run")
end_run=$(awk 'END { print $3 }' "$T/steps.run")
steps=$(awk '{ printf "%s%d", sep, $2; sep = "," }' "$T/steps.run")
LP_CHANNEL=1 LP_SCAN=$scan LP_STEPS=$steps \
	./lp-trace station "$ops" "$first" "$end_run" \
	-o "$T/station.trace" 2>/dev/null

# score_steps NAME KIND MARKER: wl's windows of KIND, b43's steps after MARKER
score_steps() {
	awk -v k="$2" '$1 == k { print $2, $3 }' "$T/steps.run" > "$T/$1.win"
	awk 'NR == FNR { s[NR] = $1; e[NR] = $2; w = NR; next }
	     / (PHY|RAD)\./ {
		n = substr($2, 2) + 0
		for (i = 1; i <= w; i++)
			if (n >= s[i] && n <= e[i]) { print; next }
	     }' "$T/$1.win" "$ops" > "$T/$1.ops"
	awk -v m="----$3----" '/^----(SWITCH|UP)----$/ { on = $0 == m; next }
	     on && / (PHY|RAD)\./' "$T/station.trace" > "$T/$1.trace"
	lo=$(awk 'NR == 1 { print $1 }' "$T/$1.win")
	hi=$(awk 'END { print $2 }' "$T/$1.win")
	echo "== $1, $(wc -l < "$T/$1.win") windows of ops $lo:$hi"
	python3 ../unit/cmp_skip.py "$T/$1.ops" "$T/$1.trace" "$lo:$hi" \
		--board hp-550 --bus --rmw $B | grep -A2 '^grezzo' |
		sed '1s/^grezzo   /PHY+radio/'
}
score_steps switches S SWITCH
score_steps up U UP
