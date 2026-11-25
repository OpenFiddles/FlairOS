# ChatGPT Activity Log

All timestamps are local device time (America/Chicago, UTC-06:00).

## 2026-11-21 18:45 - Repository State Inspection

- Connected to the authorized Android/Termux device through Remote Desktop Commander.
- Located repository at $HOME/FlairOS.
- Inspected Git status, branch, remotes, commits, tracked/untracked files, architecture/build documentation, Gemini records, source wiring, and existing build artifacts.
- Confirmed main is at 7d3e7ed, equal to origin/main and 8 commits ahead of gitlab/main.
- Confirmed the working tree was already dirty: 31 tracked files modified plus numerous untracked files. No pre-existing implementation files were edited, staged, reverted, or committed.
- Inspected task scheduler, ELF32, VFS, WinDown, APK/ZIP, DEX, ashmem, Binder, and FAT32 source.
- Attempted make test; it stopped before host tests because xorriso is unavailable in Termux.
- Attempted independent host-test target builds. The I/O manager host test failed to link because io_selftest() references undefined serial_str.
- Ran 15 available host-test executables successfully: AirGraph, driver, framebuffer, ATA IDENTIFY, PMM, VM region, desktop renderer, desktop chrome, WinDown, task scheduler, ELF32, VFS, BMP, image, and cursor.
- Ran tests/boot_layout_test.py and tests/airp_apps_test.py build/settings.airp build/notes.airp; both passed.
- Ran make -j4 all; Make reported the existing image target was already up to date. This was not a clean rebuild.
- No QEMU boot or screenshot capture was performed.
- Created this agent/ChatGPT recordkeeping directory. Gemini's records were not changed.

## Findings

- The documented supported boot path remains i386 BIOS. AArch64 is bring-up scaffolding, not a complete boot target.
- Latest commit: 7d3e7ed, "feat: add Phase 1-6 kernel foundations (task scheduler, ELF32, VFS, WinDown events, Python/net ABI)".
- Current uncommitted work adds device profiles, I/O manager self-test, cursor integration, APK/DEX/ashmem/Binder code, Android planning docs, FAT32 source, acceptance tooling, and desktop/userland changes.
- APK parsing walks ZIP local file headers; extraction supports stored (method 0) entries only. No general DEFLATE extraction or complete Android runtime is established.
- DEX validation and bytecode execution support only a small subset, not Dalvik/ART.
- FAT32 source and a host test exist, but FAT32 is not wired into the kernel build or aggregate tests.
- APK, ashmem, Binder, DEX, and FAT32 host-test sources are not integrated into the Makefile test target.
- The I/O host test target has a linker defect caused by serial_str in drivers/io_manager.c.

## 2026-11-21 — FAT32 storage and expandable VFS pass

- Added FAT32 BPB geometry validation, FAT-capacity checks, cluster-range checks, and bounded cluster-chain traversal.
- Added FAT32 directory enumeration for short 8.3 names, skipping deleted, LFN, and volume-label entries.
- Added automatic primary-MBR FAT32 partition discovery (types 0x0B/0x0C), with superfloppy fallback through the ATA primary-master read API.
- Connected detected FAT32 root contents to /storage in the VFS. Imported FAT32 files/directories are explicitly read-only; no disk sectors are written and no formatting occurs.
- Changed VFS node and descriptor tables from fixed-capacity arrays to heap-backed doubling growth. VFS_MAX_NODES and VFS_MAX_FDS now define initial capacities, not hard ceilings.
- Added host coverage that exceeds the old 64-node and 16-descriptor limits.
- Added fat32.o to the kernel link and fat32_host_test to make test.
- Tests passed: FAT32 host test, VFS host test (including 90 directories and 24 simultaneous file descriptors), and individual i386 compilation of fat32.o and vfs.o.
- Full kernel image link remains blocked by the existing linker assertion: __bss_end = 0x30650, exceeding the reserved boundary 0x30000. No linker boundary was weakened.
- Persistent FAT32 writes, directory recursion, long filenames, GPT partition discovery, and QEMU validation remain outstanding.
- No Git commit or push was made.


## 2026-11-21 — Kernel BSS recovery follow-up

- Investigated the linker boundary and found the 4 KiB `custom_pixels` cursor buffer was a major static-BSS allocation.
- Moved the optional custom cursor pixel buffer to lazy heap allocation (`kmalloc` in kernel builds; `malloc` in the cursor host test). The built-in cursor remains independent of this allocation.
- Rebuilt `build/kernel.elf`, `build/kernel.bin`, and `build/textile.img` successfully; the existing `__bss_end <= 0x30000` assertion remains unchanged.
- Verified the current linked ELF reports `__bss_end = 0x0002f654`, leaving 0x9AC bytes before the reserved 0x30000 boundary.
- Rebuilt and passed `build/cursor_host_test`, `build/fat32_host_test`, and `build/vfs_host_test`; `git diff --check` passed.
- The smoke test was started but no completed PASS result was captured. It is not reported as passed.
- Confirmed the existing QEMU smoke fixture creates a 30 MiB custom-format disk (`FSDY` magic), not a FAT32 volume. That fixture cannot validate FAT32 persistence and is below FAT32's minimum cluster-count threshold.
- No FAT32 write API or disk writeback was enabled in this follow-up. FAT32 remains read-only pending a dedicated disposable FAT32 test image and safe write-path tests.
- No commit, push, or disk formatting/write was performed.


## 2026-11-21 — FAT32 writable file operations

- Added a separate FAT32 sector-write callback; mounting remains read-only until a caller explicitly installs a writer.
- Added 8.3-name file creation, file replacement/growth/truncation, deletion, cluster allocation/freeing, directory-slot reuse, directory-chain extension, and updates to every FAT copy.
- Added bounds checks for cluster counts, file cluster requirements, volume LBAs, and directory cluster arguments. File contents are written before the directory entry is published; the previous chain is freed after metadata publication.
- Connected the FAT32 writer to ATA only outside LiveCD sessions. ATA's existing low-level LiveCD write block remains intact.
- Integrated direct-root `/storage` VFS file create/write/truncate/delete with FAT32. Disk-backed VFS writes are staged in a separate memory buffer so a failed write does not mutate the cached file contents. `/storage` directories are deliberately not created in RAM because persistent FAT32 mkdir is not implemented. Imported FAT32 directories remain read-only.
- Replaced the earlier minimal FAT32 test with a 70,000-sector in-memory FAT32 volume using two FAT copies. Tests exercise read, create, 700-byte multi-cluster write, replacement, mirrored FAT consistency, duplicate-name rejection, invalid 8.3-name rejection, deletion, cluster freeing in both FAT copies, and write denial when no writer callback is installed. Added respect for the FAT32 read-only file attribute.
- Added explicit `format=raw` drive declarations to the QEMU smoke harness to avoid QEMU's raw-image probing restrictions. The smoke run still did not pass: `format` reported a write failure and the existing `run` path later panicked with `AXI window presentation requires AirGraph`. The QEMU run is not counted as FAT32 validation.
- PASS: FAT32 host test; VFS host test; i386 FAT32/VFS object builds; kernel image link and `build/textile.img` generation; linker boundary remains valid at `__bss_end=0x2F654`; `git diff --check`.
- NOT RUN: QEMU FAT32 disk write/reboot persistence test. No mkfs.fat utility is installed; the FAT32 test uses an isolated in-memory disk.
- No actual user media was formatted or written. No commit or push.

## 2026-11-21 19:38 — Fresh build recovery and QEMU FAT32 persistence

- Forced a full i386 kernel-object rebuild with `make -B -j2 build/kernel.elf`. It reproduced the linker assertion failure; the current source had grown the pre-PMM BSS start to `0x26000`, and the static hardware inventory pushed `__bss_end` to `0x30654`.
- Preserved the `0x30000` linker reservation. Reduced `HWINFO_MAX_PCI` from 128 to 80 (still a broad device inventory) to bound the pre-PMM static inventory. Fresh link then passed with `__bss_start=0x26000`, `__bss_end=0x2FD54`, leaving `0x2AC` bytes before the reserved boundary.
- Rebuilt `build/kernel.elf`, `build/kernel.bin`, and `build/textile.img` successfully.
- Created a disposable, standards-geometry FAT32 disk at `build/qemu-fat32-test.img`: MBR type 0x0C, partition start LBA 2048, 70,000 sectors, 32 reserved sectors, two 547-sector FATs, 68,874 data clusters, FSInfo and backup boot sector. This image is separate from any user media.
- QEMU first boot mounted the FAT32 partition, enabled the normal-boot ATA writer, and created `/storage/QEMU.TXT` through the VFS test command. A second QEMU boot mounted the same image and `fat32verify-test` reported `fat32: reboot persistence verified`.
- Independently inspected the disk image after the two boots: root entry `QEMU    TXT`, cluster 3, size 37; payload exactly `FlairOS FAT32 reboot persistence test`.
- Diagnosed and fixed an ATA PIO selection-order bug: `ata_setup_device()` previously polled STATUS before selecting the requested drive. ATA discovery had left an absent IDE position selected, causing primary-master sector reads to fail. The driver now selects the target device, delays, then polls STATUS. The QEMU FAT32 mount/write/reboot test passed with this fix.
- Removed temporary `fat32-debug` / `ata-debug` serial instrumentation after diagnosis. Rebuilt the boot image without debug logging; `grep` confirms no debug markers remain.
- Final checks: FAT32 host test PASS; VFS host test PASS; `git diff --check` PASS; `make -j2 build/textile.img` PASS; linked BSS remains `0x2FD54`.
- The existing broad `tests/smoke.py` suite remains unresolved (its separate custom-FS format / AXI-AirGraph failures are still distinct from this FAT32 test). No claim of full QEMU acceptance.
- No user media was formatted or written. No commit, staging, or push was performed.

 
## 2026-11-21 — Material 3 desktop, Explorer, Settings, and BMP icon pack
- Continued on the authorized Termux device in the FlairOS repository; inspected the existing dirty working tree and agent records before editing. Preserved unrelated dirty files and did not stage, commit, or push.
- Reworked the desktop shell with a Material 3-inspired header, workspace status pill, consistent app tiles, floating taskbar, and Start panel. Added explicit Settings launch hit-testing alongside Explorer.
- Updated Explorer with visible item counts, human-readable B/KB/MB sizes, extension-derived file types (TEXT, IMAGE, CONFIG, APP), and clearer action hints. Existing filtering, alphabetical sorting, preview, create, delete, and refresh behaviors remain.
- Improved Settings system status: battery state is explicitly reported as unavailable when firmware does not expose telemetry; storage slots show free/total and distinguish LiveCD, disk-backed, and RAM-only sessions. Existing appearance/display/input settings and UI.CFG persistence remain.
- Added a deterministic standard-library generator for eight original 64x64 anti-aliased 24-bit BMP icons (home, clock, terminal, folder, settings, battery, document, image) under resources/images/icons/.
- Kept BMP files as external resources rather than linking the full icon pack into the constrained kernel image. An attempted embedded-icon link correctly hit the unchanged 0x30000 BSS reservation; the embedded object was removed from the build and the vector shell fallback retained. Explorer still needs a userland image-drawing syscall before it can render these BMP assets directly.
- Extended AIRP validation to include Explorer, in addition to Settings and Notes.
- User explicitly deferred x86-64 and Rust migrations; recorded them as backlog items and left the i386 architecture unchanged.
- Fresh build/textile.img generation passed. VNC/QEMU restarted as PID 6455; TCP connection to 127.0.0.1:5900 returned the RFB 003.008 banner. Serial log confirmed normal boot, AirGraph 800x600 desktop activation, and desktop start.
- Host tests passed: desktop chrome, VFS, FAT32, image subsystem, and BMP subsystem. BMP header validation passed for all eight files (64x64, 24-bit, valid file sizes). git diff --check passed.
- No commit, stage, or push. No user media was formatted or written.

## 2026-11-22 — Explorer VFS navigation and file-manager pass
- Re-inspected the live Git status before editing. The repository already contained many unrelated modified and untracked files; preserved them and did not reset, clean, stage, commit, or push.
- Replaced Explorer's legacy flat-filesystem listing path with the existing hierarchical VFS syscall API (readdir, stat, open, read, write, close, mkdir, unlink).
- Added folder navigation from /, parent navigation with Backspace, directory-first alphabetical ordering, item counts, search filtering, list/grid/details view switching, selected-item metadata, and a read-only text preview.
- Added NOTE##.TXT creation in the current directory and a typed folder-name flow. VFS rejects persistent /storage directory creation today; this is reported as a safe operation failure, while RAM-backed directories such as /home can accept folders.
- Changed deletion from immediate execution to explicit Y/N confirmation. Non-empty folders and read-only items remain protected by VFS behavior.
- Added include/vfs.h to the Explorer Makefile dependency rule so header changes trigger recompilation.
- Build/test evidence: make -j2 build/explorer.airp passed (AIRP output 9588 bytes); make -j2 build/textile.img passed; build/vfs_host_test passed; three-app AIRP validation passed (Explorer payload 9552 bytes, Settings 4388 bytes, Notes 2428 bytes); git diff --check passed.
- Restarted QEMU VNC as PID 10720. Port 127.0.0.1:5900 returned RFB 003.008. Serial boot log confirms vfs: hierarchical root ready, desktop: start, and AirGraph 800x600 display. The Explorer UI itself still needs manual interaction testing.
- Remaining Explorer work: rename, copy/paste, move, multi-select, image previews, and BMP icon rendering. Preview currently reads at most 8192 bytes and is intended for text-like content. VFS/FAT32 still lacks nested-directory traversal and persistent mkdir.
- No user media was formatted or written. No commit, staging, or push. x86-64 and Rust remain deferred.

## 2026-11-22 — Explorer safety guard and final rebuild
- Added a clear guard against opening imported directories directly under /storage because the current FAT32 importer does not populate their child entries. Explorer now explains that nested FAT32 browsing is unavailable instead of displaying a misleading empty folder.
- Final rebuild: Explorer AIRP 9740 bytes; updated boot image generated successfully. VFS host test and all three AIRP validators passed; Explorer validator reported 9704-byte payload, Settings 4388 bytes, Notes 2428 bytes. git diff --check passed.
- Restarted QEMU VNC as PID 11613. After the QEMU startup delay, localhost:5900 returned RFB 003.008. Serial log confirms VFS initialization and desktop start.
- Manual Explorer interaction remains for the user. No commits, staging, or pushes.

## 2026-11-22 — Mobile shell and APK app-management implementation

- Connected to the authorized Android/Termux phone through Remote Desktop Commander; confirmed ARM64 Termux environment, Node 26.3.1, and Python 3.13.13. Expanded Desktop Commander access only to the requested `$HOME/FlairOS` repository while retaining the existing `$HOME/Denk` permission.
- Inspected Git status and contributor records before editing. The worktree already contained extensive unrelated modifications and untracked files. Created local branch `feature/mobile-shell-app-management` from current HEAD `7d3e7ed`; did not stage, commit, push, reset, or clean.
- Added new `kernel/mobile.c` with an Android-style portrait home screen, app drawer, install page, package details, long-press actions, and keyboard fallback.
- Kept `kernel/desktop.c` as the desktop shell. Added dynamic Start Menu APK rows and a right-click context menu with Open, View App, and Uninstall.
- Added a shared VFS `/apps` enumeration path in both shells. Added AXI public wrappers for APK launch/install/view; package details are read with the existing APK inspector. APK installation verifies the full write and removes a partial destination on failure.
- Added lazy WinDown compositor initialization so AXI windows can render when the mobile shell is active. Added mouse polling inside the APK launch loop so application windows can receive close/click input.
- Registered the existing COM2 touch driver and wired serial touch events plus PS/2/VMMouse pointer input into the mobile shell.
- Added CMake `FLAIROS_SHELL` selection, portrait-only enforcement for mobile, CMake preset `mobile`, and Makefile `mobile` target. Kept Makefile output in `build-mobile-make` to avoid colliding with CMake's `build-mobile`.
- Fixed the CMake host-test aggregator so all registered host executables are dependencies of `host-tests`.
- Added `docs/MOBILE_SHELL.md` documenting build commands, shell architecture, app management, and limits.
- PASS: CMake mobile build; CMake desktop build; Makefile mobile build; CTest 24/24; `git diff --check`.
- PASS: QEMU mobile boot serial reached the mobile shell startup marker. Attempted monitor-driven keyboard injection for drawer/launch, but it did not produce the app-launch marker; interactive acceptance is explicitly left open.
- Important limits recorded: current mobile target is still i386 BIOS; APK support is stored-ZIP-only with a limited DEX runtime; `/apps` is RAM-backed and not reboot-persistent; physical touchscreen hardware is not yet validated.
- No staging, commit, push, repository reset/clean, user-media format, or user-media write.

- Follow-up hardening: sanitized APK install names to prevent path separators/traversal and fixed extension handling before path construction; corrected the mobile installer to strip the `.apk` suffix using the actual filename length. Rebuilt mobile + desktop CMake targets, reran all 24 CTest cases, rebuilt with `make mobile -j2`, and repeated mobile/desktop QEMU boot-marker checks. `git diff --check` remained clean.
- Observed an already-running QEMU/VNC process (PID 17343) using the pre-existing `build/textile.img`; left it untouched.


## 2026-11-22 — Main-only mobile integration reconciliation
- Rechecked the repository after the user requested the mobile work be merged and reconciled with existing `main` changes.
- Confirmed the current branch was `main`; `main` and `feature/mobile-shell-app-management` both resolved to `7d3e7ed`. The feature branch had no unique commits to merge.
- Confirmed the mobile implementation and supporting docs remain in the uncommitted working tree on `main`, alongside the existing contributor changes. No broad staging, reset, clean, overwrite, or commit was performed.
- Deleted only the redundant local `feature/mobile-shell-app-management` branch after `git merge-base --is-ancestor feature/mobile-shell-app-management main` succeeded. Kept the unrelated `feature/v2-multi-arch-linux` branch untouched.
- Reviewed the mobile integration documentation and key shared build/kernel/AXI touchpoints. The mobile path uses the shared VFS `/apps` source of truth, shared AXI/WinDown application window path, and existing touch/pointer input plumbing; desktop remains the default shell.
- `git diff --check` passed after reconciliation. Existing recorded validation remains: mobile and desktop CMake builds, Makefile mobile build, 24/24 CTest, and QEMU mobile/desktop boot markers.
- Attempted `cmake --build build -j2`; this was not a valid CMake build directory (no `CMakeCache.txt`) and exited before compiling. No build output was modified by this attempt. Fresh builds were not rerun because the prior build directories had been removed to conserve phone storage.
- No commit, push, staging, reset, or broad cleanup. Current `main` worktree remains intentionally dirty with all contributor work preserved.## 2026-11-22 17:35 — Fresh mobile image rebuild and VNC restart

- User explicitly requested a rebuild. Stopped the prior QEMU/VNC process (PID 17343) before replacing its boot image.
- Rebuilt the existing `build/textile.img` in place with `make -B -j2 BUILD=build DEVICE=phone DISPLAY=portrait FLAIROS_SHELL=mobile all`. The forced rebuild ensured every shared object was compiled with the mobile/phone/portrait flags rather than reusing desktop-configured objects.
- Build completed with exit code 0. Existing compiler warnings (mostly typedef redefinitions and unused desktop-only helpers in the mobile build) were non-fatal; no link or image-generation errors.
- Confirmed `build/textile.img` is 1,474,560 bytes and has the new 17:35 build timestamp. `build/storage.img` remains 31,457,280 bytes with its original 16:10 timestamp; it was not rebuilt or modified.
- Restarted the existing VNC workflow. New QEMU PID is 8823, using `build/textile.img` and the preserved `build/storage.img`.
- PASS: serial boot log confirms AirGraph 600x750 portrait mode and `mobile: Android-style portrait shell started`; no panic marker in the captured boot log.
- PASS: `tools/run-vnc.sh status` reports PID 8823 on `127.0.0.1:5900`; TCP port 5900 accepts a connection.
- PASS: current branch remains `main`; `git diff --check` passed.
- No source changes, staging, commit, push, reset, clean, or extra build directory creation. Interactive app-launch/install/uninstall flows remain unverified.

## 2026-11-22 — Mobile app fullscreen and pointer layering fix

- Reviewed the user's VNC screenshot: AXI/WinDown app appeared as a floating desktop window with desktop title chrome; the cursor was hidden behind the compositor's title bar.
- Root cause: `mobile_present_app()` composited WinDown desktop chrome after `axi_gfx_render()`, covering the pointer. AXI mobile apps also reused desktop window geometry and titlebar rendering.
- Changed the mobile presentation path to leave desktop WinDown chrome out of the mobile activity surface and render AXI last, preserving the pointer as the topmost layer.
- Added mobile-only full-display AXI app rendering without a floating titlebar/window frame. Added an Android-style bottom navigation pill; tapping its bottom navigation area closes/backs out of the active AXI app. Mobile pointer hit coordinates now map across the full display to app body rows/columns.
- Kept desktop AXI window geometry, titlebar, WinDown composition, and desktop pointer path behind the non-mobile compile-time branch.
- Rebuilt the existing `build/textile.img` for phone/portrait mobile mode. First forced full rebuild passed; after a small cleanup removing unused WinDown declarations from `mobile.c`, incremental relink/image rebuild also passed.
- Restarted VNC as PID 12045 on `127.0.0.1:5900`. Boot log confirms 600x750 portrait and `mobile: Android-style portrait shell started`.
- `git diff --check` passed. `build/storage.img` was not rebuilt or modified. Branch remains `main`.
- LIMITATION: VNC boot was verified, but the Settings app was not automatically launched for a visual post-fix screenshot. Fullscreen appearance and touch/Back interaction still need confirmation in the live viewer. CTest was not rerun. No commit or push.

## 2026-11-23 — Spring motion, Dock magnification, Spotlight, and Termux startup

- Re-inspected `git status` and the existing `tools/` directory before editing. The worktree already contained extensive tracked and untracked changes from other sessions; all unrelated changes were preserved. No reset, clean, broad stage, commit, or push.
- Added `shell_tick_at(now_ticks, ticks_per_second)` so the 64-bit desktop shell receives the real monotonic timer from `kernel/kmain64.c` and derives frame delta time instead of relying on fixed per-loop pixel steps. Kept `shell_tick()` as a deterministic host-test wrapper and added no-op time-aware adapters for mobile and TV shells.
- Added a bounded, damped spring integrator in `ui/shells/shell_desktop.c`. It now drives window entrance offsets, Control Center motion, Spotlight pop-in/pop-out, and per-icon Dock magnification.
- Implemented continuous pointer-distance Dock magnification with neighbor falloff; icons scale around their centers. Changed the hover icon's frame animation to a finite one-shot sequence so the UI tick can become idle rather than looping forever while the pointer stays over an icon.
- Updated `tests/runtime64_airp_test.c` to bound animation-drain loops and explicitly exercise Spotlight open/close. The test caught integer spring settling at a small residual offset; adjusted the settle threshold and reran successfully.
- Created root-level `bashsrc`. When sourced from `~/.bashrc`, it starts Termux:X11, XFCE4, brings the X11 app forward, and launches `tools/run-x11.sh` in fullscreen unless an X11 QEMU instance is already running. `FLAIROS_AUTOSTART=0` disables automatic launch while still loading the helper functions/aliases.
- Inspected VNC tooling and used `tools/vnc_ctl.py` to capture 800x600, 1024x768, 480x800, and 1280x720 guest screenshots. Port 5902 was already down; ports 5900, 5901, 5903, and 5904 were reachable. Did not restart the 1 GiB AArch64 desktop guest because of the phone's tight shared memory budget.
- Rebuilt the x86_64 desktop preset and restarted its VNC guest on port 5901. Serial output confirms `arch=x86_64`, `render=ok 1024x768`; `tools/vnc_ctl.py screenshot` produced a valid 1024x768 RGB PNG.
- VNC pointer/keyboard injection did not visibly change the guest UI during automated hover/click attempts (dock-region pixel comparison was unchanged). Screenshot capture and guest boot are verified, but pointer-driven visual acceptance remains OPEN; do not claim the Dock animation was visually confirmed through VNC.
- Build PASS: CMake default i386 build; preset builds `desktop/x86_64`, `mobile/aarch64`, and `tv/aarch64`.
- Test PASS: `runtime64_airp_test`, `airnim_host_test`, and `sprunkui_anim_host_test` (3/3 CTest); `bash -n` for `bashsrc` and VNC/X11 scripts; `FLAIROS_AUTOSTART=0` source test; changed-file `git diff --check`.
- Whole-worktree `git diff --check` still reports an existing trailing blank line in the already-modified `CMakeLists.txt` at line 417; this file was not edited for this task. No commit, stage, or push.


## 2026-11-23 — Root bashsrc now targets AArch64 Desktop

- Re-inspected the root `bashsrc`, `tools/run-x11.sh`, `tools/run-vnc-multi.sh`, and current Git status before editing. The worktree was already extensively modified/untracked; preserved all existing changes.
- Added `tools/run-x11-aarch64.sh`. It builds the `desktop/aarch64` preset, ensures the AArch64 storage image exists, and launches `qemu-system-aarch64` using `build-presets/desktop-aarch64/flairos-desktop-aarch64.elf`, VirtIO display/storage/input devices, and fullscreen SDL output.
- Updated root `bashsrc` so `flairos_up` launches the AArch64 Desktop script and checks for an existing matching AArch64 QEMU guest rather than looking for i386.
- Kept Termux:X11 and XFCE4 startup behavior. Did not add or invoke `termux-wake-lock`; the startup script includes an explicit comment documenting that choice.
- PASS: `bash -n bashsrc tools/run-x11-aarch64.sh`.
- PASS: `FLAIROS_AUTOSTART=0 bash -c 'source ./bashsrc; declare -F flairos_up ...'`.
- PASS: checked that root `bashsrc` contains no `termux-wake-lock`, i386 QEMU target, or legacy `tools/run-x11.sh` launcher.
- PASS: confirmed the AArch64 Desktop ELF already exists at `build-presets/desktop-aarch64/flairos-desktop-aarch64.elf`.
- LIVE BOOT NOT YET VERIFIED: this change was validated statically; the new AArch64 Termux:X11 guest was not launched in this follow-up.
- No stage, commit, push, reset, or clean.
