#!/usr/bin/env python3
"""Vendor/test comparison with a DECLARED list of known divergences.

The problem it solves: a single op not reproduced at the start of the trace
misaligns everything after it, and 30000 ops become unreadable. Removing from
the vendor stream the ops we know we do not want to emit makes the comparison
downstream measurable again.

The problem it introduces, hence the rules: a list of exceptions is also the
easiest way to make a number add up.

  1. Every entry has a written REASON and a CONTEXT (the expected previous
     op). If the context does not match, the entry does not apply, and the
     tool says so.
  2. The result ALWAYS reports how many ops were skipped. The percentage is
     not quoted alone.
  3. An entry is legitimate only if we know why the port must not emit that
     op. "It aligns better" is not a reason.
  4. Skipped ops that could affect values read later are marked
     `cascade=True`: then the comparison downstream is suspect and the tool
     flags it.

Uso: cmp_skip.py vendor.txt test.txt lo:hi --min-block N --gap-tol M
     [--board agcombo] [--bus] [--verbose]
"""
import argparse
from functools import lru_cache
import re
import importlib.util
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir))
import paths  # noqa: E402,F401  (puts the decoders on sys.path)
from b43trace import tracelib  # noqa: E402


# ---------------------------------------------------------------------------
# Known divergences, per board. Each entry:
#   pattern  : regex on the vendor op to skip
#   after    : regex on the vendor op right before it (context)
#   after2   : regex on the op two positions before (optional)
#   before   : regex on the op right after it (optional)
#   before2  : regex on the op two positions after (optional)
#   max      : how many times at most it can apply
#   reason   : why the port must not emit it
#   cascade  : True if the op could affect values read later
KNOWN = {
    'd6220': [
        dict(pattern=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             after=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             before=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             before2=r'^PHY\.RD addr=0x7af',
             ctx=[(-6, r'^PHY\.RD addr=0x527')],
             max=3, cascade=False,
             reason="enable+suspend pair interleaved by the up context "
                    "during cal_restore's ~1 s probe pacing wait: it "
                    "appears only after late wakeups (gap 1.32 s instead "
                    "of 1.00 s, e.g. #28859/#28950) and only in the -up "
                    "capture -- absent in attach ch44, attach ch36-bw40 and "
                    "down-to-bss. The context at -6 (peek 0x527, a regular "
                    "iteration) excludes the twin pair at the end of the "
                    "extended-first, which the port transcribes and emits. "
                    "Scheduling noise, not driver structure: the port must "
                    "not emit it."),

        dict(pattern=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             after=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             after2=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             before=r'^PHY\.RD addr=0x7af',
             ctx=[(-7, r'^PHY\.RD addr=0x527')],
             max=3, cascade=False,
             reason="suspend half of the pair above: same evidence."),

        dict(pattern=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             after=r'^MAC\.MHF addr=0x0 val=0x0 mask=0x4000$',
             before=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             max=1, cascade=False,
             reason="first suspend+enable pair between the MHF (clr 0x4000) "
                    "and the finalize's GPIO. It is a beacon reload's inner "
                    "suspend, and the port emits those at "
                    "@beacon_reload_pre. On the seven segments below 5250 "
                    "MHz those are enough and this rule does not fire; on "
                    "the nineteen above one is left without a counterpart, "
                    "two ops with the entry below, and the residual is that "
                    "there the vendor has one more than its caller declares."),
        dict(pattern=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             after=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             after2=r'^MAC\.MHF addr=0x0 val=0x0 mask=0x4000$',
             max=1, cascade=False,
             reason="enable half of the pair above."),
        dict(pattern=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             after=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             before=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             ctx=[(-3, r'^MAC\.MHF addr=0x0 val=0x0 mask=0x4000$')],
             max=1, cascade=False,
             reason="second pair between MHF and GPIO: #28761, +1.29 s from "
                    "the first, of the same nature -- and on the 26 cold "
                    "segments it never fires, because the port emits that "
                    "reload."),
        dict(pattern=r'^MAC\.MCTRL val=0x1 mask=0x1$',
             after=r'^MAC\.MCTRL val=0x0 mask=0x1$',
             before=r'^GPIO\.OUT val=0x4 mask=0x4',
             ctx=[(-4, r'^MAC\.MHF addr=0x0 val=0x0 mask=0x4000$')],
             max=1, cascade=False,
             reason="enable half of the second pair."),

        # The clearing of the pairwise keys' MAC rows and the two rows at the
        # top. The core emits them: b43_upload_card_macaddress() and
        # b43_security_init() -> b43_clear_keys() -> keymac_write() ->
        # b43_amt_write(), after the first switch_channel. ADDRM.SET and
        # AMT.WR are the labels the vendor tracer puts on the entry of
        # keymac_write()/b43_amt_write() -- no addr=/val=, only idx= -- and
        # the integration suite, which sees only the bus, cannot produce
        # them. The OBJ.BULKR/OBJ.BULKW are bus traffic instead, and b43 does
        # them 16 bits at a time: they are here until the comparison can
        # unroll them.
        # Bus profile only: ../unit's harness emits the vendor's records
        # itself, at b43's point (see MOVED).
        # See docs/retrace-todo.md, "Key-table clearing (ADDRM.SET)".
        dict(pattern=r'^ADDRM\.SET idx=',
             after=None,
             max=60, bus_only=True, cascade=False,
             reason="entry label of keymac_write() on the vendor, not a bus "
                    "access: b43 gets there from b43_clear_keys() and "
                    "b43_upload_card_macaddress(), but function boundaries "
                    "do not show on the bus."),
        dict(pattern=r'^AMT\.WR idx=',
             after=r'^ADDRM\.SET idx=',
             max=60, bus_only=True, cascade=False,
             reason="entry label of b43_amt_write(): same reason as the "
                    "entry above, not a bus access."),
        dict(pattern=r'^OBJ\.BULKR addr=0x[0-9a-f]+ len=8$',
             after=r'^AMT\.WR idx=',
             after2=r'^ADDRM\.SET idx=',
             max=60, bus_only=True, cascade=False,
             reason="reread of the row, which KEEP_FLAGS implies."),
        dict(pattern=r'^OBJ\.BULKW addr=0x[0-9a-f]+ len=8$',
             after=r'^OBJ\.BULKR addr=0x[0-9a-f]+ len=8$',
             after2=r'^AMT\.WR idx=',
             max=60, bus_only=True, cascade=False,
             reason="rewrite of the row."),
    ],
    'agcombo': [
        dict(pattern=r'^PHY\.WR addr=0x1ec val=0x2$',
             after=r'^PHY\.MOD addr=0x2e4',
             max=2, cascade=False,
             reason="radar detect OFF. wlc_phy_radar_detect_on_off_cfg_acphy "
                    "is 144B in 7.14.43 (two writes: 0x2 then 0x9c40) and "
                    "52B in 7.14.89, where the preliminary switch-off is "
                    "gone. The port models 7.14.89, so it must not emit it."),

        dict(pattern=r'^(SI\.COREREG|PMU\.PLL|GPIO\.(OUT|OE) val=0x0 mask=0x0)',
             after=None,
             max=6, cascade=True,
             reason="clock and bus re-init between the two entries into the "
                    "analog section. These are bcma/b43-core ops, not the "
                    "PHY's: the PHY does not reprogram the PLL. NOT "
                    "UNDERSTOOD whether it is per chip or per version -- "
                    "switch_radio's code is identical between 7.14.89 and "
                    "7.14.43, so the difference is at runtime."),
    ],
}


# ---------------------------------------------------------------------------
# Blocks b43 emits at another point of the stream, per board. Each entry is
# a delimited block of the vendor stream and the point where b43 emits it:
#   start, end : regex on the block's first and last op
#   after      : regex on the op before the block (context, optional)
#   span       : how many ops at most from `start` to `end`, both included
#   dest       : regex on the first op after the block behind which b43
#                emits it
#   dest_ctx   : context of that op, (offset, regex) pairs (optional)
#   backward   : True if b43 emits it earlier: `dest` is then the last op
#                before the block
#   max        : how many blocks at most
#   reason     : where and why b43 emits it there
#
# The block is moved in the vendor stream before the alignment: its ops all
# stay in the comparison, and count as right only if the port emits them at
# the declared point.
#
# Empty: the core blocks the vendor emits inside the PHY's sequences are
# emitted by the harness at the vendor's point, from the
# b43_phy_ac_core_site() sites (b43/phy_ac.h), and moving them here would put
# them out of place again.
# ---------------------------------------------------------------------------
MOVED = {}

def apply_moves(ops, rules):
    """Move the blocks declared in MOVED to where b43 emits them."""
    used = [0] * len(rules)
    moved_ops = [0] * len(rules)
    out = list(ops)
    fixed = [False] * len(out)
    i = 0
    while i < len(out):
        hit = None
        for k, r in enumerate(rules):
            if used[k] >= r['max'] or fixed[i]:
                continue
            if not re.search(r['start'], out[i]):
                continue
            if r.get('after') and not (i > 0 and re.search(r['after'], out[i - 1])):
                continue
            end = next((j for j in range(i, min(len(out), i + r['span']))
                        if re.search(r['end'], out[j])), None)
            if end is None:
                continue
            def at(j):
                return re.search(r['dest'], out[j]) and \
                    all(0 <= j + o < len(out) and re.search(x, out[j + o])
                        for o, x in r.get('dest_ctx', ()))
            if r.get('backward'):
                dest = next((j + 1 for j in range(i - 1, -1, -1) if at(j)),
                            None)
            else:
                dest = next((j + 1 for j in range(end + 1, len(out))
                             if at(j)), None)
            if dest is None:
                continue
            hit = (k, end, dest)
            break
        if hit is None:
            i += 1
            continue
        k, end, dest = hit
        block = out[i:end + 1]
        if dest > end:
            rest = out[end + 1:dest]
            out[i:dest] = rest + block
            fixed[i:dest] = fixed[end + 1:dest] + [True] * len(block)
        else:
            rest = out[dest:i]
            out[dest:end + 1] = block + rest
            fixed[dest:end + 1] = [True] * len(block) + fixed[dest:i]
            i = end + 1
        used[k] += 1
        moved_ops[k] += len(block)
    return out, used, moved_ops


# ---------------------------------------------------------------------------
# Read-modify-write on the bus (--rmw). The N-PHY's MMIO captures do not have
# the accessors' ops (the MODs of the AC's wl-diag captures): an RMW appears
# there as the read of a PHY or radio register followed right away by the
# write of the same register. The pair becomes a single op, X.RMW, with the
# value read and the value written, on both sides; an RMW that writes back
# the value read is still an op to emit. The table port is excluded, where
# the data read and write are not an RMW of the same register.
# ---------------------------------------------------------------------------
RMW_EXCLUDED = {
    ('PHY', 0x072),     # TABLE_ADDR
    ('PHY', 0x073),     # TABLE_DATALO
    ('PHY', 0x074),     # TABLE_DATAHI
}
_RMW_OP = re.compile(r'^(PHY|RAD)\.(RD|WR) addr=(0x[0-9a-f]+) val=(\S+)$')


def _rmw_op(o):
    m = _RMW_OP.match(o)
    if not m:
        return None
    sp, kind, addr, val = m.groups()
    addr = int(addr, 16)
    return None if (sp, addr) in RMW_EXCLUDED else (sp, kind, addr, val)


def fold_rmw(ops):
    """The read+write pairs of the same register as X.RMW."""
    out = []
    i = 0
    while i < len(ops):
        r = _rmw_op(ops[i])
        w = _rmw_op(ops[i + 1]) if i + 1 < len(ops) else None
        if r and w and r[1] == 'RD' and w[1] == 'WR' and \
                (r[0], r[2]) == (w[0], w[2]):
            out.append(f'{r[0]}.RMW addr={r[2]:#x} rd={r[3]} val={w[3]}')
            i += 2
            continue
        out.append(ops[i])
        i += 1
    return out


def load_compare(path=None):
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            'compare.py')
    spec = importlib.util.spec_from_file_location('cmp', path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def apply_skips(ops, rules, verbose=False):
    """Remove from the vendor stream the ops that match, honoring max and context."""
    used = [0] * len(rules)
    out, skipped = [], []
    for i, op in enumerate(ops):
        hit = None
        for k, r in enumerate(rules):
            if used[k] >= r['max']:
                continue
            if not re.search(r['pattern'], op):
                continue
            ctx = [('after', i - 1), ('after2', i - 2),
                   ('before', i + 1), ('before2', i + 2)]
            ok = True
            for key, j in ctx:
                rx = r.get(key)
                if rx is None:
                    continue
                neigh = ops[j] if 0 <= j < len(ops) else ''
                if not re.search(rx, neigh):
                    ok = False
                    break
            for off, rx in r.get('ctx', ()):
                neigh = ops[i + off] if 0 <= i + off < len(ops) else ''
                if not re.search(rx, neigh):
                    ok = False
                    break
            if not ok:
                continue
            hit = k
            break
        if hit is None:
            out.append(op)
        else:
            used[hit] += 1
            skipped.append((i, op, hit))
    return out, skipped, used


OP_HEAD = re.compile(r'^(\S+)\s+(?:addr|off|word)=(0x[0-9a-f]+)')
TBL_HEAD = re.compile(r'^TBL\.(?:WR|RD)\s+(id=\S+\s+off=\S+)')

# Ops WITHOUT an identity of their own: they are ports, and the identity is
# the access that encloses them.
#
#   PHY 0x000d/0x000e   table address port
#   PHY 0x000f/0x0010   table data port, low and high word
#
# That 0x0010 is the port's second word and not a register shows in the run:
# 0x000d, 0x000e, then 0x0010 and 0x000f alternating, one pair per entry.
#
# A TBL.WR is a marker: the data go out as a run of writes on the data port,
# one per entry. Two writes to 0x000f are not "the same op with a different
# value" just because the address coincides -- the address always
# coincides, it is a port. They can belong to different tables.
#
# So the pairing key for these includes the TBL marker before them in their
# stream. The other classes have an address that identifies a register and
# pair on that.
PORTS = {('PHY.WR', '0xd'), ('PHY.WR', '0xe'), ('PHY.WR', '0xf'),
         ('PHY.WR', '0x10'),
         ('PHY.RD', '0xd'), ('PHY.RD', '0xe'), ('PHY.RD', '0xf'),
         ('PHY.RD', '0x10')}

# Generation relations checked and that need NO rule, because the generated
# classes do not appear in the d6220's captures:
#
#   TPL.RAMW -> TPL.PTRW/TPL.DATW    zero occurrences of the latter
#
# OBJ.BULKW -> OBJ.WR needs none either: the bulk pair is hooked in the
# d6220's captures -- 130 OBJ.BULKR and 151 OBJ.BULKW on cold01 -- and both
# sides emit the header, so they pair on their own class. A capture with the
# single ops and without the bulk would want a rule.
#
# One that exists but does not endanger the pairing: MAC.MHF writes the
# matching HOSTF cell in five cases out of eleven, so a missing MHF brings a
# missing OBJ.WR with it. They are two traced ops and count as two, but they
# do not pair with each other -- the classes differ -- so _pair_gap() does
# not mix them up. See "Host-flag order" in docs/retrace-todo.md.


def op_keys(ops):
    """Identity key of every op: for the ports it includes the TBL marker."""
    out = []
    tbl = None
    for o in ops:
        m = TBL_HEAD.match(o)
        if m:
            tbl = m.group(1)
        m = OP_HEAD.match(o)
        if not m:
            out.append(None)
            continue
        key = (m.group(1), m.group(2))
        out.append(key + (tbl,) if key in PORTS else key)
    return out


@lru_cache(maxsize=32768)
def ident(o):
    """Identity of an op without the value: class and register, or the class
    alone for those that have no register (MAC.MCTRL, GPIO.*)."""
    m = OP_HEAD.match(o)
    if m:
        return m.group(1) + ' ' + m.group(2)
    return o.split(' ', 1)[0]


def _pair_gap(kv, kt, V, a, b, c, d):
    """Pairs of the same register inside an unaligned region.

    An op emitted on the right register with the wrong value appears twice
    in the diff -- wl's version missing and the port's left over -- and
    counting it as "one missing plus one too many" inflates the defect and
    calls it by the wrong name. The pairing is by identity (op_keys()): a
    register for the normal classes, the register plus the TBL marker for
    the table ports. A vendor op with the wildcard value (`val=*`, see
    canon()) paired this way has no value to get wrong and comes back as
    equal.

    Returns (equal, wrong value, missing, too many).
    """
    avail = {}
    for x in kt[c:d]:
        if x is not None:
            avail[x] = avail.get(x, 0) + 1
    wild = wrong = 0
    for i, x in enumerate(kv[a:b]):
        if x is not None and avail.get(x):
            avail[x] -= 1
            if 'val=*' in V[a + i]:
                wild += 1
            else:
                wrong += 1
    paired = wild + wrong
    return wild, wrong, (b - a) - paired, (d - c) - paired


def segments(V, T, ops_equal, bus, anchor=False):
    """The alignment as a sequence of stretches, in the order of the two
    streams.

    ('run', n): n pairs of corresponding ops, consecutive on both sides.
    ('gap', nv, nt, eq, wrong, missing, surplus): nv ops of wl and nt of the
    port between one stretch and the next; eq of those pairs count as equal
    (the wildcard, see _pair_gap()), the others do not correspond.

    With the bus profile a trace has the full values and the unrolled vendor
    has a MOD's read without a value and the write bound to a mask: the
    strings are never equal, so it aligns on the identity -- class and
    register -- and inside the aligned blocks the value is judged with
    compare.py's ops_equal(); a wrong value breaks the stretch. Without the
    bus profile it aligns on the strings.

    With anchor, two streams with full values on both sides (a bus capture
    against test/integration): it aligns on the strings first, and on the
    identity only between one block and the next. On a long series of writes
    to the same port the identity alone does not know where the series
    starts, and one op more or less before it pairs the whole series off by
    one.
    """
    def same(v, t):
        # canon() made 'val=*' the read without a value; ops_equal() knows it
        # as UNDEFINED. The accessor-level side can be either one: the
        # wl-diag vendor against a bus trace (test/integration), or the
        # harness against a bus capture (reverse-tools/mmio2ops.py). The bus
        # side has neither wildcards nor masks, so trying both directions
        # does not widen the comparison.
        return (ops_equal(v.replace('val=*', 'val=UNDEFINED'), t) or
                ops_equal(t.replace('val=*', 'val=UNDEFINED'), v))

    kv, kt = (None, None) if bus else (op_keys(V), op_keys(T))
    av, at = ([ident(o) for o in V], [ident(o) for o in T]) if bus else (V, T)
    segs, diff = [], []

    def run(n):
        if segs and segs[-1][0] == 'run':
            segs[-1] = ('run', segs[-1][1] + n)
        else:
            segs.append(('run', n))

    def opcodes():
        if not anchor:
            yield from tracelib.align_opcodes(av, at)
            return
        for k, a, b, c, d in tracelib.align_opcodes(V, T):
            if k == 'equal':
                yield k, a, b, c, d
                continue
            for k2, a2, b2, c2, d2 in tracelib.align_opcodes(av[a:b],
                                                              at[c:d]):
                yield k2, a + a2, a + b2, c + c2, c + d2

    for k, a, b, c, d in opcodes():
        if k != 'equal':
            # With the bus profile the identities outside the aligned blocks
            # differ by construction: all missing or too many.
            pg = ((0, 0, b - a, d - c) if bus else
                  _pair_gap(kv, kt, V, a, b, c, d))
            segs.append(('gap', b - a, d - c) + pg)
            diff.append((k, a, b, c, d))
            continue
        if not bus:
            run(b - a)
            continue
        n = 0
        for i in range(b - a):
            if same(V[a + i], T[c + i]):
                n += 1
                continue
            if n:
                run(n)
            n = 0
            segs.append(('gap', 1, 1, 0, 1, 0, 0))
            diff.append(('value', a + i, a + i + 1, c + i, c + i + 1))
        if n:
            run(n)
    return segs, diff


def score(segs, min_block, gap_tol):
    """The equal pairs that are in long enough blocks.

    A block is a sequence of stretches separated by no more than gap_tol
    non-corresponding ops, counted as max(nv, nt) minus the pairs that count
    as equal: a wrong value is one, a missing op is one, three wl ops
    against five of the port are five. A block counts if it has at least
    min_block equal pairs; the non-corresponding ops it tolerates stay
    non-corresponding. The pairs of shorter blocks count as one missing and
    one too many.

    Returns (equal counted, blocks counted, equal discarded, non-corresponding
    ops tolerated inside the counted blocks).
    """
    eq = nblk = lost = tol = 0
    cur = cur_tol = 0
    pending = None

    def close():
        nonlocal eq, nblk, lost, tol
        if cur >= min_block:
            eq += cur
            nblk += 1
            tol += cur_tol
        else:
            lost += cur

    for sg in segs:
        if sg[0] == 'gap':
            if pending is None:
                pending = [0, 0]
            nc = max(sg[1], sg[2]) - sg[3]
            pending[0] += nc
            pending[1] += sg[3]
            continue
        if pending is not None and cur and pending[0] <= gap_tol:
            cur += pending[1] + sg[1]
            cur_tol += pending[0]
        else:
            if cur:
                close()
            cur, cur_tol = sg[1], 0
        pending = None
    if cur:
        close()
    return eq, nblk, lost, tol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('vendor')
    ap.add_argument('test')
    ap.add_argument('range')
    ap.add_argument('--bus', action='store_true',
                    help='compare.py\'s bus profile: MODs unrolled, TBL.* '
                         'dropped')
    ap.add_argument('--anchor-values', action='store_true',
                    help='align on the values first: two bus streams with '
                         'full values, see segments()')
    ap.add_argument('--board', default='agcombo')
    ap.add_argument('--led-pins', type=lambda v: int(v, 0), default=0,
                    help='GPIO pins of the LEDs, from ./ac_trace led_pins BOARD')
    ap.add_argument('--min-block', type=int, required=True,
                    help='minimum equal pairs of a block for it to count '
                         f'(at least {tracelib.MIN_BLOCK}, the alignment\'s '
                         'minimum)')
    ap.add_argument('--gap-tol', type=int, required=True,
                    help='non-corresponding ops tolerated between one '
                         'stretch and the next of the same block')
    ap.add_argument('--rmw', action='store_true',
                    help='read+write of the same PHY or radio register as '
                         'one RMW')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()
    if args.min_block < tracelib.MIN_BLOCK:
        ap.error(f'--min-block below {tracelib.MIN_BLOCK}: the alignment '
                 'produces no shorter blocks')
    if args.gap_tol < 0:
        ap.error('--gap-tol negative')

    C = load_compare()
    lo, hi = (int(x) for x in args.range.split(':'))
    profile = 'bus' if args.bus else None
    expanded = {}
    v = C.load_vendor(args.vendor, (lo, hi), profile, expanded)
    t = C.load_test(args.test, profile)
    off = C.find_offset(t, v[0])
    if off > 0:
        t = t[off:]

    @lru_cache(maxsize=32768)
    def canon(o):
        o = C.RET_SUFFIX.sub('', o)
        if 'val=UNDEFINED' in o or C.val_nondet(o):
            o = C.VAL_TOK.sub('val=*', o, count=1)
        return C.strip_bits_outside_perimeter(o)

    rules = [r for r in KNOWN.get(args.board, [])
             if args.bus or not r.get('bus_only')]
    moves = MOVED.get(args.board, [])
    V0 = [canon(x) for x in v]
    T = [canon(x) for x in t]

    # Ops no capture can contain: out from both sides, or the denominator and
    # the numerator talk about different things. Defined in compare.py, like
    # the perimeter.
    V0, ps = C.drop_power_save(V0)
    V0, sv = C.drop_vendor_only(V0)
    V0, mv_used, mv_ops = apply_moves(V0, moves)
    T, sp = C.drop_port_only(T, V0)
    if args.rmw:
        V0, T = fold_rmw(V0), fold_rmw(T)

    ops_equal = lru_cache(maxsize=32768)(C.ops_equal)

    def stats(V, T):
        segs, diff = segments(V, T, ops_equal, args.bus, args.anchor_values)
        eq, nblk, lost, tol = score(segs, args.min_block, args.gap_tol)
        cls = [sum(sg[i] for sg in segs if sg[0] == 'gap') for i in (4, 5, 6)]
        return eq, nblk, diff, (lost, tol), cls

    eq0, nblk0, _, bl0, cls0 = stats(V0, T)
    VP, outside, keys = C.apply_perimeter(V0, args.led_pins)
    eqp, nblkp, _, blp, clsp = stats(VP, T)
    V1, skipped, used = apply_skips(VP, rules, args.verbose)
    eq1, nblk1, diff1, bl1, cls1 = stats(V1, T)

    print(f"board {args.board}, window {lo}:{hi}")
    if expanded:
        print(f"bulk expanded   : {C.format_expanded(expanded)}")
    if ps:
        print(f"power save      : {len(ps)} ops of wl's power saving "
              f"without a BSS, see drop_power_save()")
    if sv:
        print(f"vendor only     : {len(sv)} ops no b43 code can emit")
    if sp:
        print(f"port only       : {len(sp)} port ops without an oracle, "
              f"see PORT_ONLY")
    if moves:
        print(f"moved           : {sum(mv_ops)} ops in {sum(mv_used)} blocks "
              f"on {len(moves)} rules, see MOVED")
    print()
    print("The line that counts is 'raw'. The denominator is the union: all")
    print("of wl's ops PLUS those the port emits and wl does not, because an")
    print("op too many is as much a defect as a missing one -- the driver must")
    print("emit wl's ops, not its own. It reaches 100% only if the two streams")
    print("coincide. The numerator is the equal pairs in the blocks of at least")
    print(f"{args.min_block} pairs, with at most {args.gap_tol} "
          "non-corresponding ops between one stretch")
    print("and the next.")
    print("'in perimeter' removes what the harness cannot emit and serves")
    print("navigation, not scoring: that debt is in docs/retrace-todo.md.\n")
    par = f"[block>={args.min_block} tol={args.gap_tol}]"

    def line(name, eq, nv, nt, nblk, bl, cls):
        tot = nv + nt - eq
        wrong, missing, surplus = cls
        lost, tol = bl
        print(f"{name:16s}: {eq}/{tot} = {100.0 * eq / tot:.2f}%   {par}   "
              f"{nblk} blocks")
        print(f"                  {wrong} with the wrong value, "
              f"{missing} wl ops missing, {surplus} port ops too many")
        print(f"                  {lost} equal pairs in blocks too short, "
              f"{tol} non-corresponding ops tolerated in the blocks")

    line("raw", eq0, len(V0), len(T), nblk0, bl0, cls0)
    line("in perimeter", eqp, len(VP), len(T), nblkp, blp, clsp)
    print(f"                  {len(outside)} ops outside the perimeter on "
          f"{len(keys)} cells declared someone else's")
    line("WITH exceptions", eq1, len(V1), len(T), nblk1, bl1, cls1)
    print(f"                  {len(skipped)} ops skipped on {len(rules)} rules\n")

    for k, r in enumerate(C.PERIMETER):
        n = sum(1 for _, h in outside if h == k)
        print(f"  [perimeter {k}] {n} ops" + ("  NOT APPLIED" if not n else ""))
        for ln in re.findall(r'.{1,72}(?:\s|$)', r['reason']):
            print(f"      {ln.strip()}")
        print()

    for k, r in enumerate(moves):
        tag = "NOT APPLIED (0 times)" if not mv_used[k] else \
            f"{mv_used[k]}/{r['max']} blocks, {mv_ops[k]} ops"
        print(f"  [move {k}] {tag}")
        for ln in re.findall(r'.{1,72}(?:\s|$)', r['reason']):
            print(f"      {ln.strip()}")
        print()

    casc = False
    for k, r in enumerate(rules):
        if not used[k]:
            print(f"  [rule {k}] NOT APPLIED (0 times)")
        else:
            flag = '  ** CASCADE **' if r['cascade'] else ''
            print(f"  [rule {k}] {used[k]}/{r['max']} times{flag}")
            casc = casc or r['cascade']
        for ln in re.findall(r'.{1,72}(?:\s|$)', r['reason']):
            print(f"      {ln.strip()}")
        print()

    if casc:
        print("WARNING: at least one rule is marked cascade. The skipped ops")
        print("could affect values read later, so the comparison downstream")
        print("is indicative, not conclusive.\n")

    print("first divergent regions after the exceptions:")
    for k, a, b, c, d in diff1[:6]:
        print(f"  {k:<7} V[{a}:{b}]={b - a}  T[{c}:{d}]={d - c}")
        for i in range(a, min(b, a + 2)):
            print(f"      V {V1[i][:64]}")
        for i in range(c, min(d, c + 2)):
            print(f"      T {T[i][:64]}")


if __name__ == '__main__':
    main()
