# Display mode validation

The Bochs/QEMU DISPI driver validates the adapter identity, framebuffer BAR,
PCI memory decoding, advertised video-memory size, requested dimensions,
mode readback, 32-bit pixel depth, virtual dimensions, and framebuffer pitch
before exposing a surface to AirGraph.

The driver bounds dimensions before arithmetic, rejects modes whose backing
video memory is insufficient, and disables the graphics mode if readback or
framebuffer validation fails. PCI command writes preserve the status halfword,
whose bits have write-one-to-clear semantics.

The desktop still selects a profile at build time
(`DISPLAY=landscape|portrait|wide`). This makes mode selection safer across
different framebuffer capacities; it does not yet implement host-window
resize events. A complete runtime resize must reinitialize the framebuffer,
AirGraph surface, viewport, and pointer mapping as one operation.
