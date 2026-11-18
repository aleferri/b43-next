#!/bin/sh
# Scores b43's N-PHY against the RT-AC56U's BCM43217 cold captures, from the
# PHY init through `wl up`, as ../phy_ht/gates.sh does for the HT-PHY. The
# window runs from wl's first PHY operation after the `up` mark to the `ssid`
# mark. b43 runs b43_phy_init() on the capture's channel, its reads served
# from the window, and ../unit/cmp_skip.py --bus --rmw scores the two
# streams.
#
#   ./gates.sh --min-block 8 --gap-tol 2 [CAPTURE...]
#
# CAPTURE is a name under traces/mmio-cold, e.g. cold-2g6-20; without, the
# three 20 MHz ones; cold-2g1l-40, cold-2g6l-40 and cold-2g11u-40 run the
# flow at 40 MHz on their control channel and sideband. GATE_TMP keeps the
# window's trace and b43's. Each capture gets two rows: all the operations,
# and the PHY's and radio's only, the perimeter of this harness, which
# compiles no core; then the noise samples after the BSS start (flow noise).
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
[ -n "$B" ] || { echo "usage: $0 --min-block N --gap-tol M [CAPTURE...]"; exit 2; }

BOARD=$TRACES/bcm43217-rev0/rt-ac56u
export N_NVRAM=$BOARD/nvram.txt
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT

[ $# -gt 0 ] || set -- cold-2g1-20 cold-2g6-20 cold-2g11-20
for c in "$@"; do
	ops=$T/$c.ops
	zcat "$BOARD/traces/mmio-cold/$c.bin.gz" > "$T/$c.bin"
	b43trace bus.mmio2ops "$T/$c.bin" -o "$ops" 2>/dev/null
	first=$(awk "/MARK 'up'/ { u = 1; next } u && / PHY\./ { print substr(\$2, 2); exit }" "$ops")
	last=$(awk "/MARK 'ssid'/ { print substr(\$2, 2) - 1; exit }" "$ops")
	ch=$(echo "$c" | sed 's/cold-2g\([0-9]*\).*/\1/')
	# cold-2g6l-40: 40 MHz, the control channel the lower one; 11u the upper
	case $c in
	*l-40) bw=40 sb=1 ;;
	*u-40) bw=40 sb=-1 ;;
	*) bw=20 sb=0 ;;
	esac
	echo "== $c, ops $first:$last, channel $ch, $bw MHz"
	N_CHANNEL=$ch N_BW=$bw N_SB=$sb ./n-trace full "$ops" "$first" "$last" \
		-o "$T/$c.trace" || exit 1
	python3 ../unit/cmp_skip.py "$ops" "$T/$c.trace" "$first:$last" \
		--board rt-ac56u --bus --rmw $B | grep -A2 '^raw '
	grep -E ' (PHY|RAD)\.' "$ops" > "$T/$c.phy.ops"
	grep -E ' (PHY|RAD)\.' "$T/$c.trace" > "$T/$c.phy.trace"
	python3 ../unit/cmp_skip.py "$T/$c.phy.ops" "$T/$c.phy.trace" \
		"$first:$last" --board rt-ac56u --bus --rmw $B | grep -A2 '^raw ' |
		sed '1s/^raw      /PHY+radio/'
	# The noise samples wl takes after the BSS start, through the flow noise
	first=$(awk "/MARK 'bss-up'/ { print substr(\$2, 2) + 1; exit }" "$ops")
	last=$(awk "/MARK 'done'/ { print substr(\$2, 2) - 1; exit }" "$ops")
	res=$(N_CHANNEL=$ch N_BW=$bw N_SB=$sb ./n-trace noise "$ops" "$first" \
		"$last" -o "$T/$c.noise" 2>&1 | grep '^noise:')
	awk -v f="$first" -v l="$last" '{ n = substr($2, 2) + 0 }
		n >= f && n <= l && /OBJ\.RD addr=0x030[8ace]/ { print "cpu0", $4, $5, $6, $7 }' \
		"$ops" | head -n "$(wc -l < "$T/$c.noise")" > "$T/$c.noise.wl"
	if cmp -s "$T/$c.noise" "$T/$c.noise.wl"; then same="wl's"; else same="not wl's"; fi
	echo "$res; reads $same"
done
