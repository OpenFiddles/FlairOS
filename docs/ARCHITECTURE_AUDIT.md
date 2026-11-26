# FlairOS Architecture Audit — 2026-11-15

## Scope and method

This is a source-level audit of the `main` branch at
`d8c65bd9bca5b8de0a1a3df16b5f89b48988d549`. It records what the checked-in
code currently demonstrates and separates that from the target architecture.
It is not a claim that the listed subsystems are production-ready.

## Current boot and platform model

- The supported boot target is i386 BIOS. The Makefile explicitly rejects other
  architectures until they have a boot layer.
- Stage 1 is a 512-byte BIOS boot sector. It reads fixed LBA ranges using CHS
  geometry (18 sectors per track, 36 sectors per cylinder).
- Stage 2 gathers up to 32 E820 entries and passes a version-1 handoff at
  physical address `0x4FF0`; it then enables protected mode and jumps to the
  kernel at `0x10000`.
- The standard image is 2,880 sectors. Kernel, app, Explorer, filesystem
  metadata, and optional MicroPython data occupy fixed locations.
- This is a useful legacy boot path, but it is not yet a general block-device
  boot protocol. Large disks, LBA extensions, and a relocatable loader remain
  future work.

## Memory management and application loading

- The physical frame allocator reads the E820 handoff, but manages only
  `0x00100000..0x10000000` (1 MiB through 256 MiB). Its availability and
  managed bitmaps are fixed at `0x30000` and `0x32000`.
- Paging currently uses 4 MiB identity-mapped supervisor pages, with page-table
  splitting for selected 4 KiB mappings.
- The app address space is built around a fixed `0x40000..0x80000` image/stack
  window. App page frames, page directory, and heap page table are tracked in
  static kernel variables; the app heap is bounded at
  `0x10000000..0x10400000`.
- AIRP v1 has a 36-byte header and a 128 KiB flat-image limit. The loader copies
  into the fixed `TX_APP_BASE` and clears a fixed 256 KiB slot.
- The current design has a meaningful first step toward isolation, but it is
  not a dynamic multi-process address-space manager. There is no general
  executable segment mapper, relocation loader, per-process resource model,
  or independently scheduled process lifecycle yet.

## Storage

- The current filesystem is a fixed-layout ATA format. Its directory begins at
  LBA 398, it exposes 16 file entries, 8 handles, 12-character names, and a
  64 KiB per-file ceiling.
- File contents are buffered in kernel heap allocations but persisted into
  fixed per-file disk regions. This is not a general dynamically allocated
  block filesystem and does not provide journaling or crash-atomic metadata.
- Preserve this format for compatibility; introduce a separately versioned
  extended format rather than changing its interpretation in place.

## Graphics and desktop

- The repository has a pixel framebuffer API and AirGraph software rendering
  primitives, plus adaptive viewport mapping for different display shapes.
- AirGraph exposes backend capability flags and reports whether GPU acceleration
  is available, but the inspected tree does not establish a working GPU driver
  or hardware-accelerated backend. The truthful baseline is software rendering.
- The current desktop is implemented in `kernel/ui/desktop.c`, with rendering
  split into desktop graphics/rendering files. The checked tree does not yet
  establish a complete independent WinDown compositor with general surfaces,
  z-order policy, damage tracking, or multi-window client protocol.
- VGA text output remains present in the kernel console path. It should remain
  available for diagnostics and recovery while the pixel desktop becomes the
  normal user-facing path.

## Drivers and hardware inventory

- Existing driver code covers a subset of legacy PC devices and includes PCI,
  CPU, E820, ACPI, and SMBIOS inventory code.
- A declared interface or inventory record is not equivalent to a functioning
  driver. USB, modern touch controllers, general GPU acceleration, Wi-Fi,
  battery, sensor, and audio support require device-specific implementations
  and tests before they can be advertised as supported.

## Kernel services and application platform

- The current kernel has interrupt/syscall entry, a TSS/user-mode transition,
  paging, a kernel heap, and driver initialization code.
- The inspected source does not establish a preemptive thread scheduler,
  general IPC, capability-based permissions, robust process accounting, or a
  complete application lifecycle service.
- The SDK and userland applications are early platform components, not yet a
  stable, versioned native application ecosystem.

## Build and validation status

- GitLab pipeline `3069395244` for the audited `main` commit failed before
  runner execution with `ci_quota_exceeded`. The listed jobs have no runner
  and no execution duration; this is an infrastructure/quota failure, not
  evidence that the source compiled or failed to compile.
- The GitHub combined-status query for this exact commit returned no status
  records. That is not a passing Actions result.
- Earlier investigation reported unresolved linker symbols and a compiler
  runtime helper in a previous workflow. The current source tree contains
  definitions for `boot_init`, `boot_is_live_session`, `setup_run`,
  `hwinfo_report`, and the desktop graphics entry points. A fresh successful
  link is still required to close the historical linker concern; source
  presence alone does not prove the full link succeeds.

## Engineering invariants

1. Keep AirGraph responsible for rendering and graphics resources.
2. Keep WinDown responsible for windows, surfaces, focus, stacking, input
   routing, and composition policy.
3. Keep kernel memory protection and scheduling separate from user interface
   policy.
4. Keep VGA/serial recovery diagnostics independent of the graphical desktop.
5. Never report GPU acceleration without a detected, initialized accelerated
   backend.
6. Preserve the legacy floppy image and AIRP v1 while new boot, storage, and
   package formats are introduced in parallel.
7. Make each architectural change testable without weakening existing checks.

## Recommended implementation order

1. Restore a reproducible baseline build and capture actual linker diagnostics.
2. Correct the E820/PMM model and add host-testable allocator coverage before
   expanding the managed physical address range.
3. Replace the fixed app window with a bounded, dynamically sized loader using
   page-backed segments and explicit stack allocation; retain the v1 loader.
4. Add a versioned extended disk layout and block-device interface while
   preserving the floppy image layout.
5. Stabilize the framebuffer/display contract, then add explicit software and
   accelerated AirGraph backend discovery.
6. Implement WinDown as a distinct surface compositor before building the
   mobile-style shell on top of it.

## Phase 1 status

The source audit is recorded. Phase 1 is **not complete**: the build must be
executed successfully, historical linker concerns must be resolved with
actual linker output, and regression tests must pass before the foundation is
accepted.
