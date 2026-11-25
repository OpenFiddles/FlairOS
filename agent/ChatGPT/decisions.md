# Engineering Decisions

## 2026-11-21 - Preserve Existing Work During Inspection
- **Decision:** Do not modify, stage, reset, or commit the existing dirty working tree during a state-inspection task.
- **Reasoning:** The repository already contains extensive changes from other sessions/contributors.
- **Consequences:** Findings are recorded separately under agent/ChatGPT/. No implementation or Gemini records were changed.

## 2026-11-21 - Report Android Compatibility Conservatively
- **Decision:** Describe APK/DEX/Binder/ashmem code as early groundwork, not Android app compatibility.
- **Reasoning:** APK extraction supports stored ZIP entries only; DEX execution supports a tiny opcode subset; no complete Android framework/runtime is present.
- **Consequences:** Future reports must distinguish parser/ABI prototypes from executable Android application support.

## 2026-11-21 - Tie Test Claims to Executed Commands
- **Decision:** Report 15 host-test executables and 2 Python checks as passed, but report aggregate test and I/O host-test build failures separately.
- **Reasoning:** make test failed before host tests due to missing xorriso; io_host_test failed to link; make all was a no-op due to existing artifacts.
- **Consequences:** Do not claim a clean build, complete test suite, or QEMU acceptance pass.

## FAT32 integration decision — 2026-11-21

- FAT32 is the selected persistent filesystem direction.
- Current integration is deliberately read-only: detect an MBR FAT32 primary partition (0x0B/0x0C) or a FAT32 superfloppy, then expose root entries beneath /storage.
- Do not write or format the disk until FAT allocation, mirrored FAT updates, directory-entry updates, and crash/error handling are implemented and tested.
- FAT32 imported nodes are marked read-only so VFS RAM edits cannot be mistaken for persistent disk writes.
- VFS node and FD tables grow geometrically from their existing initial sizes. Growth remains bounded by available heap memory rather than arbitrary fixed slot counts.
- Preserve the linker reservation at 0x30000; never relax its assertion just to make the link pass.


## Kernel BSS recovery — 2026-11-21
- **Decision:** Move the optional 32x32 custom cursor pixel buffer from static BSS to lazy heap allocation.
- **Reasoning:** The 4 KiB static buffer contributed to exceeding the fixed 0x30000 reserved-memory boundary. The built-in cursor does not need the buffer.
- **Consequences:** The kernel now links with `__bss_end=0x2F654`; the linker assertion and reserved PMM/Explorer layout remain unchanged. Cursor custom-BMP loading may fail cleanly if heap allocation fails.
- **Storage test separation:** The existing 30 MiB QEMU smoke disk is a custom FSDY volume, not FAT32. FAT32 write support must use a separate standards-compliant disposable FAT32 image.


## FAT32 writable operations — 2026-11-21
- **Decision:** Keep sector writes behind an explicit `fat32_set_writer` callback. The VFS installs the ATA writer only outside LiveCD mode; the ATA driver's low-level LiveCD write denial remains the final guard.
- **Decision:** Initially support root-directory 8.3 files only. Do not create RAM-only directories under `/storage` and misrepresent them as persistent.
- **Decision:** Replace file contents by allocating and writing a new cluster chain, publishing the new directory metadata, then freeing the previous chain.
- **Risk:** FAT32 has no journal here. A device error during mirrored FAT updates or directory metadata writes can leave orphaned clusters or inconsistent FAT copies. Do not treat this as production-safe until power-loss/error-injection and QEMU persistence tests pass.

## 2026-11-21 — Pre-PMM BSS budget and ATA device selection
- **Decision:** Keep the linker reservation at `0x30000`; bound the static PCI inventory at 80 entries rather than weakening the assertion or moving pre-PMM hardware discovery onto a heap that is not initialized yet.
- **Reasoning:** The forced rebuild showed `__bss_end=0x30654`; the inventory is statically allocated and `hwinfo_report()` runs before `pmm_init()`/`kmalloc` are available. An 80-device cap recovers enough static memory while retaining practical PCI coverage.
- **Consequence:** Fresh kernel link reports `__bss_end=0x2FD54`, with `0x2AC` bytes of headroom. Revisit if further static growth approaches the boundary.
- **Decision:** Select the requested ATA device before polling its channel STATUS register.
- **Reasoning:** Discovery probes all IDE positions and can leave an absent drive selected; polling before selecting the primary master incorrectly rejects valid disks.
- **Consequence:** ATA primary-master sector reads work in QEMU; FAT32 MBR mount, file write, reboot, and read-back passed.

 
## Architecture migration backlog — deferred 2026-11-21
- **Decision:** Do not begin the x86-64 upgrade or Rust migration during the current UI task.
- **Reasoning:** The user explicitly asked to put both migrations aside for later. Keep the current i386 BIOS target stable while improving Explorer, Settings, and the Material 3 desktop.
- **Consequences:** Continue building and testing the current freestanding i386 toolchain. Revisit either migration only when the user explicitly reopens it.

## 2026-11-22 — Desktop/mobile shell split and app discovery

- **Decision:** Keep `desktop.c` as the desktop session controller and place the mobile home screen/session loop in a separate `mobile.c`.
- **Decision:** Select the shell at compile time with `FLAIROS_SHELL=desktop|mobile`; the mobile profile requires the phone device profile and portrait display.
- **Decision:** Use the VFS `/apps` directory as the shared installed-APK source of truth for both shells rather than maintaining two divergent in-memory lists. Desktop Start Menu and mobile app drawer independently enumerate that directory.
- **Decision:** Keep APK installation in RAM-backed `/apps` until a durable package store and rollback/recovery semantics exist. Do not imply installs survive reboot.
- **Decision:** Treat APK/DEX support as experimental compatibility groundwork. Stored-ZIP-only extraction and a limited DEX interpreter do not constitute general Android app compatibility.
- **Decision:** Lazily initialize WinDown through `desktop_windown_get()` so AXI application windows can be rendered by either shell.
- **Decision:** Fix host-test aggregation with a CMake global property so the `host-tests` target depends on every registered host executable, not only the final function invocation.


## 2026-11-22 — Keep mobile shell integration on main
- **Decision:** The mobile shell work remains directly in the `main` working tree; do not create or use a feature branch for this work.
- **Evidence:** `main` and `feature/mobile-shell-app-management` both pointed to `7d3e7ed`; the feature branch contained no unique commits. The redundant local branch was safely deleted after confirming `main` contained its tip.
- **Integration approach:** Preserve the existing dirty changes from other contributors and keep the mobile shell layered onto the shared CMake, Makefile, kernel, AXI, VFS, and driver changes. Do not manufacture a merge commit or stage unrelated files.
- **Validation status:** The previously recorded mobile and desktop builds, 24/24 CTest run, QEMU boot markers, and diff check were run against this same working-tree integration. A later attempt to use `build/` as a CMake build directory was invalid because it has no `CMakeCache.txt`; no source/build files were changed by that failed command. Fresh verification was not rerun because the mobile build directories had been removed to conserve phone storage.
## 2026-11-23 — Time-delta spring motion and startup launcher

- **Decision:** Drive desktop shell animation from the monotonic timer passed by `kmain64`, with a bounded elapsed-time spring step, rather than advancing a fixed number of pixels per render loop.
- **Decision:** Apply the spring motion to window entrance, Control Center, Spotlight, and pointer-distance Dock magnification. Keep the Dock magnification neighbor-aware and center-anchored.
- **Decision:** Make Dock icon frame animation finite while hovered. A hover effect must not keep `shell_tick()` permanently dirty after the one-shot sequence completes.
- **Decision:** Keep `shell_tick()` as a host-test compatibility wrapper and add `shell_tick_at()` adapters for mobile/TV, avoiding a broad shell API break.
- **Decision:** Add a root `bashsrc` that is sourceable from `~/.bashrc`; default autostart can be disabled with `FLAIROS_AUTOSTART=0`. It launches the fullscreen Termux:X11/XFCE/QEMU path without killing unrelated QEMU/VNC guests.
- **Decision:** FlowDrip remains deferred. No transport, discovery, pairing, share sheet, or transfer implementation was started in this milestone.


## 2026-11-23 — AArch64 Desktop is the root bashsrc default

- **Decision:** Root `bashsrc` must launch the AArch64 Desktop guest, not the legacy i386 QEMU guest.
- **Implementation:** Use a dedicated `tools/run-x11-aarch64.sh` launcher and the existing `desktop-aarch64` preset artifact. Keep the i386 launcher available separately; do not silently repurpose it.
- **Decision:** Do not request a Termux wake lock during startup. User reported repeated wake-lock output flooding the screen; startup should remain quiet and avoid that command entirely.
- **Validation boundary:** Syntax/source tests and artifact existence are confirmed; live Termux:X11 boot of this new path remains unverified.
