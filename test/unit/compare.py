#!/usr/bin/env python3
"""
Normalize a wl-diag capture and compare it against the trace emitted by
ac_trace. Only op lines are compared (address / value / mask); the
episode number, timestamp, and cpuN prefix are stripped from the vendor
trace before diffing since the test does not simulate scheduling.

Usage:
    compare.py <vendor.txt> <test.out> [OPTIONS]

--range LO:HI     keep only vendor lines whose episode # is within [LO,HI]
--no-perimeter    also compare the ops of the b43 core (see PERIMETER)
--auto-align     find the offset in <test> that best matches <vendor>[0]
                  by scanning for the first common op; useful when the
                  test flow does a prologue (save-gain, save-tone, ...)
                  that the vendor does not emit. Reports the offset and
                  compares from there.
--align-on OP    like --auto-align but pin to a specific op string
                  (e.g. 'PHY.WR   addr=0x0463 val=0x0027').

Vendor uses PHY.OR/PHY.AND for single-bit sets/clears; they get folded
to PHY.MOD in the format `val=<kernel_mask> mask=<kernel_set>` so the
test's PHY.MOD lines diff cleanly.
"""
import re
import sys
import argparse
from functools import lru_cache
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir))
import paths  # noqa: E402,F401  (puts the decoders on sys.path)
from b43trace import tracelib  # noqa: E402

VENDOR_LINE = re.compile(
    r'^\s*[0-9.]+\s+#\d+\s+cpu\d+\s+(.+?)\s*(?:;.*)?$'
)
TEST_LINE = re.compile(r'^cpu\d+\s+(.+?)\s*$')

# High-level ops and their "shadow" at core-register level: the vendor
# tracer logs both, the harness (like the driver) only the first. The shadows
# are the chipcommon regcontrol addr/data pair and the GPIO registers; the
# other SI.COREREG (0x0600, 0x0080, 0x0088, and anything not on core 0) are
# ops of their own and are not dropped.
SHADOW_OFFSETS = {
    0x658, 0x65c,          # regcontrol addr/data   <- PMU.RC
    0x660, 0x664,          # pllcontrol addr/data   <- PMU.PLL
    0x064,                 # gpioout               <- GPIO.OUT
    0x068,                 # gpioouten             <- GPIO.OE / GPIO.OUTEN
    0x06c,                 # gpiocontrol           <- GPIO.CTL
}
# NOT a shadow, even though it always follows a GPIO.CTL or GPIO.OE: (core 0,
# 0x08c) is gpiotimeroutmask, written by si_gpioled(), the mask of the GPIOs
# the LED timer may drive. It is an op of the LED block and is in PERIMETER
# with the others.
# Ops the driver really performs, but from code outside the unit under test:
# the harness compiles only the PHY files of b43/, not main.c nor bcma. They
# are skipped, not reproduced, and only after checking that whoever performs
# them emits them *at the right point* of the sequence -- otherwise the skip
# hides an ordering error.
#
#   (core 3, 0x01e0) BCMA_CLKCTLST: b43_bcma_wireless_core_reset calls
#   bcma_core_set_clockmode(BCMA_CLKMODE_FAST) before b43_phy_init, and the
#   capture has the request at #37/#40, before the first PHY op (#60). Order
#   checked. The second pair (#115/#118) reads the same register with HAVEHT
#   already high: assumed to repeat the same request, not checked separately.
#
#   (core 0, 0x600) BCMA_CC_PMU_CTL: the PLL_UPD flush that latches the
#   PLLCTL values. Patch 0007 emits it with bcma_pmu_set32 right after the two
#   bcma_chipco_pll_write, exactly where the capture has it (#31/#34, read and
#   write of the read-modify-write, right after the PLLs at #15/#23). Order
#   checked.
# Pure reads the driver does not do: the vendor tracer records them, but they
# are read-backs without effect, and reproducing them would mean adding a read
# whose result is discarded. They are skipped, on condition that they really
# have no side effect (null mask).
FOREIGN_READBACK = re.compile(
    r'^PMU\.PLL addr=0x[23] val=0x0 mask=0x0\b')

FOREIGN_COREREG = {
    (0x3, 0x1e0),
    (0x0, 0x600),
}

SHADOW_PARENT = re.compile(r'^(?:PMU\.(?:RC|PLL)|GPIO\.(?:OUT|OE|OUTEN|CTL))\b')
SI_COREREG = re.compile(r'^SI\.COREREG\s+core=(0x[0-9a-fA-F]+)\s+off=(0x[0-9a-fA-F]+)')

# The header of a copy into object memory. The vendor tracer writes it
# because it hooks the copy routine, and right below come the words, one op
# per word: `OBJ.BULKW addr=0x0160 len=32` and then the sixteen `OBJ.WR` from
# 0x0160. There the header is not a hardware access, it is the name of what
# makes one -- b43 writes the same words without a copy routine, and
# requiring it to emit the header too would mean inventing an op.
#
# It is not always a shadow, though: on the rows of the address match table
# (RCMTA routing) the words are not decoded, the header is the only record of
# the traffic, and the port has to emit it -- see b43_test_emit_amt(). So the
# condition is not the class but what follows: it is dropped only if the
# single word at the same address is below it.
BULK_HEAD = re.compile(r'^OBJ\.BULK([RW])\s+addr=(0x[0-9a-fA-F]+)\s+len=')
BULK_WORD = re.compile(r'^OBJ\.(RD|WR)\s+addr=(0x[0-9a-fA-F]+)\b')

# The chanspec in shared memory, the same case as the header above: the
# tracer hooks the function that writes it, and the write is the next op --
# `CS.SHM ch=36 bw=20 band=5g raw=0xd024` and then `OBJ.WR addr=0x00a0
# val=0xd024`, where the record's raw field is the word the write carries.
# One event with two records, so the hardware is compared on the write and
# the port must not emit the boundary.
#
# The condition is what follows, not the class, as for BULK_HEAD: a record
# without its write below stays in the stream and counts. On the 43 cold
# segments there is exactly one per segment and the write is always there,
# with the same value; a segment that recorded the boundary and not the write
# would show it instead of vanishing.
CHANSPEC_HEAD = re.compile(r'^CS\.SHM\b.*\braw=(0x[0-9a-fA-F]+)')
CHANSPEC_WORD = re.compile(
    r'^OBJ\.WR\s+addr=0x0*a0\s+val=(0x[0-9a-fA-F]+)\b')


def _chanspec_head_is_shadow(op, nxt):
    m = CHANSPEC_HEAD.match(op)
    if not m or nxt is None:
        return False
    w = CHANSPEC_WORD.match(nxt)
    return bool(w) and int(m.group(1), 16) == int(w.group(1), 16)


def _bulk_head_is_shadow(op, nxt):
    m = BULK_HEAD.match(op)
    if not m or nxt is None:
        return False
    w = BULK_WORD.match(nxt)
    if not w:
        return False
    if (m.group(1), w.group(1)) not in (('R', 'RD'), ('W', 'WR')):
        return False
    return int(m.group(2), 16) == int(w.group(2), 16)


# OBJ.SET: the header of a shared memory clear, the same case as BULK_HEAD
# above. `OBJ.SET addr=0x10f4 val=0x0000 len=960` and then the real writes,
# one word at a time from the same address and with the same value. One event
# with two records: the writes are in the comparison and the port emits them
# all, so the header must not be counted a second time.
#
# As for BULK_HEAD the condition is what follows, not the class. On the 43
# cold segments there is exactly one per segment and the write it covers is
# always there, with the same address and value; a segment that recorded the
# header without the writes would show it instead of vanishing.
SET_HEAD = re.compile(
    r'^OBJ\.SET\s+addr=(0x[0-9a-fA-F]+)\s+val=(0x[0-9a-fA-F]+)\s+len=')
SET_WORD = re.compile(r'^OBJ\.WR\s+addr=(0x[0-9a-fA-F]+)\s+val=(0x[0-9a-fA-F]+)')


def _set_head_is_shadow(op, nxt):
    m = SET_HEAD.match(op)
    if not m or nxt is None:
        return False
    w = SET_WORD.match(nxt)
    if not w:
        return False
    return (int(m.group(1), 16) == int(w.group(1), 16) and
            int(m.group(2), 16) == int(w.group(2), 16))


# PHY.RDW: the data port read without reselecting the address.
#
# The record carries no address because the FUNCTION has none, not because
# the tracer loses it. Read on the prologue of the d6220's 7.14 blob
# (`mipsdis.py wlD6220.o 0xbc458 0x68`), phy_reg_read_wide takes only $a0:
#
#     lw   $v0, 228($a0)      MMIO base
#     lhu  $v0, 1022($v0)     reads base+0x3fe, PHY_DATA
#
# while phy_reg_read(pi, addr) at 0xb9d70 uses $a1 and first writes
# base+0x3fc, PHY_CONTROL. The wide one skips that write: it rereads the data
# port with the address left where it was.
#
# So the register is not unknown, it is determined by construction: the one
# the last PHY access selected. It is resolved here, and the comparison sees
# the same op b43 emits, which reselects that register every time -- an
# idempotent PHY_CONTROL write, without effect on the data read.
#
# With no PHY access before it the address is not there and the record stays
# as it is: it becomes a divergence, the right way to be wrong.
PHY_SELECT = re.compile(r'^PHY\.(?:RD|WR|MOD)\s+addr=(0x[0-9a-fA-F]+)')
WIDE_READ = re.compile(r'^PHY\.RDW\s+(val=\S+)')


def resolve_wide_reads(ops):
    """Give every PHY.RDW the address the previous access selected."""
    out, sel = [], None
    for op in ops:
        m = PHY_SELECT.match(op)
        if m:
            sel = m.group(1)
        w = WIDE_READ.match(op)
        if w and sel is not None:
            op = normalize_op(f"PHY.RD addr={sel} {w.group(1)}")
        out.append(op)
    return out


# The bulk headers without the words below.
#
# On the d6220 the copies into shared memory (selector 1) and into the SCR
# (selector 2) are always followed by their words, and drop_shadow_ops drops
# the header. On the tg789vac-v2 they never are: 2510 BULKW, 423 BULKR and 43
# SET with those selectors on the 43 cold segments, without a single word
# below. The selector also says the word below is the expansion: after
# `BULKW 0x0010 len=2` with selector 2 the d6220 has the word with sel=0x20000
# and then an SHM write to the same cell, the tg789 only the second. The head
# already fixes the event's shape, though, because on the d6220's segments
# every expansion is exactly len/2 consecutive 16-bit ops from addr, in
# ascending order. Here it is rebuilt from the head:
#
#   OBJ.SET    len/2 writes of val: a memset, the value is in the head.
#   OBJ.BULKR  len/2 reads val=UNDEFINED: the value read is not there, and for
#              ops_equal() an UNDEFINED read binds address and class.
#   OBJ.BULKW  len/2 writes val=UNDEFINED: the tracer does not record `buf`,
#              so these writes are compared on address and order and NOT on
#              the value. That is why they are counted apart: a write here
#              matches whatever value the port writes.
#
# Selector 4 (RCMTA, the 8-byte rows of the address match table) stays as it
# is: it is expanded on no board, and the port emits that header -- see
# b43_test_emit_amt(). A head without a5, an ARGX not folded, stays as it is
# too: the selector is unknown.
BULK_RAW = re.compile(r'^OBJ\.BULK([RW])\s+addr=(0x[0-9a-fA-F]+)\s+len=(\d+)'
                      r'.*?\ba5=(0x[0-9a-fA-F]+)')
SET_RAW = re.compile(r'^OBJ\.SET\s+addr=(0x[0-9a-fA-F]+)\s+val=(0x[0-9a-fA-F]+)'
                     r'\s+len=(\d+)')
BULK_EXPAND_SEL = (0x10000, 0x20000)


def _same_sel(head, word):
    """The word below carries the head's selector, or none."""
    a5 = re.search(r'\ba5=(0x[0-9a-fA-F]+)', head)
    sel = re.search(r'\bsel=(0x[0-9a-fA-F]+)', word)
    return not a5 or not sel or int(a5.group(1), 16) == int(sel.group(1), 16)


def expand_bulk_heads(raws, expanded=None):
    """Replace the unexpanded bulk headers with their words."""
    out = []
    for i, raw in enumerate(raws):
        nraw = raws[i + 1] if i + 1 < len(raws) else None
        nxt = normalize_op(nraw) if nraw is not None else None
        head = normalize_op(raw)
        if _set_head_is_shadow(head, nxt) or \
                (_bulk_head_is_shadow(head, nxt) and _same_sel(raw, nraw)):
            out.append(raw)
            continue
        m = SET_RAW.match(raw)
        if m:
            a, val, n = int(m.group(1), 16), m.group(2), int(m.group(3)) // 2
            kind, words = 'SET', [f"OBJ.WR addr=0x{a + 2 * k:04x} val={val}"
                                  for k in range(n)]
        else:
            m = BULK_RAW.match(raw)
            if not m or int(m.group(4), 16) not in BULK_EXPAND_SEL:
                out.append(raw)
                continue
            rw, a, n = m.group(1), int(m.group(2), 16), int(m.group(3)) // 2
            kind = 'BULK' + rw
            words = [f"OBJ.{'RD' if rw == 'R' else 'WR'} "
                     f"addr=0x{a + 2 * k:04x} val=UNDEFINED" for k in range(n)]
        out.extend(words)
        if expanded is not None:
            expanded[kind] = expanded.get(kind, 0) + len(words)
    return out


def format_expanded(expanded):
    parts = [f"{expanded[k]} words from {k}" for k in ('SET', 'BULKR', 'BULKW')
             if k in expanded]
    s = ", ".join(parts)
    if 'BULKW' in expanded:
        s += f"; of the {expanded['BULKW']} from BULKW the value is not compared"
    return s


def drop_shadow_ops(ops):
    """Drop the high-level ops whose actuation is the ones that follow."""
    out = []
    parent = False
    for i, op in enumerate(ops):
        nxt = ops[i + 1] if i + 1 < len(ops) else None
        if _bulk_head_is_shadow(op, nxt):
            continue
        if _chanspec_head_is_shadow(op, nxt):
            continue
        if _set_head_is_shadow(op, nxt):
            continue
        if FOREIGN_READBACK.match(op):
            parent = True          # its shadows stay shadows
            continue
        m = SI_COREREG.match(op)
        if m and (int(m.group(1), 16), int(m.group(2), 16)) in FOREIGN_COREREG:
            continue
        if m and parent and int(m.group(1), 16) == 0 \
                and int(m.group(2), 16) in SHADOW_OFFSETS:
            continue
        parent = bool(SHADOW_PARENT.match(op))
        out.append(op)
    return out

# The normalization of the ops is in reverse-tools/tracelib.py, not here,
# because it is the definition of "the same op": the whitespace, the width of
# the literals, the fold of PHY.AND/PHY.OR onto the PHY.MOD form the wrapper
# emits, and the name of the GPIO enable -- which the harness takes from the
# bcma symbol (bcma_chipco_gpio_outen) and the vendor tracer from the
# register (OE). Every tool comparing the two sides must use the same one, or
# two tools give two answers to the same question.
normalize_op = tracelib.norm

# ---------------------------------------------------------------------------
# THE METRIC PENALIZES EXTRA OPS
#
# The score's denominator is the union: all of wl's ops plus those the port
# emits and wl does not. It reaches 100% only if the two streams coincide.
#
# The port's insertions are NOT free: the driver must emit wl's ops, not its
# own, and one access too many to a register is as much a defect as a missing
# one -- it can leave the hardware in a state the stock driver never
# produces. With the denominator on wl's ops alone, removing 6000 spurious
# ops from the port does not move the number by a unit, and the progress
# stays invisible.
#
# The ops the port legitimately emits and wl does not have go in PORT_ONLY,
# with the reason written down. The list is empty on purpose.

# PERIMETER: ops the harness cannot emit, not ops that do not concern us.
#
# The goal is for b43 to emit ALL of wl's ops one by one, the core's
# included. So this is not a list of things out of scope: it is the limit of
# the harness, which compiles only the PHY files and not main.c, xmit.c or
# bcma. What ends up here is debt, and is tracked in docs/retrace-todo.md.
#
# So the number to look at is the one WITHOUT the perimeter, and
# --no-perimeter is the way to get it. The perimeter serves one purpose: to
# find the PHY's next divergence without stopping on the first core op. It is
# a navigation tool, not a score.
#
# The criterion is ownership: only what can be shown to belong to someone
# else is dropped. NOT reachability from the PHY, which would be degenerate
# -- the denominator would shrink when the port shrinks, and removing code
# would raise the score.
#
# The evidence:
#   CORE_SHM     names and blocks of the kernel's b43.h (drivers/net/wireless/
#                broadcom/b43/b43.h); the block sizes come from there.
#   TPL.RAMW     template RAM, which b43 writes in
#                b43_write_template_common() and
#                b43_write_mac_bssid_templates().
#   MAC.MHF.RD   b43_hf_read() is in the core; the PHY has no call to it, and
#                the PHY's only access is mhf_maskset().
#   OTP, SROMCTL bcma's srom code.
#
# What is NOT here, and still counts against us even when it is in all
# likelihood the core's: the counters 0x768-0x790, 0x7d6-0x7e6 and
# 0x10c-0x15e, which b43.h does not name because b43 never reads the
# statistics, and the regions 0x8ec-0xa8e, 0x10f4-0x14b2, 0x0020,
# 0x018a-0x018c, 0x0eec. Moving them here takes evidence, not a better
# number.
# ---------------------------------------------------------------------------
CORE_SHM = [
    (0x0000, 0x0006, "UCODEREV/PATCH/DATE/TIME"),
    (0x0008, 0x0008, "PCTLWDPOS"),
    (0x000e, 0x000e, "EDCFSTAT"),
    (0x0012, 0x0012, "DTIMPER"),
    (0x001e, 0x001e, "TIMBPOS"),
    (0x0022, 0x0022, "ACKCTSPHYCTL"),
    (0x0030, 0x0030, "TXFCUR"),
    (0x0034, 0x0034, "RXPADOFF"),
    (0x003e, 0x003e, "NRRXTRANS"),
    (0x0040, 0x0042, "UCODESTAT/FWCAPA"),
    (0x0048, 0x0048, "PRSSIDLEN"),
    (0x004a, 0x004c, "PRTLEN/NOSLPZNATDTIM"),
    (0x0054, 0x0056, "BEACPHYCTL/KTP. PHYVER (0x0050) and PHYTYPE (0x0052) "
                     "left the perimeter: the port writes them from "
                     "phy.rev and phy.type in shm_readback_block(), where "
                     "the capture has them"),
    (0x0058, 0x0058, "TSSI_CCK, the half the port does not write: 0x005a "
                     "is written by channel_setup()"),
    (0x0066, 0x0066, "RADAR"),
    (0x0068, 0x006a, "BT_BASE0 / TSSI_OFDM_A, 32 bit"),
    (0x006e, 0x006e, "PHYTXNOI"),
    (0x0070, 0x0072, "TSSI_OFDM_G / RFRXSP1, 32 bit"),
    (0x0088, 0x008a, "JSSI0/JSSI1"),
    (0x0098, 0x009e, "SIZE01..SIZE67. SPUWKUP (0x0094) and PRETBTT (0x0096) "
                     "left the perimeter: the port writes them, the first "
                     "as the AC member of brcmsmac's per-PHY family and the "
                     "second with this core's value"),
    (0x00a8, 0x00a8, "MCASTCOOKIE"),
    (0x00b0, 0x00b0, "EXTNPHYCTL"),
    (0x00b6, 0x00b6, "BCN_LI"),
    (0x0160, 0x017e, "PRSSID, 32-byte SSID"),
    (0x0180, 0x0186, "probe response timings: not named in b43.h, but b43 "
                     "turns the offload off with PRMAXTIME=1 and never "
                     "writes them. See docs/retrace-todo.md"),
    (0x0188, 0x0188, "PRPHYCTL"),
    (0x01c0, 0x023e, "rate tables: OFDMDIRECT/BASIC, CCKDIRECT/BASIC"),
    (0x0318, 0x05d3, "TKIPTSCTTAK, 50 entries of 14 bytes"),
    (0x05dc, 0x05de, "0x05dc, written only once and outside the four "
                     "occurrences of the block: it carries the same coremask "
                     "but where from is not established"),
]

# Cells that were in CORE_SHM and were REMOVED because the port writes them:
# 0x0010 SLOTT, 0x0016 WLCOREREV, 0x001c BTSFOFF, 0x003c DEFAULTIV,
# 0x0044/0x0046 SFFBLIM/LFFBLIM, 0x005c ANTSWAP, 0x005e-0x0062 HOSTF1-3,
# 0x0064 RFATT, 0x0078 HOSTF4, 0x0080 MAXBFRAMES, 0x00c0/0x00c2 MACHW,
# 0x00d4 HOSTF5, 0x0018/0x001a BTL0/BTL1,
# 0x0240-0x02be the four EDCFQ blocks, 0x05f4 PSM,
# 0x05e0-0x05f2 the tail of KEYIDXBLOCK, which the port clears in
# set_channel's 0x05e0-0x0666 run,
# 0x05d6/0x05d8 the two middle cells of the chainmask block, which
# b43_phy_ac_chainmask_block() now writes at all four sites.
# 0x0100 CHAN_5GHZ and 0x0108 BCMCFIFOID, which the vendor touches only
# inside the dump of the 0x00e0-0x015e region, and
# b43_phy_ac_wd_region_dump() reads all of it.
#
# Removing them is not optional: the perimeter drops ops from the vendor side
# only, so a cell the port emits and the perimeter drops becomes an insertion
# without a counterpart and breaks the positional comparison there -- the
# positional gate stops on a divergence while the LCS rises, which is the
# symptom. That is why the list is narrowed every time the port learns to
# write a cell.

# Cells b43.h names as the MAC's but the AC PHY really uses, with the
# citation. They stay in the denominator.
#
# Adding an entry here can only lower the score or leave it where it is,
# never raise it: it keeps a vendor op in the comparison, so if the port
# emits it numerator and denominator grow together, and if it does not only
# the denominator grows. It is the safe direction, and the reason this list
# does not need the caution CORE_SHM does.
PHY_ALSO = [
    (0x008c, 0x008c, "JSSIAUX per b43.h; read by wd_stats_tail()"),
    (0x00a0, 0x00a0, "CHAN per b43.h; it is B43_SHM_AC_CHANSPEC in "
                     "b43/phy_ac.h, and write_chanspec() writes it"),
    (0x01c0, 0x01de, "DIRMAP_A: set_channel's scan reads all of it, and "
                     "prb_rsp_rate_po() uses eight entries of it to compute "
                     "2*entry+offset"),
    (0x0200, 0x021e, "DIRMAP_B: read by the scan, and the twelve-rate loop "
                     "uses four entries of it for the CCK blocks"),
    (0x01e0, 0x01fe, "BBRSMAP_A: written by b43_phy_ac_basic_rate_map()"),
    # BSS config cells emit_core_bss_config() emits as a duplicate of the
    # core. None of them has an early write in the capture, so removing them
    # uncovers no op the port does not emit. 0x0018 (BTL0) stays out: the
    # vendor also writes it at #655, inside the chip init block.
    (0x0012, 0x0012, "DTIMPER, emitted by emit_core_bss_config()"),
    (0x0018, 0x0018, "BTL0: 7 from emit_core_shm_chipinit(), 0x012a from "
                     "emit_core_bss_config()"),
    (0x001e, 0x001e, "TIMBPOS, same"),
    (0x0022, 0x0022, "ACKCTSPHYCTL, same"),
    (0x0048, 0x0048, "PRSSIDLEN, same"),
    (0x004a, 0x004a, "PRTLEN, same"),
    (0x0160, 0x017e, "PRSSID, the 32-byte SSID, same"),
    (0x0056, 0x0056, "closes the scan of the direct-map tables"),
]

# Ops the vendor emits and NO b43 code can emit, because they correspond to
# a function boundary b43 does not have and have no effect on the hardware.
# They are dropped from the vendor side only.
#
# This is not the perimeter, which drops other owners' cells, and not
# PORT_ONLY, which drops port ops without an oracle. It is a third category
# and must stay short: every entry is a piece of the goal declared
# unreachable, and it needs evidence that there is nothing to emit.
#
#   MARK     a record of the tracer, not of the driver: written by the sweep
#            script from userspace ('chNN bwB', before the rmmod of the
#            previous channel) or by the wl-diag module itself ('mod
#            COMING'/'mod GOING'). There is no register behind it, so no code
#            can emit it. It enters the window only since the cold segments
#            reach 'mod GOING'.
#   REG.RD 0x0160
#            the reread of objaddr in a capture taken at the bus: the stock
#            driver rereads the address after writing it, at every shared
#            memory access (brcmsmac's brcms_b_read_objmem() and
#            brcms_b_write_objmem()), and mmio2ops.py leaves it as an op of
#            its own. b43 writes the control word with b43_shm_control_word()
#            without rereading it, even where b43_flush_writes() is true, so
#            it has nothing to emit; the OBJ that follows is on both sides and
#            is compared.
#   IOCTL, IOVAR.SET
#            the userspace commands, from the hook on wlc_ioctl(): `wl up`,
#            `wl chanspec`, `wl phycal_tempdelta`. They are the driver's
#            inputs, not its operations: in b43 the same input comes from
#            mac80211/cfg80211, and its effect -- if it has one on the
#            registers -- is in the ops that follow and is compared there.
#   probe response offload   PRTLEN (0x004a), PRSSID (0x0160-0x017e),
#            PRSSIDLEN (0x0048), the timings 0x0180-0x0186 and the template
#            RAM word 0x0700. b43 does not have the firmware answer probes:
#            it writes PRMAXTIME=1 in b43_wireless_core_init() with the
#            comment that a MaxTime of one microsecond always triggers the
#            timeout, "so we never send any probe resp". Consistently the
#            three template cells are defined in b43.h and never written --
#            no use of PRSSID, PRSSIDLEN and PRTLEN in main.c -- and
#            0x0180-0x0186 are not even named. mac80211 builds the probe
#            responses in software, so there is nothing to emit and the AP
#            stays discoverable.
#            A CHOICE OF THE WIP, NOT A LIMIT: the offload is skipped for
#            now. When it is implemented -- "Probe-response offload" in
#            docs/retrace-todo.md -- this entry goes and the comparison gets
#            stricter, the right direction. Note that PRMAXTIME (0x0074) is
#            NOT here: the vendor writes it once with 0, b43 twice (0 in
#            b43_chip_init, then 1), so there is a counterpart and a value
#            divergence there, not an absence.
#            HOSTF5 bit 15 belongs to the same offload: the MAC.MHF slot 4
#            mask 0x8000 with which the vendor raises it on the first
#            bring-up and lowers it on the later ones (cold01 #9943). The 784
#            ucode reads it at 0x0AF2 and 0x0CAC on the probe request's RX
#            path: at 1 it consumes it -- answers from the template and drops
#            the frame, r20 bit 0 -> flush at 0x0A27 -- at 0 it passes it to
#            the host. b43 keeps it at 0, because hostapd makes the probe
#            responses, and does not emit the MAC.MHF; the OBJ.WR of 0x00d4
#            that follows is on both sides and is compared under
#            VAL_BITS_OUTSIDE_PERIMETER.
VENDOR_ONLY = (
    r"^MARK\b",
    r'^REG\.RD off=0x160 ',
    r"^IOCTL\b",
    r"^IOVAR\.SET\b",
    r'^OBJ\.WR addr=0x(?:48|4a|18[0246])(?: |$)',
    r'^OBJ\.WR addr=0x1(?:6[02468ace]|7[02468ace])(?: |$)',
    r'^TPL\.RAMW addr=0x700(?: |$)',
    r'^MAC\.MHF addr=0x4 val=0x(?:8000|0) mask=0x8000$',
)

# Ops the PORT emits and the vendor legitimately does not have. They are
# dropped from the test side only, and are needed because the score
# penalizes extra ops: without this list a right op without a counterpart
# would weigh as a defect.
#
# Every entry is argued here, as for VENDOR_ONLY: an op without a
# counterpart usually means the port does something too many, not that the
# metric needs adjusting. The legitimate cases are two -- an op b43 must do
# for its structure where wl does a different one, and a right op whose
# counterpart exists but was not captured because the hook came later. The
# second is temporary by definition: a recapture closes it.
# There is a third legitimate case, and it is named instead of passing its
# entries under one of the two above: an op mainline b43 emits, outside the
# PHY, that the stock driver does not have at all. It is not the port's
# debt, because there is nothing in the PHY to fix, and it cannot be paid
# without touching main.c. The criterion is the perimeter's -- drop only what
# is shown to be someone else's -- applied to the test side.
#
#   AMT.*  the address match table. The port writes it from the core
#          (`b43_amt_write()` in b43/main.c), derived from the DSL-3580L's
#          cold capture. It is not an op too many: it is a right op that a
#          capture without the hook on `wlc_bmac_write_amt` cannot contain.
#          So it is in PORT_ONLY_UNTRACED and not here: it counts only
#          against a capture that does not trace the class, and turns itself
#          off against one that does -- the d6220 and the DSL today, the
#          agcombo once recaptured.
#
#   OBJ on 0x0004/0x0006 and the three constants on 0x0000/0x0002
#          the unaligned half of b43_validate_chipaccess(). The aligned half,
#          which the vendor executes op by op at #511-#522 of the reference
#          segment, is NOT here and is compared: 12 ops, and they match.
#          These are the 16 that remain -- the backup and restore of
#          0x0004/0x0006, the four write16 of the pattern, the unaligned
#          read32 and the write32 that follows it.
#
#          The two pieces of evidence. The cells 0x0004/0x0006: the vendor
#          touches them in no capture -- zero occurrences on all 26 cold
#          segments, on the steady-state tick and on the DSL capture -- and
#          the PHY writes no SHM below 0x000c, so an op there cannot be the
#          port's in the sense that matters. The constants 0x1122/0x3344/
#          0xccdd on 0x0000/0x0002: they appear as an OBJ value on those cells
#          in no capture, and in b43 they exist only inside that self-test.
#
#          The pattern is tied to the values on purpose: if mainline's
#          constants change it stops matching and the ops count again, the
#          right way to be wrong.
#
#          It is the only entry of this list that no recapture closes: the
#          vendor does not run that test and never will.
#   REG.WR 0x49c   the MAC's GPIO_CONTROL, how b43 drives the LEDs: leds.c
#          read-modify-writes the cell at every state change, in
#          b43_leds_init(), b43_leds_exit() and from the triggers. The vendor
#          drives the same LEDs with si_gpioout on the chipcommon -- the ops
#          PERIMETER declares the core's -- so here the counterpart is not
#          there and never will be: same function, different register, by
#          structure. It appears only with the --bus profile
#          (test/integration): test/unit's harness does not compile leds.c.
PORT_ONLY = (
    r'^OBJ\.(RD|WR) addr=0x0*[46] ',
    r'^OBJ\.(RD|WR) addr=0x0*[02] val=0x0*(1122|3344|ccdd)\b',
    r'^REG\.WR off=0x49c\b',
)

# The second case of the list above: the counterpart exists, but the capture
# does not trace that class. Temporary by definition, and the capture decides
# the condition instead of the reader -- as soon as the vendor side carries an
# op of the class the entry turns off and the comparison gets stricter,
# without anyone having to remember to come and remove it. Same criterion as
# check_class_coverage.py and wrap.c's oracle_has_* flags.
#
#   REG.*, MAC.MCMD  the MAC's MMIO registers: the vendor tracer hooks the
#          access functions of PHY, radio and shared memory, not raw MMIO,
#          and no wl-diag capture carries an op of these classes. The noise
#          sample request goes through them, as do the interrupt ACKs and
#          masks and the beacon templates' valid bit. The captures taken at
#          the bus (reverse-tools/mmio2ops.py) carry them, and against those
#          the entry turns off.
#   WRAP.*  the core's agent space: reset, clock and the IOCTL PHY bits. Same
#          case, only the bus captures see it.
#   PHY.WRW the second and third word of a 48-bit cell, which
#          wlc_phy_write_table_ext() writes on the data port without going
#          through phy_reg_write(), the hooked accessor. The bus captures
#          carry them.
PORT_ONLY_UNTRACED = (
    (r'^AMT\.', 'AMT.'),
    (r'^REG\.', 'REG.'),
    (r'^MAC\.MCMD\b', 'MAC.MCMD'),
    (r'^WRAP\.', 'WRAP.'),
    (r'^PHY\.WRW\b', 'PHY.WRW'),
)


def _any_pattern(patterns):
    return re.compile('|'.join(f'(?:{p})' for p in patterns)).search


def drop_port_only(ops, vendor=()):
    """Remove from the port the ops the vendor legitimately does not have."""
    pats = list(PORT_ONLY)
    for pat, cls in PORT_ONLY_UNTRACED:
        if not any(op.startswith(cls) for op in vendor):
            pats.append(pat)
    search = _any_pattern(pats)
    out, dropped = [], []
    for op in ops:
        (dropped if search(op) else out).append(op)
    return out, dropped


def drop_power_save(ops):
    """Remove from the vendor its power saving without a BSS.

    Without a BSS up, on the third watchdog round after `wl up` wl reads
    table 13 at entry 0x5f (PHY.WR 0x72 = 0x345f), stops the DMAs, puts the
    core in reset, and then every 2 s takes it out and puts it back (the PHY
    off, the radio switch read) until the configuration after brings it back
    up. b43 does not have this power saving and does not emit it: a choice,
    not a debt of the port, and it does not count in the score.

    The stretch goes from that read to the first MARK after it, if before
    the MARK the core goes down: the reset raised (WRAP.WR 0x800 = 1) or the
    clock removed (WRAP.WR 0x408 = 0), which stays even where the score takes
    only the I/O control. The same read inside a configuration, without the
    core going down after it, stays. To be called before drop_vendor_only(),
    which removes the MARKs."""
    down = re.compile(r'^WRAP\.WR off=0x(?:800 val=0x1|408 val=0x0)(?: |$)')
    out, dropped, i = [], [], 0
    while i < len(ops):
        if ops[i].startswith('PHY.WR addr=0x72 val=0x345f'):
            j = i
            while j < len(ops) and not ops[j].startswith('MARK'):
                j += 1
            if any(down.match(o) for o in ops[i:j]):
                dropped += ops[i:j]
                i = j
                continue
        out.append(ops[i])
        i += 1
    return out, dropped


def drop_vendor_only(ops):
    """Remove from the vendor the ops no b43 code can emit."""
    search = _any_pattern(VENDOR_ONLY)
    out, dropped = [], []
    for op in ops:
        (dropped if search(op) else out).append(op)
    return out, dropped


PERIMETER = [
    dict(pattern=r'^OBJ\.(RD|WR) ', core_shm=True,
         reason="shared memory cells the kernel's b43.h declares the "
                "core's. They are the only OBJ ops dropped: the rest of the "
                "window stays in the denominator even when it is in all "
                "likelihood the core's, because we have no evidence and "
                "erring that way would inflate the score. See the block "
                "above for the criterion and for why reachability from the "
                "PHY is not used."),

    dict(pattern=r'^TPL\.RAMW addr=0x48\b',
         reason="template RAM, cell 0x0048, which stays someone else's. The "
                "others left the perimeter: the port writes 0x0200 and 0x0480 "
                "from the duplicates emit_core_bss_ssid() and 0x0700 from "
                "emit_core_prb_rsp_template(), which are the kernel's main.c "
                "code mirrored in the harness. Not this one: its two "
                "occurrences, at #12960 and #13584, fall on no boundary the "
                "harness controls -- the first follows the 0x018a/0x018c "
                "pair the PHY emits, the second the counter sweep -- so there "
                "is no place to put it without splitting a PHY function. The "
                "PHY does not touch template RAM."),

    dict(pattern=r'^MAC\.MHF\.RD\b',
         reason="bare read of the host flags. The PHY never reads them on "
                "its own: its only access is b43_phy_ac_mhf_maskset(), which "
                "the harness traces as MAC.MHF with a mask, and the PHY has "
                "no call to b43_hf_read(). In the segment it appears only "
                "once, at #13537, in the middle of the core's MAC config "
                "block -- between OBJ.WR 0x60 and the MAC suspend -- and it "
                "is main.c's b43_hf_read()."),

    dict(pattern=r'^(OTP\.|SROMCTL\.)',
         reason="OTP and the SROM control register: read by bcma's srom "
                "code. Not out of scope -- bcma/sprom.c is touched by the "
                "port, which adds the rev 11 extraction -- but outside the "
                "PHY, and the harness mounts the board profile directly "
                "instead of reading the SROM. In the cold attach they fall "
                "in the middle of the analog sequence, between the ten save "
                "reads and the AFE bank, so without this entry the "
                "positional comparison stops there.\n"
                "TODO: check that the three ops of the segment (SROMCTL.RD, "
                "OTP.RDW, OTP.INIT at #548-#551 on cold01) are the ones "
                "bcma/drivers/bcma/sprom.c already emits. If they are, the "
                "skip is final and the comparison window has to start after "
                "them -- that is what this tool's --range is for. If they "
                "are not, it is a hole in the patch."),

    dict(pattern=r'^(GPIO\.(CTL|OUT|OE) |SI\.COREREG core=0x0 off=0x8c\b)',
         led=True,
         reason="the LEDs. In the blob (wlD6220.o) they are four core "
                "functions, all on chipcommon: wlc_bmac_hw_up() at attach -- "
                "si_gpiocontrol(mask, 0), si_gpioled(mask, mask) which is "
                "the gpiotimeroutmask write at 0x8c, si_gpioout(mask, off) "
                "with the bit raised for the active-low LEDs, "
                "si_gpioouten(mask, mask) -- wlc_bmac_led() to turn the "
                "single LEDs on and off at bss-up and down (gpio 2 and gpio "
                "10, the latter active-low: 0x0004 and 0x0400), and "
                "wlc_bmac_led_hw_deinit() at rmmod, which releases the mask "
                "with out, outen and gpioled at zero. Each touches a "
                "non-empty subset of the board's LED pins, which the harness "
                "derives from the profile's SROM (./ac_trace led_pins BOARD) "
                "and come with --led-pins; the GPIO ops with a mask outside "
                "those pins stay in the comparison. They have no effect on "
                "the PHY, and in b43 the LEDs are leds.c's, from the SROM's "
                "gpio0-3 fields, its own way (the MAC's GPIO_CONTROL MMIO) "
                "and not the vendor's; pins 4-15 from the NVRAM's ledbh are "
                "taught to it by gpio_ext[] in bcma/ and leds.c in b43/. See "
                "README.md, \"What is missing\"."),

    dict(pattern=r'^SI\.COREREG core=0x0 off=0x80\b',
         reason="the chipcommon watchdog, written only once at the end of "
                "wl1's detach: after the LED release, and the core's last op "
                "before the chip reset. In the stock driver it is "
                "pcie_watchdog_reset(), which resets the chip with the "
                "watchdog before releasing the PCIe device; the register is "
                "bcma's (BCMA_CC_WATCHDOG) and bcma's removal path has "
                "nothing similar, see docs/retrace-todo.md, \"MAC and DMA, "
                "from the bus captures\"."),

    dict(pattern=r'^CAL\.INIT\b',
         reason="the stock driver's calibration forcing switch. In this "
                "build the record carries neither address nor value, so "
                "there is nothing to reproduce."),
]


ADDR = re.compile(r'\baddr=(0x[0-9a-f]+)')
SPACE = re.compile(r'^([A-Z]+)\.')


def _cell(op):
    m = ADDR.search(op)
    return int(m.group(1), 16) if m else None


def _core_shm(op):
    a = _cell(op)
    if a is None:
        return False
    if any(lo <= a <= hi for lo, hi, _ in PHY_ALSO):
        return False
    return any(lo <= a <= hi for lo, hi, _ in CORE_SHM)


GPIO_MASK = re.compile(r'^GPIO\.\w+ .*\bmask=(0x[0-9a-f]+)')


def _led(op, pins):
    m = GPIO_MASK.match(op)
    if not m:
        return True
    mask = int(m.group(1), 16)
    return mask != 0 and not mask & ~pins


def apply_perimeter(ops, led_pins=0):
    """Remove the vendor ops that can be shown to be someone else's.

    Returns (inside, dropped, cells). The third item is the set of (space,
    address) dropped: it is to be looked at, not counted, because if a PHY
    cell ended up in there the count would add up with the omission hidden.
    """
    searches = tuple(re.compile(r['pattern']).search for r in PERIMETER)

    @lru_cache(maxsize=32768)
    def match(op):
        for k, (r, search) in enumerate(zip(PERIMETER, searches)):
            if not search(op):
                continue
            if r.get('core_shm') and not _core_shm(op):
                continue
            if r.get('led') and not _led(op, led_pins):
                continue
            return k
        return None

    out, dropped, keys = [], [], set()
    for op in ops:
        hit = match(op)
        if hit is None:
            out.append(op)
        else:
            dropped.append((op, hit))
            m, s = ADDR.search(op), SPACE.match(op)
            if m and s:
                keys.add((s.group(1), m.group(1)))
    return out, dropped, keys


VAL_TOK = re.compile(r'val=(?:0x[0-9a-fA-F]+|UNDEFINED)')

# Cells whose value no code can predict, so address, class and position are
# compared but not the value.
#
# BSLOTS and REGGAP of the four EDCFQ blocks: BSLOTS is the backoff drawn at
# random at the start of the contention window, and REGGAP is AIFS + BSLOTS.
# Measured on all 43 segments and all four queues, 172 points: BSLOTS falls
# uniformly in [0, CWMIN] -- 0..15 on best effort and background, 0..7 on
# video, 0..3 on voice -- and the REGGAP relation holds on each of the 172.
# It is not an exception hiding a defect: it is a nondeterministic cell, and
# requiring its value would mean guessing a random number.
VAL_NONDET = [
    (0x0240, 0x02be, 0x0a, "BSLOTS, backoff drawn at random"),
    (0x0240, 0x02be, 0x0c, "REGGAP = AIFS + BSLOTS"),
]


# Cells whose value is compared with a tolerance, because the port computes
# it and the last bit is not reproducible. Empty: the RX IQ's b coefficient
# (0x?a1) was here until the solve followed the vendor's fixed-point
# arithmetic (docs/rxiq-cal-analysis.md, "The solve"); now the 316 points of
# reverse-tools/rxiq_points.py are exact and the cell is compared to the bit
# like the others. An entry here suspends a piece of the goal: it comes with
# the measure of the residual that justifies it.
VAL_TOLERANCE = [
]


# Bits of a cell on which the port departs from the vendor by choice: the
# comparison ignores them and binds all the other bits of the same write.
# Every entry is a documented decision of the port, not a residual to
# measure.
#
# HOSTF5 (0x00d4) bit 15, the probe response offload: see the entry in
# VENDOR_ONLY. The vendor writes 0x8088 on the first bring-up, the port
# 0x0088.
# HOSTF2 (0x0060) bit 0, the microcode's probe response: the vendor clears it
# on the first bring-up, the port always keeps it on
# (b43_phy_ac_op_switch_channel()).
VAL_BITS_OUTSIDE_PERIMETER = [
    (0x00d4, 0x8000, "HOSTF5 bit 15, probe response offload"),
    (0x0060, 0x0001, "HOSTF2 bit 0, the microcode's probe response"),
]


def _s(v: int, bits: int) -> int:
    """Interpret @v as a signed integer on @bits bits."""
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def val_within_tolerance(v: str, t: str) -> bool:
    """Do the two ops write the same cell with values within the tolerance?"""
    mv = re.match(r'PHY\.WR\s+addr=0x([0-9a-fA-F]+)\s+val=0x([0-9a-fA-F]+)', v)
    mt = re.match(r'PHY\.WR\s+addr=0x([0-9a-fA-F]+)\s+val=0x([0-9a-fA-F]+)', t)
    if not mv or not mt or mv.group(1) != mt.group(1):
        return False
    a = int(mv.group(1), 16)
    for addr, bits, tol, _ in VAL_TOLERANCE:
        if a != addr:
            continue
        d = _s(int(mv.group(2), 16), bits) - _s(int(mt.group(2), 16), bits)
        return abs(d) <= tol
    return False


def val_nondet(op: str) -> bool:
    """Does the op write a cell whose value is not predictable?"""
    m = re.match(r'OBJ\.WR\s+addr=0x([0-9a-fA-F]+)', op)
    if not m:
        return False
    a = int(m.group(1), 16)
    return any(lo <= a <= hi and (a - lo) % 0x20 == off
               for lo, hi, off, _ in VAL_NONDET)

RET_SUFFIX = re.compile(r'\s+ret=0x[0-9a-fA-F]+')

MASKED_OP = re.compile(r'^(\S+(?: addr=\S+)?) val=(0x[0-9a-fA-F]+) mask=(0x[0-9a-fA-F]+)$')
PLAIN_OP = re.compile(r'^(\S+(?: addr=\S+)?) val=(0x[0-9a-fA-F]+)$')

def val_within_mask(v: str, t: str) -> bool:
    """A vendor op with a mask against a port op with a full value: the write
    in which unfold_bus() unrolled a MOD, or a MAC.MCTRL, which the vendor
    traces as a maskset and the bus as a whole write. The vendor binds only
    the mask's bits; it read the others, and the port read them in turn from
    the oracle, which has no value for those reads."""
    mv, mt = MASKED_OP.match(v), PLAIN_OP.match(t)
    if not (mv and mt) or mv.group(1) != mt.group(1):
        return False
    mask = int(mv.group(3), 16)
    return (int(mv.group(2), 16) & mask) == (int(mt.group(2), 16) & mask)

OBJ_WR_VAL = re.compile(r'^(OBJ\.WR addr=0x([0-9a-fA-F]+) val=)(0x[0-9a-fA-F]+)$')

def strip_bits_outside_perimeter(op: str) -> str:
    """The op with the bits of VAL_BITS_OUTSIDE_PERIMETER removed from the
    value, if it writes one of their cells; otherwise the op as it is."""
    m = OBJ_WR_VAL.match(op)
    if not m:
        return op
    a = int(m.group(2), 16)
    for addr, bits, _ in VAL_BITS_OUTSIDE_PERIMETER:
        if a == addr:
            return f"{m.group(1)}{int(m.group(3), 16) & ~bits:#x}"
    return op


def ops_equal(v: str, t: str) -> bool:
    """Op-by-op comparison, with the value read treated as a wildcard when
    the vendor did not record it.

    The captures made without "capture ret val" log every read as
    val=UNDEFINED: there the value is not comparable and is ignored, while
    the address and the op class stay binding. On the captures with the
    RETVALs folded (`trace_filter.py --retvals`) the value is there and is
    compared."""
    # bcma's read-modify-write ops (PMU.RC, GPIO.*) carry in the vendor trace
    # the value read back after the change; the harness's bcma stubs do not
    # model that readback, so the suffix is not comparable.
    v = RET_SUFFIX.sub('', v)
    if v == t:
        return True
    if val_within_mask(v, t):
        return True
    if strip_bits_outside_perimeter(v) == strip_bits_outside_perimeter(t):
        return True
    if val_nondet(v):
        return VAL_TOK.sub('val=*', v, count=1) == VAL_TOK.sub('val=*', t, count=1)
    if val_within_tolerance(v, t):
        return True
    if 'val=UNDEFINED' in v:
        return VAL_TOK.sub('val=*', v, count=1) == VAL_TOK.sub('val=*', t, count=1)
    return False

def extract_episode(raw: str) -> int:
    m = re.search(r'#(\d+)', raw)
    return int(m.group(1)) if m else -1

def op_forms(raws, profile):
    """A trace's normalized ops: one per line, or those of the `bus` profile,
    which unrolls the MODs and knows the RAD.MOD + RAD.RD pair."""
    if profile == 'bus':
        return tracelib.unfold_bus_seq(raws)
    return [normalize_op(r) for r in raws]

VENDOR_MOD = re.compile(r'^((?:PHY|RAD)\.MOD addr=\S+) val=(0x[0-9a-f]+) mask=(0x[0-9a-f]+)$')

def mask_vendor_mod(op):
    """A vendor MOD's value restricted to its mask.

    The hook records the `val` argument of mod_phy_reg()/mod_radio_reg() as
    the caller passes it, and the hardware sees only its bits under the mask:
    in brcmsmac `val &= mask` in mod_phy_reg() and `(rval & ~mask) | (val &
    mask)` in mod_radio_reg() (phy_cmn.c). The vendor passes for instance a
    negative offset of the 0x0910 bank as 0xffff under mask 0x00ff, the same
    write as the port's 0x00ff. Vendor side only: on the port a bit outside
    the mask is a real write of b43_phy_maskset() and must be seen."""
    m = VENDOR_MOD.match(op)
    if not m:
        return op
    mask = int(m.group(3), 16)
    if not mask:
        return op
    return f"{m.group(1)} val={int(m.group(2), 16) & mask:#x} mask={m.group(3)}"

def load_vendor(path, ep_range, profile=None, expanded=None):
    lo, hi = ep_range or (0, 10**9)
    raws = []
    for line in open(path):
        m = VENDOR_LINE.match(line)
        if not m:
            continue
        ep = extract_episode(line)
        if not (lo <= ep <= hi):
            continue
        raws.append(m.group(1))
    raws = expand_bulk_heads(raws, expanded)
    ops = [mask_vendor_mod(o) for o in op_forms(raws, profile)]
    return drop_shadow_ops(resolve_wide_reads(ops))

def load_test(path, profile=None):
    raws = [m.group(1) for line in open(path) for m in [TEST_LINE.match(line)] if m]
    return resolve_wide_reads(op_forms(raws, profile))

def find_offset(test, target_op):
    """Return the index of `target_op` in test, or -1.

    Uses ops_equal, not exact equality: if the vendor did not record the
    value read the anchor must not depend on it."""
    for i, op in enumerate(test):
        if ops_equal(target_op, op):
            return i
    return -1

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument('vendor')
    ap.add_argument('test')
    ap.add_argument('--range', help='LO:HI vendor episode range')
    ap.add_argument('--bus', action='store_true',
                    help='bus profile: MODs unrolled into RD+WR, TBL.* '
                         'dropped; for a trace taken at the MMIO bus '
                         '(test/integration)')
    ap.add_argument('--led-pins', type=lambda v: int(v, 0), default=None,
                    help='GPIO pins of the board\'s LEDs, from ./ac_trace '
                         'led_pins BOARD. Without it, the vendor\'s LED '
                         'block stays in the comparison')
    ap.add_argument('--auto-align', action='store_true',
                    help='skip test prologue by aligning on vendor[0]')
    ap.add_argument('--align-on', help='align test on this exact op string')
    ap.add_argument('--no-perimeter', action='store_true',
                    help='ALSO compare the ops outside the PHY\'s perimeter. '
                         'The result is not a measure of the port: it stops '
                         'on the first op of the b43 core, which the harness '
                         'does not compile. It is for inspecting a capture, '
                         'not for giving a number')
    ap.add_argument('--perimeter-addr', action='store_true',
                    help='list the cells the perimeter drops: the set is to '
                         'be looked at, because a cell the port should touch '
                         'and does not ends up here')
    args = ap.parse_args()

    rng = None
    if args.range:
        lo, hi = args.range.split(':')
        rng = (int(lo), int(hi))

    profile = 'bus' if args.bus else None
    expanded = {}
    vendor = load_vendor(args.vendor, rng, profile, expanded)
    test = load_test(args.test, profile)
    if expanded:
        print(f"bulk expanded: {format_expanded(expanded)}")

    vendor, sv = drop_vendor_only(vendor)
    test, sp = drop_port_only(test, vendor)
    if sv:
        print(f"vendor only: {len(sv)} ops dropped because no b43 code "
              f"can emit them; see VENDOR_ONLY in this file")
    if sp:
        print(f"port only: {len(sp)} port ops dropped because the oracle "
              f"cannot contain them; see PORT_ONLY in this file")

    if not args.no_perimeter:
        if args.led_pins is None:
            print("LED pins not given: the vendor's LED ops stay in the "
                  "comparison; see --led-pins")
        vendor, outside, keys = apply_perimeter(vendor, args.led_pins or 0)
        print(f"outside perimeter: {len(outside)} vendor ops dropped on "
              f"{len(keys)} cells declared someone else's; "
              f"see PERIMETER in this file")
        if args.perimeter_addr:
            for space in sorted({s for s, _ in keys}):
                a = sorted((x for s, x in keys if s == space),
                           key=lambda v: int(v, 16))
                print(f"  {space}: " + " ".join(a))

    if args.align_on:
        off = find_offset(test, args.align_on)
        if off < 0:
            print(f"align-on: op not found in test: {args.align_on}")
            return 2
        print(f"aligning test at offset {off} (--align-on)")
        test = test[off:]
    elif args.auto_align and vendor:
        off = find_offset(test, vendor[0])
        if off < 0:
            print(f"auto-align: vendor[0] not found in test: {vendor[0]}")
        else:
            print(f"aligning test at offset {off} (auto: '{vendor[0]}')")
            test = test[off:]

    print(f"vendor: {len(vendor)} ops")
    print(f"test:   {len(test)} ops")

    n = min(len(vendor), len(test))
    mismatches = 0
    for i in range(n):
        if not ops_equal(vendor[i], test[i]):
            mismatches += 1
            if mismatches <= 20:
                print(f"  @{i}:")
                print(f"    vendor: {vendor[i]}")
                print(f"    test:   {test[i]}")
    if len(vendor) != len(test):
        print(f"length differs: vendor={len(vendor)} test={len(test)}")

    if mismatches == 0 and len(vendor) == len(test):
        print("MATCH")
        return 0
    print(f"total mismatches (compared prefix): {mismatches}")
    return 1

if __name__ == '__main__':
    sys.exit(main())
