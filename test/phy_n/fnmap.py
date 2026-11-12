#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""b43's functions against wl's ops: where each call lands in the capture.

    fnmap.py OPS TRACE FIRST:LAST [--depth N]

TRACE is a harness trace taken with N_FN=1 (built with `make N_FN=1`): the
PHY and radio ops with ----FN:name---- markers. Each call down to --depth
(default 3) is printed with its op count and, where its ops are in an equal
block of the alignment with wl's PHY and radio ops, the #numbers of wl's ops
they match; '-' when none of them matches.
"""
import os
import re
import sys

sys.path.insert(0, os.environ.get('TRACE_TOOLS', '../../../b43-next-trace-tools'))
import tracelib as tl  # noqa: E402

PHY = re.compile(r'(PHY|RAD)\.')


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    lo, hi = (int(x) for x in sys.argv[3].split(':'))
    depth = int(sys.argv[sys.argv.index('--depth') + 1]) if '--depth' in sys.argv else 3
    v = [o for o in tl.read_vendor(sys.argv[1]) if lo <= o[0] <= hi and PHY.match(o[3])]
    vn, vnum = [o[3] for o in v], [o[0] for o in v]
    ops, calls, stack = [], [], []
    for line in open(sys.argv[2]):
        line = line.strip()
        m = tl.RE_MARKER.match(line)
        if m:
            if m.group(1):
                if stack:
                    c = stack.pop()
                    c[3] = len(ops)
            else:
                c = [m.group(3), len(stack), len(ops), None]
                stack.append(c)
                calls.append(c)
            continue
        op = line[5:] if line.startswith('cpu0 ') else line
        if PHY.match(op):
            ops.append(tl.norm(op))
    match = {}
    for k, a, b, c, d in tl.align_opcodes(vn, ops):
        if k == 'equal':
            for i in range(b - a):
                match[c + i] = vnum[a + i]
    for name, dep, s, e in calls:
        e = len(ops) if e is None else e
        if dep >= depth or e == s:
            continue
        hit = [match[i] for i in range(s, e) if i in match]
        where = f"#{hit[0]}-#{hit[-1]} ({len(hit)}/{e - s})" if hit else '-'
        print(f"{'  ' * dep}{name}  ops {s}-{e}  wl {where}")


if __name__ == '__main__':
    main()
