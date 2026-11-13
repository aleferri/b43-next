#!/bin/sh
# Runs the TX power control init of b43_phy_ht_op_init() against every
# HT-PHY init of the R6400 v1 captures. The window of an init runs from its
# first table 0x1a read (b43_phy_ht_tx_power_fix()) to the write that turns
# the control on.
#
#   ./check.sh [-v]
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
. "$REPO/test/paths.sh"
cd "$(dirname "$0")"

BOARD=$TRACES/bcm4331-rev2/r6400v1
export HT_NVRAM=$BOARD/nvram.txt
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# The windows of one capture, "first last mark": a power fix followed by
# the control turned off.
windows() {
	awk '
	/ MARK / { mark = $NF; next }
	start && ++k == 33 && !/PHY\.RD addr=0x01e7 / { start = 0 }
	/PHY\.WR addr=0x0072 val=0x68[0-9a-f][0-9a-f]$/ && !start {
		start = substr($2, 2); m = mark; k = 0; next
	}
	start && /PHY\.WR addr=0x01e7 val=0x[89a-f]/ {
		print start, substr($2, 2), m; start = 0
	}' "$1"
}

for f in "$BOARD"/traces/mmio-*/*-ops.txt.gz; do
	ops=$T/$(basename "$f" .gz)
	zcat "$f" > "$ops"
	# What the window before left: its channel (ch 6 before the first of
	# the hot cycle, its untraced warm-up) and, after a BSS start, the TX
	# gain index of its RX calibration
	before=6 gain=40
	windows "$ops" | while read -r first last mark; do
		printf '%-18s %-14s ' "$(basename "$ops" -ops.txt)" "$mark"
		# A later init starts from the target the configuration before left;
		later=1; [ "$mark" = "'up'" ] && later=0
		ch=$(echo "$(basename "$ops")$mark" |
			sed -n "s/^cold-2g\([0-9]*\).*/\1/p; s/^hot.*ssid-\([0-9]*\).*/\1/p")
		HT_LATER=$later HT_CHANNEL=$ch HT_PREV_CHANNEL=$before \
			HT_TX_GAIN_IDX=$gain \
			./ht-trace pctl_init "$ops" "$first" "$last" "$@" || echo FAIL > "$T/failed"
		before=$ch
		case $mark in *ssid*) gain=10 ;; esac
	done
	rm -f "$ops"
done
[ -f "$T/failed" ] && exit 1
exit 0
