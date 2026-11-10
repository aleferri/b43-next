# Driver status

How the port is put together and how the sources map onto the patch series.
What is open is in [`retrace-todo.md`](retrace-todo.md); the scores are in the
top-level README.

## What the driver accepts

- `op_init()` accepts only the BCMA bus and chips `0x4352`/`0x4360`
  (`-EOPNOTSUPP` otherwise).
- `op_switch_channel()` returns `-ESRCH` when the radio is not 2069 rev 4 or
  the channel table has no row for the frequency.
- The 2069 channel table has 64 rows, 2412–2484 MHz and 5170–5825 MHz, keyed
  on the frequency the synthesiser is tuned to: the primary channel at 20 MHz,
  `center_freq1` at 40 and 80. Nothing filters by configuration.

## Entry points

| entry | caller in b43 | what it does |
|---|---|---|
| `op_init` | `b43_phy_init()` | PLL check, core probe, frontend pre-init, PMU regctl, GPIO, `mode_init`, table load, `init_regs`, host flags |
| `software_rfkill` | `b43_phy_init()`, `b43_phy_exit()` | unblocked: radio init, power-on, rccal, AFE-LPF stage. Blocked: `b43_phy_ac_down()` |
| `switch_analog` | four sites in `main.c` plus `b43_phy_init()` | the AFE arm unit; the cold preamble once, on the first entry with a channel (`b43_phy_ac_cold_preamble_due()`) |
| `switch_channel` | `b43_phy_init()`, `b43_op_config()` | channel setup; a no-op when channel and width are already programmed |
| `channel_calibrate` | `b43_op_config()`, in place of its final `mac_enable` | re-enables the MAC between the RX gain-control sweep and the post-switch calibrations |
| `recalc_txpower` / `adjust_txpower` | `b43_phy_txpower_check()` | per-rate table from the SROM (`b43-next/ppr_ac.c`), target `0x0646` per core |
| `pwork_1sec` | the 1 s periodic work in `b43-next/main.c` | one watchdog turn while the PHY is in its run state; the bss-up once the availability check is over |
| `pwork_60sec` | b43 periodic work | CRS minimum-power threshold, written only when it changes |
| `radar_poll` | `radar_work`, every 150 ms while `conf->radar_enabled` | the detector read (`PHY 0x0251`/`0x0252`) |
| `noise_sample_done` | `handle_irq_noise()` on `B43_IRQ_NOISESAMPLE_OK` | the background noise sample |

The availability check is `dev->cac_pending`, recomputed by `b43_op_config()`
and cleared by `BSS_CHANGED_BEACON_ENABLED`; the PHY keeps its own copy.
While it is pending `b43_ac_cac_match_gate()` keeps the MAC muted: TX FIFOs
suspended, station row of the address match table without flags,
`B43_MACCTL_AP` off.

## Warnings and scaffolding

Where the driver writes something it cannot derive, it logs one `b43warn` per
site with `b43_phy_ac_todo()` (`grep -n 'b43_phy_ac_todo(dev' b43-next/*.c`).

Some values are transcribed from captures rather than derived. On an RF chain
other than the one they were read from they can overdrive the PA, so they are
scaffolding: the FEM control table exists for `femctrl=6` with `femctrl_sub 0`,
the D6220, the DSL-3580L, the tg789vac and the agcombo, for `femctrl=2` with `femctrl_sub 1`, the
MacBookAir6,1, for `femctrl=1` with `femctrl_sub 0`, the archer-t5e, and for
`femctrl=3` with `femctrl_sub 3` (2.4 GHz) and `0` (5 GHz), the Northstar
boards (R7000, EA6900, RT-AC66U B1);
`b43_phy_ac_set_regtbl_on_femctrl()` stops with a warning on any other.

- A scaffold is removed by deriving the value, or by proving with captures that
  it is invariant over the whole domain — never because the gate passes.
- A derived value must not be replaced by the constant it reduces to on the
  reference boards: the trace cannot see that regression, so check
  refactors with a source diff too. The converse needs the same proof:
  `0x?724` was taken from `tssifloor5g`, `0x03ff` on every board, until a
  board with the variable removed (the R7000, issue #16) showed a constant.

## Reading a divergence: phase, version, chip, board

The stock driver has two paths where b43 has one, the first attach and a
bring-up on an interface that was already up, and the same function writes
different constants in the two. Rule out the phase before blaming the chip, and
the driver version before blaming the board.

| comparison | isolates |
|---|---|
| D6220 vs DSL-3580L (both 4352) | driver version (7.14.89 vs 6.30) |
| D6220 vs TG789vac v2 (same `wl`) | chip and ucode |
| agcombo vs TG789vac v2 (both 4360) | driver version within 7.14 |

The port reconstructs 7.14; the DSL-3580L can tell a version fork from a
hardware fact and nothing else. Within 7.14 the agcombo runs `switch_channel`'s
phases in the D6220's order: anchored on the first constant write of each, all
17 anchorable phases appear in order. The differences are in repetition (the
gain-cal writes on table `0x0c` recur about 55 times on 7.14.43, once per step
on the D6220), so an agcombo capture is compared as it is, with
`gates.sh --board agcombo`.

## What may be observed

Allowed: hooking the stock driver's hardware I/O accessors (PHY, radio, MMIO,
shared memory, template RAM, OTP, the TX post: hnddma's, or `wlc_txfifo`
where hnddma has no symbols), trapping its raw MMIO, reading the memory the
hardware reads by DMA (descriptors and the buffers they point at, as
`wl-diag` does at the TX post and `wl-mmio-trap` at a TX index write), and
reading the blob's `.rodata`/`.data`. Not allowed: hooking its internal
logic functions.
When a value is needed, the routes are, in order: an I/O accessor that is or
can be covered; DMA memory at the point the hardware is handed it; an iovar
from outside; a table in `.rodata`. If none is enough, the unknown stays
open.
