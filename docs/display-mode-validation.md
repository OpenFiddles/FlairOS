# Display mode validation

FlairOS currently exposes a Bochs-compatible DISPI framebuffer to AirGraph.
The driver supports both PCI Bochs/QEMU VGA adapters and the legacy ISA-style
Bochs VGA aperture used by v86's generic VGA device.

For PCI adapters, the driver scans buses and advertised functions, enables PCI
I/O and memory decoding, and obtains the linear framebuffer address from BAR0.
It rejects I/O BARs, unassigned BARs, and 64-bit BAR0 values that this 32-bit
kernel cannot safely map. It never substitutes a guessed address for a bad
PCI BAR.

For ISA-style DISPI adapters, the driver first verifies the DISPI identity
register and then uses the legacy ISA LFB aperture at 0xE0000000. This address
is specific to the ISA-style Bochs VGA interface; it is not used for PCI
adapters or arbitrary physical GPUs.

Both paths validate the advertised video-memory size, requested dimensions,
mode readback, 32-bit pixel depth, virtual dimensions, and framebuffer pitch
before exposing a surface to AirGraph. The driver bounds dimensions before
arithmetic and disables graphics mode if readback or framebuffer validation
fails.

**Hardware scope:** this is not a universal driver for Intel, AMD, or NVIDIA
graphics cards. Physical systems must expose a Bochs-compatible DISPI adapter
(or use a future firmware VBE/GOP handoff or native GPU driver). A generic GPU
without DISPI is reported as unavailable; FlairOS does not guess its
framebuffer address or silently draw into arbitrary physical memory.

The desktop still selects a profile at build time
(`DISPLAY=landscape|portrait|wide`). This makes mode selection safer across
different framebuffer capacities; it does not yet implement host-window resize
events. A complete runtime resize must reinitialize the framebuffer,
AirGraph surface, viewport, and pointer mapping as one operation.
