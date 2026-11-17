#!/bin/sh
# Scores b43's LCN-PHY against the Compaq CQ57's BCM4313 attach capture, from
# the PHY init through wl's first MAC enable, as ../phy_n/gates.sh does for
# the N-PHY. The capture is an x86 mmiotrace, decoded here by mmio2ops.py;
# the board values are its SROM, decoded by ../sprom/decode_r8 with the
# tree's rev 8 extractor. b43 runs b43_phy_init() on channel 1, its reads
# served from the window, and ../unit/cmp_skip.py --bus --rmw scores the two
# streams.
#
#   ./gates.sh --min-block 8 --gap-tol 2
#
# The window has no marks to start from: it runs from the first of wl's three
# analog switches before the PHY init (two core resets and the init's own,
# the three b43 makes in flow full) to the op before wl's first MAC enable,
# after the calibrations. GATE_TMP keeps the window's ops and b43's trace.
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

BOARD=$TRACES/bcm4313-rev1/compaq-cq57
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

init=$(awk '/ PHY\.WR addr=0x06d0 / { print substr($2, 2); exit }' "$ops")
first=$(awk -v i="$init" '{ n = substr($2, 2) + 0 } n >= i { exit }
	prev && / PHY\.WR addr=0x043b / { a[k++] = prev }
	{ prev = / PHY\.RD addr=0x043b / ? n : 0 }
	END { print a[k - 3] }' "$ops")
last=$(awk -v i="$init" '{ n = substr($2, 2) + 0 }
	n > i && / MAC\.MCTRL val=0x[0-9a-f]*[13579bdf] *$/ { print n - 1; exit }' "$ops")

# wl's attach tunes channel 1: 0x657 = 965, channel 1's SFO
echo "== attach, ops $first:$last, channel 1, 20 MHz"
LCN_CHANNEL=1 ./lcn-trace full "$ops" "$first" "$last" -o "$T/attach.trace"
python3 ../unit/cmp_skip.py "$ops" "$T/attach.trace" "$first:$last" \
	--board compaq-cq57 --bus --rmw $B | grep -A2 '^grezzo'
grep -E ' (PHY|RAD)\.' "$ops" > "$T/attach.phy.ops"
grep -E ' (PHY|RAD)\.' "$T/attach.trace" > "$T/attach.phy.trace"
python3 ../unit/cmp_skip.py "$T/attach.phy.ops" "$T/attach.phy.trace" \
	"$first:$last" --board compaq-cq57 --bus --rmw $B | grep -A2 '^grezzo' |
	sed '1s/^grezzo   /PHY+radio/'

# The channel switches of wl's second scan, every 2.4 GHz channel but 14,
# with the init's calibration results as wl holds them (calres.py): each
# window from the switch's first PHY operation (0x780) to the op before the
# MAC is enabled again.
python3 - "$ops" > "$T/switches" <<'PY'
import re, sys
sfo = {965 + 2 * c: c + 1 for c in range(13)}
tunes = start = 0
for line in open(sys.argv[1]):
    f = line.split()
    if len(f) < 5:
        continue
    n = int(f[1][1:])
    if ' PHY.WR addr=0x0657 ' in line:
        tunes += 1
        if tunes > 14:
            ch = sfo[int(f[5].split('=')[1], 16)]
    if tunes < 14:
        continue
    if ' PHY.RD addr=0x0780 ' in line and not start:
        start = n
    if tunes > 14 and start and re.search(r'MAC\.MCTRL val=0x\w*[13579bdf]\s*$', line):
        print(start, n - 1, ch)
        start = 0
        if ch == 13:
            break
PY
printf '\n%-8s %-14s %s\n' channel ops PHY+radio
while read first last ch; do
	env $(python3 calres.py "$ops" "$first") LCN_CHANNEL=$ch \
		./lcn-trace switch "$ops" "$first" "$last" -o "$T/sw$ch.trace" 2>/dev/null
	grep -E ' (PHY|RAD)\.' "$T/sw$ch.trace" > "$T/sw$ch.phy.trace"
	printf '%-8s %-14s %s\n' "$ch" "$first:$last" "$(python3 ../unit/cmp_skip.py \
		"$T/attach.phy.ops" "$T/sw$ch.phy.trace" "$first:$last" \
		--board compaq-cq57 --bus --rmw $B | sed -n 's/^grezzo *: //p' |
		cut -d' ' -f1-3)"
done < "$T/switches"
