// SPDX-License-Identifier: GPL-2.0
/*
 * The fake BCMA bus, which emits the ops instead of touching the silicon.
 *
 * The hook is not on the ten `bcmn_*` symbols the link asks for -- those are
 * clock, enable, GPIO and PLL control. Register reads and writes go through
 * the inlines of <bcma-next/bcmn.h>, which dispatch on `core->bus->ops`, a
 * vtable: providing ours is enough, and b43 is left untouched.
 *
 * The line format is ../unit/wrap.c's, so the output can be compared with the
 * vendor captures by the same tools (reverse-tools/tracelib.py,
 * unit/compare.py). The classes are the decoder's: PHY.RD/WR/MOD, RAD.*,
 * OBJ.*, MAC.*, TBL.*.
 *
 * Mind what this file does NOT do: it does not model the hardware. A read
 * returns what the oracle says, and without an oracle it returns zero --
 * which for a self-clearing status bit is the value that spins a poll until
 * the timeout. It is the same problem AC_READ_ORACLE and the read plans
 * solve in ../unit, and here it is solved the same way, not reinvented.
 */
#include <bcma-next/bcmn.h>
#include <linux/kernel.h>

#include "trace_out.h"
#include "chip_link.h"

/*
 * The b43 registers we care about are in the 802.11 core's window. The
 * classes are derived from the offset, as the vendor's decoder does: wl's
 * tracer hooks the functions (wlc_bmac_write_objmem16 and friends) and has
 * the names ready, we only have the offset, so the translation is here. The
 * offsets themselves come from b43.h, not redefined here.
 */
#include "b43.h"
#include "dma.h"


/*
 * The state needed to rebuild an op from several accesses: b43 first writes
 * the address into a control register and then the data into the data one,
 * so the line is emitted on the second access, with the first one's
 * address. It is the same reconstruction the decoder does on the vendor's
 * traces.
 */
static u16 phy_addr;
static u16 radio_addr;
static u32 shm_routing_off;

/*
 * The 16-bit radio interface, RADIO_CONTROL and RADIO_DATA_LOW: the HT-PHY
 * writes radio register 0x39 of each core through it. The decoder of the
 * bus captures folds only the 24-bit one into RAD ops and leaves these two
 * registers raw, so they go out raw here too; the address register reads
 * back what was written.
 */
static u16 radio16_addr;

/*
 * The control register carries, above the index, the routing in the low 8
 * bits of the high half and the two auto-increment bits above it
 * (B43_SHM_AUTOINC_W/_R): a 32-bit access to the data port with the bit of
 * its direction set advances the index by one word, as
 * reverse-tools/mmio2ops.py does for the capture. The ucode upload and the
 * initval lists use it.
 */
#define SHM_CTL_ROUTING		0x00ff0000u
#define SHM_CTL_AUTOINC_W	((u32)B43_SHM_AUTOINC_W << 16)
#define SHM_CTL_AUTOINC_R	((u32)B43_SHM_AUTOINC_R << 16)

/*
 * The oracle cannot serve MACCONTROL: the vendor tracer records the
 * maskset's argument and not the read back, so the 137 MAC.MCTRL of the
 * reference segment do not have a single RETVAL. The driver's writes
 * determine the value entirely -- b43 uses it in read-modify-write, and
 * b43_validate_chipaccess() requires reading back the IHR_ENABLED that
 * b43_wireless_core_reset() just set. So here it is a latch, not a read
 * from the capture.
 */
static u32 macctl;

/*
 * The interrupt reasons the microcode would raise: main.c sets them when the
 * timeline delivers an event, b43 reads them from B43_MMIO_GEN_IRQ_REASON and
 * clears them with the ACK, which on that register is a write of the bits to
 * remove.
 */
static u32 irq_pending;

void b43_test_raise_irq(u32 reason)
{
	/* MAC_SUSPENDED is state and follows MACCONTROL, see the read. */
	irq_pending |= reason & ~B43_IRQ_MAC_SUSPENDED;
}

/*
 * The interrupt state of DMA channel 0, the receive one: RX_DONE when the
 * timeline brings a frame. b43 reads it in B43_MMIO_DMA0_REASON and clears it
 * by writing it back; the other five channels raise nothing here. It does not
 * go through the oracle: the vendor reads the register once per interrupt,
 * b43 once per channel, and the per-address queues would not line up.
 */
static u32 dma0_pending;

/* The receive ring's control register, for its state. */
static u32 rxctl;
/* The address b43 gave the receive ring, for its slots. */
static u32 rx_ringlo;

/*
 * The TX FIFO definitions, which b43 reads back (b43_txfifo_set()) and wl
 * never reads: XMTFIFOCMD selects one by bits 8-10, XMTFIFODEF and
 * XMTFIFODEF1 read back the selected one's. The commit bit 15 does not read
 * back: b43_txfifo_fixup() refuses a selector with it, and succeeds on
 * the R6400 v2 after the core's own FIFO writes.
 */
static u16 txfifo_cmd, txfifo_def[8][2];

static u16 *txfifo_reg(u16 off)
{
	return &txfifo_def[(txfifo_cmd >> 8) & 7][off == B43_MMIO_XMTFIFODEF1];
}

void b43_test_raise_dma0(u32 reason)
{
	dma0_pending |= reason;
}

/*
 * The four cells of b43_validate_chipaccess(), and only those.
 *
 * They are UCODEREV, UCODEPATCH, UCODEDATE and UCODETIME (SHM_SHARED
 * 0x0000-0x0006). While the PSM is stopped they are the driver's RAM: the
 * self-test writes its patterns there and reads them back,
 * b43_upload_microcode() clears them with the rest of the shared memory.
 * When the PSM starts the microcode rewrites them, and from then on the
 * value is its own: the oracle serves it, because there is no microcode
 * here.
 *
 * The model cannot be the oracle even with the PSM stopped. The unaligned
 * half of the self-test uses 0x1122/0x3344/0x5566/0x7788, which `wl` does not
 * run, and version 7.14.43 of the stock driver does not run the test at all:
 * the per-address queues do not have those reads, and serving them from the
 * capture would give the self-test the ucode's values, a failing probe.
 *
 * A read served by the model consumes the head entry of the oracle's queue
 * only if it has the same value: it is the same read the vendor made (the
 * D6220 runs the aligned half, #511-#522 of the reference segment). A
 * different entry belongs to a later read and stays where it is.
 *
 * The write-through is NOT right in general: the cells the ucode updates --
 * the statistics window 0x0300-0x0314, the counters 0x0768-0x0788, the TSSI
 * -- the port clears and reads back, and serving them from the model would
 * return zero instead of the vendor's values, letting the watchdog gate pass
 * while measuring nothing. That is why the list is short.
 */
#define SHM_MODEL_CELLS	4		/* byte 0x0000, 0x0002, 0x0004, 0x0006 */

static u16 shm_model[SHM_MODEL_CELLS];
static bool shm_model_written[SHM_MODEL_CELLS];

static int shm_model_slot(u32 routing, u16 byte_off)
{
	if (routing != B43_SHM_SHARED || (byte_off & 1) ||
	    byte_off >= SHM_MODEL_CELLS * 2)
		return -1;
	return byte_off / 2;
}

static bool shm_model_active(void)
{
	return !(macctl & B43_MACCTL_PSM_RUN);
}

static void shm_model_store(u32 routing, u16 byte_off, u16 val)
{
	int slot = shm_model_slot(routing, byte_off);

	if (slot < 0 || !shm_model_active())
		return;
	shm_model[slot] = val;
	shm_model_written[slot] = true;
}

/* Return true and fill *val if the cell is modelled and already written. */
static bool shm_model_load(u32 routing, u16 byte_off, u16 *val)
{
	int slot = shm_model_slot(routing, byte_off);

	if (slot < 0 || !shm_model_active() || !shm_model_written[slot])
		return false;
	*val = shm_model[slot];
	b43_trace_consume_shm_if(routing << 16, byte_off, *val);
	return true;
}

/*
 * The SHM address the trace has to carry is the offset in BYTES, what the
 * vendor tracer takes from wlc_bmac_read_shm's arguments. That is not what
 * goes on the bus: for B43_SHM_SHARED b43 puts the 32-bit index (offset >> 2)
 * in the control register and tells the two halves apart by the data port it
 * uses -- B43_MMIO_SHM_DATA for the word at byte 4w,
 * B43_MMIO_SHM_DATA_UNALIGNED for the one at 4w+2. b43 does not shift the
 * other routings (SCRATCH, REGS, ...), and there the index is already the
 * address.
 *
 * Without this reconstruction every OBJ op of the suite would come out at
 * offset/4 and match no vendor op: the host flags' write-through would give
 * OBJ.WR addr=0x0017 where the capture has addr=0x005e.
 */
static u16 shm_byte_off(u16 port)
{
	u16 routing = (u16)((shm_routing_off & SHM_CTL_ROUTING) >> 16);
	u16 idx = (u16)(shm_routing_off & 0xffff);

	if (routing != B43_SHM_SHARED)
		return idx;
	return (u16)((idx << 2) + (port == B43_MMIO_SHM_DATA_UNALIGNED ? 2 : 0));
}

/*
 * An aligned 32-bit access is ONE access on the bus and TWO ops for the
 * vendor, which reads and writes the shared memory 16 bits at a time only.
 * The data port is little-endian like the rest of the core: the word at the
 * offset carries the LOW half of the value, the one at +2 the high half. It
 * is the rule of the unaligned branch of b43_shm_write32()/read32(), and the
 * agcombo's bus capture confirms it, where `wl` writes 0x002a0000 at index 5
 * and corerev 42 ends up at B43_SHM_SH_WLCOREREV (0x0016).
 *
 * b43_validate_chipaccess()'s self-test reads back what it wrote from the
 * four cells modelled above, so it does not depend on the values of the
 * vendor's test, which uses 16-bit patterns of its own. See
 * shm_model_slot()'s comment for why the write-through on those is measured
 * and why the list is short.
 */
static void shm_word_write(u32 routing, u16 off, u16 val)
{
	shm_model_store(routing >> 16, off, val);
	b43_trace_shm("OBJ.WR", routing | off, val, 16);
}

static u16 shm_word_read(u32 routing, u16 off)
{
	u16 val;

	if (!shm_model_load(routing >> 16, off, &val))
		val = (u16)b43_trace_read_shm(routing, off, 16);
	b43_trace_shm("OBJ.RD", routing | off, val, 16);
	return val;
}

/*
 * The other routings -- the address match table (RCMTA), the scratch -- do
 * not have the 16-bit half: the index is a 32-bit word's, and the vendor at
 * the bus reads and writes them so, one op per word (OBJ.WR sel=0x40000
 * addr=0x007e val=0x0102c000 on the station's row).
 */
static void shm_autoinc(u32 flag, int width)
{
	if (width == 32 && (shm_routing_off & flag))
		shm_routing_off = (shm_routing_off & ~0xffffu) |
				  (u16)(shm_routing_off + 1);
}

static void shm_note_write(u16 port, u32 val, int width)
{
	u32 routing = shm_routing_off & SHM_CTL_ROUTING;
	u16 off = shm_byte_off(port);

	if (width != 32) {
		shm_word_write(routing, off, (u16)val);
	} else if (routing >> 16 != B43_SHM_SHARED) {
		b43_trace_shm("OBJ.WR", routing | off, val, 32);
	} else {
		shm_word_write(routing, off, (u16)val);
		shm_word_write(routing, (u16)(off + 2), (u16)(val >> 16));
	}
	shm_autoinc(SHM_CTL_AUTOINC_W, width);
}

static u32 shm_note_read(u16 port, int width)
{
	u32 routing = shm_routing_off & SHM_CTL_ROUTING;
	u16 off = shm_byte_off(port);
	u32 v;

	if (width != 32) {
		v = shm_word_read(routing, off);
	} else if (routing >> 16 != B43_SHM_SHARED) {
		v = b43_trace_read_shm(routing, off, 32);
		b43_trace_shm("OBJ.RD", routing | off, v, 32);
	} else {
		v = shm_word_read(routing, off);
		v |= (u32)shm_word_read(routing, (u16)(off + 2)) << 16;
	}
	shm_autoinc(SHM_CTL_AUTOINC_R, width);
	return v;
}

/*
 * clk_ctl_st of the 802.11 core (BCMN_CLKCTLST), which
 * bcmn_core_set_clockmode() and bcmn_core_pll_ctl() of bcma-next/core.c
 * write and then poll until the state follows the request: up to 1500 and
 * 10000 rounds, the whole trace if the read returns zero.
 *
 * The low half is the driver's requests and reads back as written. The high
 * half is the state, and the behaviour is the agcombo's bus capture's
 * (#119-#121, #19216-#19217): ALP and HT are always available, the backplane
 * runs on HT while FORCEHT is high and on ALP otherwise, and the three
 * external resources are up (EXTRESST = 7) even without a request, because
 * the PMU holds them. The requests' initial value is the capture's first read
 * when there is one (0x0040, HQCLKREQ, on the agcombo), zero otherwise.
 */
static u32 clkctlst_req;
static bool clkctlst_init;

static u32 clkctlst_read(void)
{
	u32 v;

	if (!clkctlst_init) {
		clkctlst_init = true;
		if (b43_trace_has("REG.RD", BCMN_CLKCTLST))
			clkctlst_req = b43_trace_read_raw(BCMN_CLKCTLST, 32) &
				       0xffff;
	}
	v = clkctlst_req | BCMN_CLKCTLST_HAVEALP | BCMN_CLKCTLST_HAVEHT |
	    BCMN_CLKCTLST_EXTRESST;
	v |= (clkctlst_req & BCMN_CLKCTLST_FORCEHT) ? BCMN_CLKCTLST_BP_ON_HT
						    : BCMN_CLKCTLST_BP_ON_ALP;
	return v;
}

/*
 * ChipCommon and the PCIe core, either generation, for bcma's own init. They are registers
 * with no side effect the suite needs, so a write is kept and a read returns
 * the capture's value when the oracle has one, the last write or the reset
 * value the harness seeded otherwise. The trace uses the bus classes of
 * reverse-tools/mmio2ops.py.
 */
#define OTHER_REGS	(0x1000 / 4)

static u32 cc_regs[OTHER_REGS], pcie_regs[OTHER_REGS];

void b43_test_cc_seed(u16 off, u32 val)
{
	cc_regs[(off & 0xfff) / 4] = val;
}

static u32 *other_core_regs(struct bcmn_device *core, const char **cls)
{
	switch (core->id.id) {
	case BCMN_CORE_CHIPCOMMON:
		*cls = "CC";
		return cc_regs;
	case BCMN_CORE_PCIE:
	case BCMN_CORE_PCIE2:
		*cls = "PCIE";
		return pcie_regs;
	default:
		return NULL;
	}
}

static bool other_core_write(struct bcmn_device *core, u16 off, u32 val,
			     int width)
{
	const char *base;
	u32 *regs = other_core_regs(core, &base);
	char cls[16];

	if (!regs)
		return false;
	regs[(off & 0xfff) / 4] = val;
	snprintf(cls, sizeof(cls), "%s.WR", base);
	b43_trace_raw(cls, off, val, width);
	return true;
}

static bool other_core_read(struct bcmn_device *core, u16 off, int width,
			    u32 *val)
{
	const char *base;
	u32 *regs = other_core_regs(core, &base);
	char cls[16];

	if (!regs)
		return false;
	if (core->id.id == BCMN_CORE_CHIPCOMMON && off >= BCMN_CC_SPROM) {
		u16 word = (off - BCMN_CC_SPROM) / 2;

		*val = b43_trace_read("SROM.RD", word, 16);
		b43_trace_srom(word, *val);
		return true;
	}
	snprintf(cls, sizeof(cls), "%s.RD", base);
	if (b43_trace_has(cls, off))
		regs[(off & 0xfff) / 4] = b43_trace_read(cls, off, width);
	*val = regs[(off & 0xfff) / 4];
	b43_trace_raw(cls, off, *val, width);
	return true;
}

/* With the virtual chip, all but clk_ctl_st (bcma wrapper) are the chip's. */
static bool chip_owns(u16 off)
{
	return b43_chip_active() && off != BCMN_CLKCTLST;
}

static void note_write(struct bcmn_device *core, u16 off, u32 val, int width)
{
	if (other_core_write(core, off, val, width))
		return;
	if (chip_owns(off))
		b43_chip_write(off, val, width);
	switch (off) {
	case B43_MMIO_PHY_CONTROL:
		phy_addr = (u16)val;
		return;			/* half an op: the line goes out with the data */
	case B43_MMIO_PHY_DATA:
		b43_trace_op("PHY.WR", phy_addr, val, 0, -1);
		return;
	case B43_MMIO_RADIO24_CONTROL:
		radio_addr = (u16)val;
		return;
	case B43_MMIO_RADIO24_DATA:
		b43_trace_op("RAD.WR", radio_addr, val, 0, -1);
		return;
	case B43_MMIO_RADIO_CONTROL:
		radio16_addr = (u16)val;
		break;
	case B43_MMIO_SHM_CONTROL:
		shm_routing_off = val;
		return;
	case B43_MMIO_SHM_DATA:
	case B43_MMIO_SHM_DATA_UNALIGNED:
		shm_note_write(off, val, width);
		return;
	case B43_MMIO_MACCTL:
		macctl = val;
		b43_trace_macctl(val);
		return;
	case B43_MMIO_MACCMD:
		b43_trace_op("MAC.MCMD", 0, val, 0, -1);
		return;
	case B43_MMIO_GEN_IRQ_REASON:
		irq_pending &= ~val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case B43_MMIO_DMA0_REASON:
		dma0_pending &= ~val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case B43_MMIO_DMA64_BASE0 + B43_DMA64_RXCTL:
		rxctl = val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case B43_MMIO_DMA64_BASE0 + B43_DMA64_RXRINGLO:
		rx_ringlo = val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case B43_MMIO_XMTFIFOCMD:
		txfifo_cmd = val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case B43_MMIO_XMTFIFODEF:
	case B43_MMIO_XMTFIFODEF1:
		*txfifo_reg(off) = val;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	case BCMN_CLKCTLST:
		clkctlst_init = true;
		clkctlst_req = val & 0xffff;
		b43_trace_raw("REG.WR", off, val, width);
		return;
	default:
		b43_trace_raw("REG.WR", off, val, width);
		return;
	}
}

/*
 * The PHY version, which b43 reads in `b43_phy_versioning`, from the capture
 * when it has it (one taken at the bus with the flush reads), and decodes into
 * analog_type, phy_type and phy_rev according to phy_common.h's fields.
 * EVERYTHING else depends on phy_type: the choice of the vtable, and the
 * firmware selection, which for corerev 42 takes `ucode42` only `if
 * (phy->type == B43_PHYTYPE_AC)`. Without this value phy_type is zero, b43
 * finds no ucode and the probe returns -EOPNOTSUPP before emitting a single
 * op.
 *
 * It is not an invented constant: the d6220's `wl1_revinfo.txt` gives
 * phytype 0xb and phyrev 0x1, AC rev1, and radiorev 0x42069. Another board
 * gives it with b43_test_chip_identity(), from its profile.
 */
static u16 id_phy_ver = 0x0b01, id_radio = 0x2069, id_radio_rev = 0x4;
static unsigned int id_core_rev = 42;

void b43_test_chip_identity(unsigned int core_rev, u16 phy_ver, u16 radio,
			    u16 radio_rev)
{
	id_core_rev = core_rev;
	id_phy_ver = phy_ver;
	id_radio = radio;
	id_radio_rev = radio_rev;
}

/*
 * The radio's identity at RADIO24_CONTROL 0-2, as b43_radio_versioning()
 * reads it: for corerev 40 on the revision at 0 and the id at 1, below it
 * the revision at 0 and the id's two bytes at 1 and 2. Below corerev 40 the
 * three are radio registers too, so only their first read is the identity.
 */
static bool radio_identity(u16 addr, u16 *v)
{
	static bool taken[3];

	if (id_core_rev >= 40) {
		if (addr > 1)
			return false;
		*v = addr ? id_radio : id_radio_rev;
		return true;
	}
	if (addr > 2 || taken[addr])
		return false;
	taken[addr] = true;
	*v = addr == 0 ? id_radio_rev : addr == 1 ? id_radio & 0xff
						   : id_radio >> 8;
	return true;
}

/* A read served by the virtual chip, traced with note_read()'s classes. */
static u32 chip_note_read(u16 off, int width)
{
	u32 routing = shm_routing_off & SHM_CTL_ROUTING;
	u32 v = b43_chip_read(off, width);
	u16 shm_off;

	switch (off) {
	case B43_MMIO_PHY_DATA:
		b43_trace_op("PHY.RD", phy_addr, v, 0, -1);
		return v;
	case B43_MMIO_RADIO24_DATA:
		b43_trace_op("RAD.RD", radio_addr, v, 0, -1);
		return v;
	case B43_MMIO_SHM_DATA:
	case B43_MMIO_SHM_DATA_UNALIGNED:
		shm_off = shm_byte_off(off);
		if (width != 32) {
			b43_trace_shm("OBJ.RD", routing | shm_off, v, 16);
		} else if (routing >> 16 != B43_SHM_SHARED) {
			b43_trace_shm("OBJ.RD", routing | shm_off, v, 32);
		} else {
			b43_trace_shm("OBJ.RD", routing | shm_off, v & 0xffff, 16);
			b43_trace_shm("OBJ.RD", routing | (u16)(shm_off + 2),
				      v >> 16, 16);
		}
		shm_autoinc(SHM_CTL_AUTOINC_R, width);
		return v;
	default:
		b43_trace_raw("REG.RD", off, v, width);
		return v;
	}
}

/*
 * With B43_VCHIP_PHY=oracle the virtual chip answers the MAC side only: the
 * PHY's and the radio's reads, whose values the chip's register files do not
 * have, come from the capture, and the chip still sees every write.
 */
static bool chip_answers(u16 off)
{
	static int phy_oracle = -1;

	if (!chip_owns(off))
		return false;
	if (phy_oracle < 0) {
		const char *e = b43_test_env("B43_VCHIP_PHY");

		phy_oracle = e && !strcmp(e, "oracle");
	}
	return !phy_oracle || (off != B43_MMIO_PHY_DATA &&
			       off != B43_MMIO_RADIO24_DATA &&
			       off != B43_MMIO_PHY_VER);
}

static u32 note_read(struct bcmn_device *core, u16 off, int width)
{
	u32 v, vendor_lo;
	u16 v16;

	if (other_core_read(core, off, width, &v))
		return v;
	if (chip_answers(off))
		return chip_note_read(off, width);
	switch (off) {
	case B43_MMIO_PHY_VER:
		v = b43_trace_has("REG.RD", B43_MMIO_PHY_VER) ?
		    b43_trace_read("REG.RD", B43_MMIO_PHY_VER, width) :
		    id_phy_ver;
		break;
		/*
		 * The radio's identity, which b43 reads in `b43_radio_versioning`
		 * for corerev 40/42: it writes 0 in RADIO24_CONTROL and the next
		 * read gives radio_rev, writes 1 and gets radio_id. Then it requires
		 * radio_manuf 0x17F -- which for that branch b43 sets itself -- and
		 * for the AC a radio_id 0x2069, or `unsupported`.
		 *
		 * The values come from the d6220's `wl1_revinfo.txt`: `radiorev
		 * 0x42069`, id 0x2069 and revision 4 -- the 2069 rev4, the same as
		 * reverse-tools/extract_chan_tuning_2069rev4.py.
		 */
	case B43_MMIO_RADIO24_DATA:
		if (radio_identity(radio_addr, &v16))
			return v16;
		v = b43_trace_read("RAD.RD", radio_addr, width);
		b43_trace_op("RAD.RD", radio_addr, v, 0, -1);
		return v;
	case B43_MMIO_PHY_DATA:
		v = b43_trace_read("PHY.RD", phy_addr, width);
		b43_trace_op("PHY.RD", phy_addr, v, 0, -1);
		return v;
	case B43_MMIO_SHM_DATA:
	case B43_MMIO_SHM_DATA_UNALIGNED:
		return shm_note_read(off, width);
	case B43_MMIO_MACCTL:
		v = macctl;
		break;
	/*
	 * An address register reads back what was written to it. The read
	 * after every address write -- b43_flush_writes() on these hosts, and
	 * the stock driver on the AC cores -- is the port's own address, not a
	 * value to take from the capture: served from the oracle's queue, it
	 * is the stock driver's address at that point in its sequence, and
	 * wrong as soon as the two sequences part.
	 */
	case B43_MMIO_PHY_CONTROL:
		v = phy_addr;
		break;
	case B43_MMIO_RADIO24_CONTROL:
		v = radio_addr;
		break;
	case B43_MMIO_RADIO_CONTROL:
		v = radio16_addr;
		break;
	case B43_MMIO_SHM_CONTROL:
		v = shm_routing_off;
		break;
	case BCMN_CLKCTLST:
		v = clkctlst_read();
		break;
		/*
		 * The microcode's answer. b43_upload_microcode() starts the PSM and
		 * then waits for B43_IRQ_MAC_SUSPENDED here, twenty rounds and then
		 * "Microcode not responding"; b43_mac_suspend() waits for the same
		 * bit.
		 *
		 * The oracle cannot serve it. The wl-diag captures do not have the
		 * raw MMIO reads; those taken at the bus have them, but they are the
		 * answers to another sequence of events, and served in a queue they
		 * would give b43 wl's interrupt reasons out of their context. The
		 * microcode produces the value, and the microcode here is the shim --
		 * kernel_shim.c's blob is a valid header with no code in it, so the
		 * PSM would never start. Answering is the model, not an invention:
		 * without it every phase with the MAC suspended is unreachable.
		 *
		 * It is not a latch like MACCONTROL: b43 writes here to ACK the
		 * interrupts, and a latch would return the ACK instead of the state.
		 * MAC_SUSPENDED follows the ENABLED bit of the MACCONTROL latch, like
		 * the microcode: high while the MAC is stopped, low while it runs, so
		 * the reasons the timeline brings reach b43 as the vendor read them,
		 * without that bit on top.
		 */
	case B43_MMIO_GEN_IRQ_REASON:
		v = irq_pending;
		if (!(macctl & B43_MACCTL_ENABLED))
			v |= B43_IRQ_MAC_SUSPENDED;
		break;
	case B43_MMIO_DMA64_BASE0 + B43_DMA64_RXSTATUS:
			/*
			 * The receive ring's state: the current slot is the
			 * vendor's, from the oracle, but the state field follows the
			 * control register b43 wrote, or the ring reset, which the
			 * vendor never does, would not finish. The vendor gives the
			 * slot as an address in its ring: where the capture has the
			 * ring's address, a slot falling inside it is mapped onto
			 * b43's.
			 */
		v = b43_trace_read_raw(off, width);
		if (!(rxctl & B43_DMA64_RXENABLE))
			v &= ~B43_DMA64_RXSTAT;
		if (b43_trace_vendor_rx_ringlo(&vendor_lo) &&
		    ((v - vendor_lo) & B43_DMA64_RXSTATDPTR) <
		    B43_RXRING_SLOTS * sizeof(struct b43_dmadesc64))
			v = (v & ~B43_DMA64_RXSTATDPTR) |
			    ((rx_ringlo + (v - vendor_lo)) &
			     B43_DMA64_RXSTATDPTR);
		break;
	case B43_MMIO_DMA0_REASON:
		v = dma0_pending;
		break;
	case B43_MMIO_XMTFIFOCMD:
		v = txfifo_cmd & ~0x8000;
		break;
	case B43_MMIO_XMTFIFODEF:
	case B43_MMIO_XMTFIFODEF1:
		v = *txfifo_reg(off);
		break;
	case B43_MMIO_DMA1_REASON:
	case B43_MMIO_DMA2_REASON:
	case B43_MMIO_DMA3_REASON:
	case B43_MMIO_DMA4_REASON:
	case B43_MMIO_DMA5_REASON:
		v = 0;
		break;
	default:
		v = b43_trace_read_raw(off, width);
		break;
	}
		/*
		 * A MAC register, not a port: on the bus it is a read of its own,
		 * and a capture taken there carries it as REG.RD.
		 */
	b43_trace_raw("REG.RD", off, v, width);
	return v;
}

static u8 stub_read8(struct bcmn_device *core, u16 off)
{
	return (u8)note_read(core, off, 8);
}

static u16 stub_read16(struct bcmn_device *core, u16 off)
{
	return (u16)note_read(core, off, 16);
}

static u32 stub_read32(struct bcmn_device *core, u16 off)
{
	return note_read(core, off, 32);
}

static void stub_write8(struct bcmn_device *core, u16 off, u8 val)
{
	note_write(core, off, val, 8);
}

static void stub_write16(struct bcmn_device *core, u16 off, u16 val)
{
	note_write(core, off, val, 16);
}

static void stub_write32(struct bcmn_device *core, u16 off, u32 val)
{
	note_write(core, off, val, 32);
}

/*
 * The blocks: b43 uses them for the tables and the template RAM. The vendor
 * traces them as a header (TBL.WR, OBJ.BULKW) followed by the single ops,
 * and the same is done here, so the two traces line up.
 */
static void stub_block_read(struct bcmn_device *core, void *buf, size_t count,
			    u16 off, u8 reg_width)
{
	b43_trace_block("OBJ.BULKR", off, count, reg_width);
	b43_trace_fill(buf, count, off, reg_width);
}

static void stub_block_write(struct bcmn_device *core, const void *buf,
			     size_t count, u16 off, u8 reg_width)
{
	b43_trace_block("OBJ.BULKW", off, count, reg_width);
}

/*
 * The core's "agent" space, the wrapper, which is a vtable of its own:
 * `bcmn_aread32` does not go through the read32 above. The core's reset and
 * clocks go through it -- bcmn_core_enable/disable, compiled by the suite as
 * they are in bcma-next/core.c -- and the IOCTL PHY bits b43 touches itself.
 * On the bus it is the 0x1000-0x1fff window, which reverse-tools/mmio2ops.py
 * reports as WRAP.RD/WR with the offset in the wrapper: here it is traced
 * with the same name.
 *
 * IOCTL and RESET_CTL are control registers and read back as written: a
 * latch. RESET_ST is the state of a reset transaction in progress, and
 * outside a transaction it is zero, which is what bcmn_core_disable() waits
 * for.
 *
 * The oracle serves IOST when the capture is taken at the bus (0x100c on the
 * agcombo: DMA64, FASTCLKA, DUALB). The wl-diag captures do not have the
 * agent space, and then the D6220's constant holds, where wl1 is the 5 GHz
 * core: from that answer b43_wireless_core_attach() derives have_2ghz_phy
 * and have_5ghz_phy. The values are b43.h's, not invented here.
 */
#define B43_BCMA_IOST_2G_PHY	0x00000001	/* 2.4G capable phy */
#define B43_BCMA_IOST_5G_PHY	0x00000002	/* 5G capable phy */

static u32 agent_ioctl, agent_resetctl;

static u32 agent_read(u16 off)
{
	switch (off) {
	case BCMN_IOCTL:
		return agent_ioctl;
	case BCMN_RESET_CTL:
		return agent_resetctl;
	case BCMN_IOST:
		if (b43_trace_has("WRAP.RD", off))
			return b43_trace_read("WRAP.RD", off, 32);
		return B43_BCMA_IOST_5G_PHY;
	case BCMN_RESET_ST:
		return 0;
	default:
		return b43_trace_read("WRAP.RD", off, 32);
	}
}

static u32 stub_aread32(struct bcmn_device *core, u16 off)
{
	u32 v = agent_read(off);

	b43_trace_raw("WRAP.RD", off, v, 32);
	return v;
}

static void stub_awrite32(struct bcmn_device *core, u16 off, u32 val)
{
	if (off == BCMN_IOCTL)
		agent_ioctl = val;
	else if (off == BCMN_RESET_CTL)
		agent_resetctl = val;
	b43_trace_raw("WRAP.WR", off, val, 32);
}

const struct bcmn_host_ops b43_test_bcma_ops = {
	.aread32	= stub_aread32,
	.awrite32	= stub_awrite32,
	.read8		= stub_read8,
	.read16		= stub_read16,
	.read32		= stub_read32,
	.write8		= stub_write8,
	.write16	= stub_write16,
	.write32	= stub_write32,
	.block_read	= stub_block_read,
	.block_write	= stub_block_write,
};
