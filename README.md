# b43-next

The `b43` driver for Broadcom's softmac 802.11 cores, out of tree, with its
own copies of the `ssb` and `bcma` buses: one module, `b43-next.ko`, that
loads next to the kernel's `ssb`, `bcma` and `b43` and shares no structure or
symbol with them, the way Broadcom's `wl` carries its own backplane code.

It starts from Linux 6.18's b43, bcma and ssb, and carries the AC-PHY port
for the BCM4352 and BCM4360 (radio 2069) developed in b43-ac-wip: see
[`docs/ac-phy.md`](docs/ac-phy.md).

## Building

```sh
make                       # against the running kernel
make KVER=6.18.0-1         # or another installed one
make KSRC=/path/to/build   # or a tree prepared with modules_prepare
```

The kernel must be 6.18 or later and have PCI, mac80211 and the CORDIC
library; MMC adds the SDIO host, MAC80211_LEDS with LEDS_CLASS the LEDs,
HW_RANDOM the RNG, BMIPS_GENERIC with OF the host of the BCM6362's integrated
WLAN, and BCM47XX_NVRAM, on the Broadcom routers, the board data of boards
without a SROM. `B43_NEXT_DEBUG=y` builds debugfs and the debug
messages; `B43_NEXT_DFS=y`, on a kernel with CFG80211_CERTIFICATION_ONUS,
lets an AP on the AC-PHY run on radar channels (tested on the RT-AC66U B1
only, not a certified detector). What the kernel's Kconfig decided for b43,
ssb and bcma is decided in [`Kbuild`](Kbuild).

Of the 6.18 stable kernels, 6.18.33 to 6.18.38 are not supported: their
typed allocators take a mandatory GFP and lack `kzalloc_flex()`.

## Installing

```sh
make install               # the module, then the modprobe and udev files
dkms add .                 # or through DKMS: dkms.conf builds the module
```

The firmware is b43's, in `/lib/firmware/b43/`, as `b43-fwcutter` writes it.

## Next to the kernel's drivers

The buses are `ssb-next` and `bcma-next`, their PCI bridges
`ssb-next-pci-bridge` and `bcma-next-pci-bridge`; the module's PCI aliases
are the union of the kernel's two bridges' tables, so it is loaded for the
same devices. Which driver gets a device is decided by
`udev/b43-next-bind`, run for every Broadcom PCI device before the kernel's
bus module is loaded for it: if the module's aliases match the device, it
sets the device's `driver_override` to one of the two bridges, unbinding the
kernel's first if it holds the device already (built in, or loaded for
another one).

`modprobe.d/b43-next.conf` blacklists `b43`: b43-next takes every chip b43
takes, and both would register the same SDIO driver. The chips brcmsmac
also drives, the BCM4313 and the BCM4322x, are handed to b43-next too, and
b43-next drives them whether the kernel has brcmsmac or not
(`allhwsupport=1`).

## Layout

```
b43-next/            b43
bcma-next/           the bcma files a PCI or BCM6362 host needs, renamed
ssb-next/            the ssb files a PCI or SDIO host needs, renamed
nvram/               the SPROM from the board's NVRAM, for both buses
include/             the two buses' headers
compat/compat.h      included before every source: kernel version differences
Kbuild, Makefile     the build; dkms.conf, modprobe.d/, udev/ the installation
scripts/             rename-upstream.sh, for upstream sources and patches
test/                the test suites; test/README.md
docs/                technical notes; ac-phy.md for the AC-PHY port
```

Upstream fixes come through `scripts/rename-upstream.sh`, which translates a
patch against `drivers/bcma`, `drivers/ssb`, `drivers/net/wireless/broadcom/b43`
and their headers into this tree's names and paths.

## Elsewhere

- **b43-next-trace-tools**: the capture tools for the stock driver and the
  decoders the tests use.
- **b43-next-traces**: the captures and dumps, by family, chip and board.
- **b43-tools**: the virtual chip the integration suite can run the 802.11
  core on.

## Not yet

- The 802.11 cores built into a SoC (BCM5352/5354, BCM4716/4718,
  BCM5357/5358): their bus is the kernel's, built in. They need a host in the
  two buses that binds to the kernel's core device and leaves ChipCommon,
  PMU and GPIO to the kernel, as OpenWrt's `wl_glue` does for `wl`.
- LCN, which upstream does not build, and LCN40, which upstream does not have.
- The cores of `b43legacy` (revisions 2 and 4), and the ssb PCMCIA host.
- OpenWrt packaging: the binding helper uses `modprobe --resolve-alias`,
  which OpenWrt's userland does not have.

## License

GPL-2.0, as the kernel sources it comes from; see [`LICENSE`](LICENSE).
