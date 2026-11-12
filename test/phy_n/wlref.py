#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""wl's calibration results and TX power values, in the report's format.

    wlref.py OPS FIRST:LAST

OPS is a capture's mmio2ops output and FIRST:LAST a window of it, e.g. the
`up` window gates.sh uses. For each key of b43's calibration report
(`calreport=1`, `N_CALREPORT=1` in the harness) it prints the value wl left
in the window: the last value written (or read back, for the read-only
power control status), so the two can be diffed line by line. The values
wl only computes in its software state (the pre-calibration deltas, the
idle TSSI it keeps) are taken from the operations that carry them.
"""
import re
import sys

OP = re.compile(r'#(\d+)\s+cpu\d+\s+(\S+)\s+(?:addr|off)=0x([0-9a-f]+)\s+val=0x([0-9a-f]+)')

RSSI_PHY = (0x1a6, 0x1ac, 0x1b2, 0x1b8, 0x1a4, 0x1aa, 0x1b0, 0x1b6,
            0x1a5, 0x1ab, 0x1b1, 0x1b7)


def s6(v):
    v &= 0x3f
    return v - 64 if v > 31 else v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    lo, hi = (int(x) for x in sys.argv[2].split(':'))
    phy, rad, cc = {}, {}, {}
    tbl = {}
    t = off = 0
    pmu_addr = None
    pmu = {}
    gain = (0, 0)
    bb = None
    rounds = []
    cur = None
    first_stat = {}
    t7_after = []
    for line in open(sys.argv[1]):
        m = OP.search(line)
        if not m:
            continue
        n = int(m.group(1))
        if n < lo or n > hi:
            continue
        cls, a, v = m.group(2), int(m.group(3), 16), int(m.group(4), 16)
        if cls in ('PHY.WR', 'PHY.RD'):
            if a == 0x72 and cls == 'PHY.WR':
                t, off = v >> 10, v & 0x3ff
                continue
            if a == 0x73:
                if cls == 'PHY.WR':
                    tbl[(t, off)] = v
                    if t == 7 and off == 0x110:
                        t7_after.append((len(rounds), v))
                    if t == 15 and off == 0x57:
                        bb = v
                    if t == 17 and off == 0:
                        cur = {'g': None, 'bb': None, 'r': []}
                        rounds.append(cur)
                off += 1
                continue
            if cls == 'PHY.WR' or a in (0x1ed, 0x1ee):
                phy[a] = v
            if cls == 'PHY.RD' and a in (0x1ed, 0x1ee):
                first_stat.setdefault(a, v)
            if cls == 'PHY.WR' and a == 0x7c:
                gain = (v, gain[1])
            if a in (0x219, 0x21a) and cls == 'PHY.RD' and cur is not None:
                if cur['g'] is None:
                    cur['g'], cur['bb'] = gain[0], bb
                cur['r'].append((a, v))
        elif cls == 'RAD.WR':
            rad[a] = v
        elif cls == 'CC.WR':
            if a == 0x650:
                pmu_addr = v
            elif a == 0x654 and pmu_addr is not None:
                pmu[pmu_addr] = v
            else:
                cc[a] = v
        elif cls == 'CC.RD':
            if a == 0x654 and pmu_addr is not None:
                pmu.setdefault(pmu_addr, v)
            elif a == 0x28:
                cc.setdefault(a, v)

    # The pre-calibration: the idle round (one read, no gain forced) and the
    # four rounds after it, the last such group in the window
    rs = [r for r in rounds if r['r']]
    for i in range(len(rs) - 5, -1, -1):
        grp = rs[i:i + 5]
        if grp[0]['g'] == 0 and all(r['g'] for r in grp[1:]):
            idle = (s6(grp[0]['r'][0][1]), s6(grp[0]['r'][1][1]))
            parts = []
            for r in grp[1:]:
                core = 0 if r['bb'] == 0x4000 else 1
                vals = [s6(v) for a, v in r['r'] if a == (0x219, 0x21a)[core]]
                parts.append((core, (r['g'] >> 3) & 31,
                              sum(vals) - len(vals) * idle[core]))
            # the gain the calibration runs at: the first table 7 0x110
            # write after the group's last round
            ri = rounds.index(grp[-1]) + 1
            g = next((v for k, v in t7_after if k >= ri and v != grp[1]['g']
                      and v != grp[2]['g'] or (k >= ri and v in
                      (grp[1]['g'], grp[2]['g']))), 0)
            print('N-PHY cal: precal idle=%d,%d core0 pad%u=%d pad%u=%d '
                  'core1 pad%u=%d pad%u=%d gain=0x%04x' % (
                      idle + tuple(x for p in parts for x in p[1:]) + (g,)))
            break

    # the ladder's 100 % entry is the calibration multiplier of the core
    print('N-PHY cal: orig_pwr_idx=%u,%u txcal_bbmult=0x%04x' % (
        (first_stat.get(0x1ed, 0) >> 8) & 0x7f,
        (first_stat.get(0x1ee, 0) >> 8) & 0x7f,
        (tbl.get((15, 17), 0) >> 8) * 0x101))
    print('N-PHY cal: txiqlo tbl15[80..90]= ' +
          ' '.join('%04x' % tbl.get((15, o), 0) for o in range(80, 91)))
    print('N-PHY cal: rxiq a0=%04x b0=%04x a1=%04x b1=%04x' %
          tuple(phy.get(a, 0) for a in (0x9a, 0x9b, 0x9c, 0x9d)))
    print('N-PHY cal: rssi radio=%04x,%04x phy= %s' % (
        rad.get(0x0b4, 0), rad.get(0x139, 0),
        ' '.join('%04x' % phy.get(a, 0) for a in RSSI_PHY)))
    print('N-PHY cal: txpctl cmd=%04x n=%04x itssi=%04x tpwr=%04x bidx=%04x '
          'stat=%04x,%04x' % tuple(phy.get(a, 0) for a in
                                   (0x1e7, 0x1e8, 0x1e9, 0x1ea, 0x1eb,
                                    0x1ed, 0x1ee)))
    print('N-PHY cal: ppr adj_pwr_tbl=' + ','.join(
        '%x' % (tbl.get((26, o), 0) & 0xff) for o in range(64, 148)))
    chipctl = cc.get(0x28, 0)
    pmu1 = pmu.get(1, 0)
    print('N-PHY cal: chipctl=%08x (bit6 %u) pmu_chipctl1=%08x '
          '(extpa c0 %u c1 %u)' % (chipctl, bool(chipctl & 0x40), pmu1,
                                   bool(pmu1 & (1 << 13)),
                                   bool(pmu1 & (1 << 8))))


if __name__ == '__main__':
    main()
