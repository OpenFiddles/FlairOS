# Display mode validation and resizing

The Bochs/QEMU DISPI driver now queries the adapter's advertised maximum
resolution instead of relying only on a fixed 1024x768 ceiling. Requests are
clamped to the adapter's actual limits, and the driver verifies the selected
X/Y resolution, 32-bit pixel depth, virtual dimensions, and framebuffer pitch
before exposing the framebuffer to AirGraph.

The driver also checks that the PCI framebuffer BAR is a nonzero memory BAR
and enables PCI memory decoding before use. Unsupported adapters and invalid
mode readbacks fail cleanly so the desktop can retain its VGA recovery path.

The current desktop still chooses a profile at build time
(`DISPLAY=landscape|portrait|wide`). This improves safe mode selection and
supports adapters with different maximum sizes; it does not yet detect a host
window resize event. A later display hotplug/resize path must reinitialize the
framebuffer, AirGraph surface, viewport, and pointer mapping together.
