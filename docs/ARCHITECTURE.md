# Architecture overview

This page is a navigation map, not a claim that every planned subsystem is
already implemented.

## Main boundaries

- **Boot and handoff:** the complete BIOS boot path is i386. The boot handoff
  supplies memory information to the kernel.
- **Kernel and hardware:** architecture-specific operations are separated from
  architecture-neutral subsystem logic where the current implementation allows.
- **Memory:** PMM owns physical frame inventory; VMM owns virtual mappings and
  address-space mechanics. See [Memory Management](MEMORY_MANAGEMENT.md).
- **Graphics:** AirGraph provides a graphics API and CPU rendering path.
  The current Bochs/QEMU framebuffer is a display target, not an accelerated GPU.
  See [AirGraph](AirGraph.md).
- **Desktop:** WinDown is the window compositor. Keep it distinct from AirGraph,
  which supplies graphics and rendering services.
- **Applications:** AIRP is the executable application format; the C SDK exposes
  the current application-facing calls. See [SDK](SDK.md).
- **Wireless:** a Bluetooth HCI driver, a 802.11/WPA2 WiFi driver core and their
  host tests exist, with no board or chip driver bound yet. See
  [Wireless](architecture/WIRELESS.md).
- **Resources:** Pancake uses the data-only PCK bundle format. PCK files are not
  executable applications. See [Pancake / PCK](PANCAKE.md).

## Current support notes

The i386 BIOS path is the complete boot target described by the project.
AArch64 work is a bring-up scaffold, not a second complete supported boot path.
MicroPython is experimental and is built as a separate image.

For implementation details, use the subsystem pages and source-linked API
reference rather than assuming future architecture diagrams are implemented.
