#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""The regions where b43's PHY and radio ops leave wl's, largest first.

    regions.py OPS TRACE FIRST:LAST [--min N]

OPS is the capture's mmio2ops output, TRACE the harness's (-o), FIRST:LAST
the window's #numbers, as gates.sh passes them (GATE_TMP keeps both files).
Only the PHY.* and RAD.* ops are compared, aligned with
$TRACE_TOOLS/tracelib.align_opcodes(). Each region is printed with its size
on both sides, its position in wl's window and its first op on each side.
"""
import os
import re
import sys

sys.path.insert(0, os.environ.get('TRACE_TOOLS', '../../../b43-next-trace-tools'))
import tracelib as tl  # noqa: E402


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    lo, hi = (int(x) for x in sys.argv[3].split(':'))
    least = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 10
    phy = re.compile(r'(PHY|RAD)\.')
    v = [o for o in tl.read_vendor(sys.argv[1]) if lo <= o[0] <= hi]
    vn = [o[3] for o in v if phy.match(o[3])]
    vnum = [o[0] for o in v if phy.match(o[3])]
    t = [x for x in tl.read_port(sys.argv[2]) if phy.match(x)]
    rows = []
    for k, a, b, c, d in tl.align_opcodes(vn, t):
        if k == 'equal' or max(b - a, d - c) < least:
            continue
        rows.append((max(b - a, d - c), k, a, b, c, d))
    for _, k, a, b, c, d in sorted(rows, reverse=True):
        at = f"#{vnum[a]}" if a < len(vnum) else "end"
        print(f"{k:7s} wl {b - a:6d}  b43 {d - c:5d}  at {at:7s} "
              f"wl: {vn[a] if a < b else '-':34s} b43: {t[c] if c < d else '-'}")


if __name__ == '__main__':
    main()
