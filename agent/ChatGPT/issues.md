# Issues and Blockers

## OPEN - Full test target blocked by missing xorriso
- **Status:** OPEN
- **Evidence:** make test stopped at the LiveCD ISO recipe: xorriso: No such file or directory.
- **Impact:** Aggregate test target did not reach host tests or smoke tests.
- **Next action:** Provide xorriso in Termux or run the full suite in a supported Linux/CI environment.

## OPEN - I/O manager host-test target does not link
- **Status:** OPEN
- **Evidence:** make build/io_host_test links drivers/io_manager.c, whose io_selftest() references serial_str; no host stub is provided.
- **Impact:** I/O manager host test cannot be freshly built using the current rule.
- **Next action:** Add a host-only serial stub or compile out kernel logging in host-test mode.

## OPEN - Some new subsystem tests are not integrated
- **Status:** OPEN
- **Evidence:** APK, ashmem, Binder, and DEX host-test sources are not all wired into the Makefile aggregate test target. The FAT32 host test is integrated.
- **Impact:** Some new subsystem tests are not run by `make test`.
- **Next action:** Add explicit build rules and aggregate target entries for the remaining subsystems.

## OPEN - Android application runtime is incomplete
- **Status:** OPEN
- **Evidence:** APK extraction supports stored ZIP entries only; DEX execution supports a tiny opcode subset; no complete Android framework/runtime exists.
- **Impact:** Ordinary Android APK installation/execution is not demonstrated.
- **Next action:** Treat docs/ANDROID_APK_PLAN.md as a roadmap, not a completed capability statement.

## PARTIALLY RESOLVED - Fresh build complete; broad QEMU acceptance remains failing
- **Status:** PARTIALLY RESOLVED
- **Evidence:** A forced kernel rebuild initially reproduced the BSS assertion; after bounding the pre-PMM PCI inventory, the kernel and boot image rebuilt with `__bss_end=0x2FD54`. A dedicated FAT32 QEMU persistence test passed.
- **Impact:** The fresh kernel build and FAT32 ATA path are validated, but the full smoke/acceptance suite is not green.
- **Next action:** Resolve the separate custom-FS format-write and AXI/AirGraph failures, then rerun full acceptance.

## FAT32/VFS follow-up

- RESOLVED: writable root-directory FAT32 8.3 file operations are integrated; host tests and a two-boot QEMU persistence test pass. FAT32 is not journaled, so production-safety limitations remain below.
- OPEN: /storage import currently covers the FAT32 root directory only; implement nested directory traversal and proper path lookup.
- OPEN: only MBR primary FAT32 partition types 0x0B/0x0C and superfloppy are detected; GPT/extended partitions are not supported.
- RESOLVED: linker BSS now ends at 0x2FD54 after moving the optional cursor pixel buffer to heap and bounding the pre-PMM PCI inventory at 80 entries. The 0x30000 linker assertion is unchanged.
- RESOLVED: a disposable FAT32 image was mounted in QEMU, written through /storage, rebooted, and read back successfully; exact payload independently verified in the disk image.

- RESOLVED: created and used a separate FAT32-valid disposable 70,000-sector image for QEMU persistence testing. The existing broad smoke fixture remains a 30 MiB custom FSDY filesystem.
- RESOLVED: QEMU FAT32-media write/reboot/read-back test passed. OPEN: the separate broad QEMU smoke/acceptance suite is still not passing.

- RESOLVED: QEMU ATA/FAT32 write-and-reboot persistence passed on a disposable image. Error-injection and power-loss recovery remain open.
- OPEN: FAT32 has no journal or atomic transaction layer. Mid-write device failures can leave leaked clusters or inconsistent mirrored FATs; add error-injection and recovery tests before relying on important data.
- OPEN: only root-directory 8.3 file create/write/delete is exposed through VFS. Nested directory traversal/mkdir, LFN, GPT/extended partitions, timestamps/attributes, and FSInfo free-count updates remain unsupported.
- OPEN: QEMU smoke harness booted to developer shell, but `format` reported a disk write failure and the later `run` command panicked with `AXI window presentation requires AirGraph`; smoke suite is not passing.


## ATA/FAT32 integration follow-up

- RESOLVED: ATA sector reads failed because `ata_setup_device()` polled STATUS before selecting the requested drive; the discovery loop could leave an absent IDE position selected. Selection now precedes polling, and QEMU MBR/FAT32 read/write/reboot verification passes.
- OPEN: FAT32 has no journal or atomic transaction. Mid-write device failures can leave leaked clusters or inconsistent mirrored FATs; add error-injection and recovery tests before relying on important data.
- OPEN: only root-directory 8.3 file create/write/delete is exposed through VFS. Nested directory traversal/mkdir, LFN, GPT/extended partitions, timestamps/attributes, and FSInfo free-count updates remain unsupported.
- OPEN: broad QEMU smoke remains failing in its separate custom-FS format-write and AXI/AirGraph paths. Do not conflate that suite with the passing FAT32 persistence test.

 
## DEFERRED - x86-64 and Rust migrations
- **Status:** DEFERRED BY USER
- **Evidence:** User explicitly requested both migrations be put aside while the current UI work continues.
- **Impact:** The supported build remains freestanding i386 BIOS; do not make architecture/toolchain migration changes as part of UI work.
- **Next action:** Revisit only after the user explicitly authorizes either migration.

## OPEN - Explorer BMP rendering API
- **Status:** OPEN
- **Evidence:** Eight original 64x64 24-bit BMP icon assets and a generator now exist under resources/images/icons/, but userland AXI currently has no bitmap/image drawing syscall.
- **Impact:** Desktop shell uses AirGraph vector glyphs; Explorer can classify image files but cannot render the BMP pack directly.
- **Next action:** Design a bounded, validated userland bitmap drawing API before wiring external icons into Explorer. Avoid embedding the full pack into the constrained kernel image.

## OPEN - Battery telemetry
- **Status:** OPEN
- **Evidence:** Current device/ACPI path does not expose a battery status/charge source to the desktop.
- **Impact:** Taskbar shows BAT --; Settings reports telemetry unavailable. No fake percentage is displayed.
- **Next action:** Add ACPI battery support only when a real battery device/AML path is identified and safely validated.

## Explorer file-manager follow-up — 2026-11-22
- RESOLVED: Explorer no longer relies on the legacy flat-file listing for its primary view; it uses the hierarchical VFS syscall interface.
- RESOLVED: Added parent/folder navigation, list/grid/details modes, search, text preview, note creation, and explicit delete confirmation.
- OPEN: Manual QEMU interaction test is still required. The current build and boot checks do not prove every UI interaction behaves correctly.
- OPEN: VFS imports FAT32 root entries but does not populate nested directory contents. FAT32 persistent mkdir and nested path traversal are not implemented; /storage folder creation is therefore refused. Do not imply nested storage browsing works.
- OPEN: VFS has no rename operation. Explorer does not yet support rename, copy/paste, move, or multi-select.
- OPEN: Explorer text preview reads at most 8192 bytes; no image preview or BMP rendering is implemented. A bounded userland image drawing API remains a prerequisite for using the external BMP icon pack.

## OPEN - APK runtime is still experimental

- **Status:** OPEN
- **Evidence:** ZIP extraction is stored-entry-only and DEX execution supports a limited opcode/runtime subset. Host parser/DEX tests pass, but general third-party Android APKs are not proven runnable.
- **Impact:** Chrome and most ordinary Android APKs are not expected to work yet.
- **Next action:** Expand ZIP/DEFLATE support, DEX/ART semantics, Android framework services, Binder/ashmem integration, and acceptance tests. Keep compatibility claims conservative.

## OPEN - Installed APK persistence is not implemented

- **Status:** OPEN
- **Evidence:** Installer copies APKs into RAM-backed VFS `/apps`; the persistent `/storage` path is currently a separate filesystem namespace.
- **Impact:** Installed apps can appear in Start Menu/mobile drawer for the current session but are not guaranteed to survive reboot.
- **Next action:** Design a durable package store, metadata index, atomic install/uninstall, recovery, and upgrade semantics before claiming persistent app management.

## OPEN - Interactive mobile/desktop UI acceptance

- **Status:** OPEN
- **Evidence:** QEMU serial boot reached the mobile shell. QEMU monitor keyboard injection did not trigger the app-drawer or APK-launch serial markers.
- **Impact:** Home screen rendering, APK launch/close, desktop right-click menu actions, mobile long-press details, install, and uninstall have not yet been exercised end-to-end by a human.
- **Next action:** Use an interactive QEMU/VNC session and verify the listed flows. Physical touchscreen input also needs device-specific validation.

## OPEN - Native mobile hardware target

- **Status:** OPEN
- **Evidence:** The mobile shell is a portrait profile on the existing i386 BIOS kernel. The AArch64 boot path remains bring-up scaffolding.
- **Impact:** This is not yet a native ARM64 build for a real Android phone.
- **Next action:** Revisit the ARM64 boot/driver stack as a separate architecture project; do not conflate portrait UI with native mobile hardware support.

## OPEN — Interactive VNC pointer acceptance for 64-bit desktop animations

- **Status:** OPEN
- **Evidence:** VNC screenshot capture succeeds and the x86_64 guest reaches `render=ok 1024x768`, but automated VNC/QMP pointer attempts did not produce visible dock-region changes. Pixel comparison between baseline and pointer-hover screenshots showed no Dock-region difference.
- **Impact:** The spring/Dock/Spotlight code builds and host shell tests pass, but real pointer-driven animation has not been visually verified in the live VNC guest.
- **Next action:** Diagnose the x86_64 guest input path (VMMouse/PS2/QEMU VNC event routing), then repeat pointer hover, Dock sweep, Spotlight, and Control Center visual tests.

## OPEN — AArch64 desktop VNC guest unavailable

- **Status:** OPEN
- **Evidence:** Port 5902 was closed during this task; ports 5900, 5901, 5903, and 5904 accepted VNC connections.
- **Impact:** Five-guest VNC matrix is not currently all online.
- **Next action:** Reassess available phone memory before restarting the AArch64 desktop guest. It was intentionally not restarted during this task.


## OPEN — AArch64 Termux:X11 live startup acceptance

- **Status:** OPEN / awaiting live boot verification.
- **Evidence:** Root `bashsrc` now routes to `tools/run-x11-aarch64.sh`; shell syntax, source-only helper loading, absence of wake-lock/i386 launch references, and AArch64 ELF existence all passed.
- **Impact:** Correct architecture is configured, but successful interactive fullscreen boot has not yet been observed after this change.
- **Next action:** Run or allow the user's next startup to invoke root `bashsrc`, then inspect `flairos-x11-launch.log` and `flairos-aarch64-serial.log` for AArch64 boot markers.
