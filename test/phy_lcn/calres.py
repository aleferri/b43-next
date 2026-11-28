#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""The LCN-PHY calibration's results wl holds at op LAST of a capture, as
the environment of lcn-trace's flow switch: its last writes of the TX I/Q
and LO coefficients (table 0 at 0x50, 0x51 and 0x55), of the radio's LO
(0x089-0x08c) and of the RX I/Q compensation (0x645, 0x646).

    calres.py OPS LAST
"""
import re
import sys

op = re.compile(r'#(\d+) +cpu\d+ +(PHY|RAD)\.WR addr=0x(\w+) val=0x(\w+)')


class CalRes:
    """The results of the capture's lines fed so far."""

    def __init__(self):
        self.tab = 0
        self.res = {}

    def feed(self, line):
        m = op.search(line)
        if not m:
            return
        reg, val = int(m.group(3), 16), int(m.group(4), 16)
        if m.group(2) == 'RAD':
            if 0x089 <= reg <= 0x08c:
                self.res['LCN_LOFT%d' % (reg - 0x089)] = val
            return
        if reg == 0x455:
            self.tab = val
        elif reg == 0x456:
            names = {0x50: 'LCN_TXIQCC_A', 0x51: 'LCN_TXIQCC_B',
                     0x55: 'LCN_TXDIDQ'}
            if self.tab in names:
                self.res[names[self.tab]] = val
            self.tab += 1
        elif reg in (0x645, 0x646):
            self.res['LCN_RXIQCOMP_%s' % 'AB'[reg - 0x645]] = val & 0x3ff

    def env(self):
        return ' '.join('%s=0x%x' % kv for kv in sorted(self.res.items()))


if __name__ == '__main__':
    last = int(sys.argv[2])
    cal = CalRes()
    for line in open(sys.argv[1]):
        m = op.search(line)
        if m and int(m.group(1)) > last:
            break
        cal.feed(line)
    print(cal.env())
