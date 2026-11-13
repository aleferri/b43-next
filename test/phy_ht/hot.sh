#!/bin/sh
# Scores b43's start of the BSS against the R6400 v1's hot captures, one
# window per channel, from the `ssid-N` mark to `down-N`, PHY and radio
# only: flow `ap_start`, as gates.sh does for the cold `ssid`, on every
# 2.4 GHz channel at 20 MHz and on 1l-7l and 5u-11u at 40.
#
#   ./hot.sh --min-block 8 --gap-tol 2 [CAPTURE [SEGMENT...]]
#
# CAPTURE is hot-2g-20 or hot-2g-40, SEGMENT a mark, e.g. ssid-3; without,
# both and all of them. GATE_TMP keeps the traces.
#
# What the configuration before left comes from the capture: its channel
# (ch 6 before the first window, the untraced warm-up), the TX gain index
# of its RX calibration, the table 0x25 pairs the window clears and the
# RC calibration's results, which the warm-up measured and every later
# init writes. wl's 1 s timer runs from the warm-up and restarts at each
# `wl up`; the one value taken from wl's output is its phase, from the
# first turn that measures the temperature.
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
[ -n "$B" ] || { echo "usage: $0 --min-block N --gap-tol M [CAPTURE [SEGMENT...]]"; exit 2; }

BOARD=$TRACES/bcm4331-rev2/r6400v1
export HT_NVRAM=$BOARD/nvram.txt
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT

# "mark first last turns tick0 t25 t25b" for every ssid-N window. The
# timer's ticks count by 1.0093 s on this board, from the last turn before
# an `up` to the up's restart; the first turn with the temperature
# measurement is tick 17 of 30 (b43_phy_ht_op_pwork_1sec()).
windows() {
	awk '
	function tick(t) { return int(t / 1.0093 + 0.5) }
	/ MARK / {
		m = $NF; n = substr($2, 2) + 0
		if (m ~ /^.up-/) { up = $1; restart = 1 }
		if (m ~ /^.ssid-/) { a = n + 1; w = m; turns = 0; nc = 0; set = 0; t25[1] = t25[2] = 0 }
		if (m ~ /^.down-/ && w != "") {
			out[++nw] = w " " a " " (n - 1) " " turns; first[nw] = f
			pair[nw] = t25[1] " " t25[2]; w = ""
		}
		next
	}
	/REG\.RD off=0x04d0/ {
		r = restart ? r + tick($1 - up) : (seen ? r + tick($1 - last) : 0)
		restart = 0; seen = 1; last = $1
		if (w != "" && !turns++) f = r
		next
	}
	/RAD\.RD addr=0x020c / && phase == "" { phase = (16 - r % 30 + 30) % 30 }
	w != "" && /PHY\.WR addr=0x0072 val=0x94/ { off = "0x" substr($NF, 9); next }
	w != "" && off != "" && /PHY\.WR addr=0x0073 / {
		if ($NF == "val=0x0000" && !set && nc < 2) t25[++nc] = off
		else if ($NF != "val=0x0000") set = 1
		off = ""
	}
	END { for (i = 1; i <= nw; i++) print out[i], first[i] + phase, pair[i] }' "$1"
}

rc() { awk -v r="RAD.WR addr=$2 " 'index($0, r) { print $NF; exit }' "$1" | sed 's/val=//'; }

caps="hot-2g-20 hot-2g-40"
case ${1:-} in hot-2g-*) caps=$1; shift ;; esac
for c in $caps; do
	ops=$T/$c.ops
	zcat "$BOARD"/traces/mmio-*/"$c-ops.txt.gz" > "$ops"
	grep -E ' (PHY|RAD)\.' "$ops" > "$T/$c.phy.ops"
	rc0=$(rc "$ops" 0x0cc2) rc1=$(rc "$ops" 0x0cc3) rc2=$(rc "$ops" 0x0ca1)
	before=6 gain=40
	windows "$ops" | while read -r mark a z turns tick0 t25 t25b; do
		seg=$(echo "$mark" | tr -d "'")
		ch=$(echo "$seg" | sed 's/ssid-\([0-9]*\).*/\1/')
		if [ $# -eq 0 ] || echo " $* " | grep -q " $seg "; then
			case $seg in
			*l) center=$((ch + 2)) ;;
			*u) center=$((ch - 2)) ;;
			*) center=0 ;;
			esac
			echo "== $c $seg, ops $a:$z"
			HT_CHANNEL=$ch HT_CENTER=$center HT_PREV_CHANNEL=$before \
				HT_TX_GAIN_IDX=$gain HT_WD_TURNS=$turns \
				HT_WD_TICK0=$tick0 HT_RCCAL0=$rc0 \
				HT_RCCAL1=$rc1 HT_RCCAL2=$rc2 HT_T25=$t25 HT_T25B=$t25b \
				./ht-trace ap_start "$ops" "$a" "$z" -o "$T/$c.$seg.trace" 2>/dev/null
			grep -E ' (PHY|RAD)\.' "$T/$c.$seg.trace" > "$T/$c.$seg.phy.trace"
			python3 ../unit/cmp_skip.py "$T/$c.phy.ops" "$T/$c.$seg.phy.trace" \
				"$a:$z" --board r6400v1 --bus $B | grep -A2 '^grezzo' |
				sed '1s/^grezzo   /PHY+radio/'
		fi
		before=$ch gain=10
	done
done
