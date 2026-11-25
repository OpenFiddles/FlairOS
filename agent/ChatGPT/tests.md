# Test Record

Environment: Android / Termux, ARM64, Node.js 26.3.1, Python 3.13.13, host cc, repository $HOME/FlairOS.
Date: 2026-11-21 (America/Chicago).

## PASS - Existing host-test executables
- **Command:** Built/rebuilt available host-test targets, then executed each available binary.
- **Expected:** Each host test exits 0.
- **Actual:** All 15 executed binaries exited 0: airgraph_host_test, driver_host_test, fb_host_test, ata_identify_host_test, pmm_host_test, vm_region_host_test, desktop_render_host_test, desktop_chrome_host_test, windown_host_test, task_host_test, elf_host_test, vfs_host_test, bmp_host_test, image_host_test, cursor_host_test.
- **Status:** PASS for these individual executions only.

## PASS - Python boot layout test
- **Command:** python3 tests/boot_layout_test.py
- **Expected:** Boot layout and AIRP boundary assertions pass.
- **Actual:** boot layout and 128 KiB AIRP boundary checks passed.
- **Status:** PASS.

## PASS - AIRP app image checks
- **Command:** python3 tests/airp_apps_test.py build/settings.airp build/notes.airp
- **Expected:** Settings and Notes images pass format/API/size checks.
- **Actual:** Settings AIRP v1, API 2, 3400 bytes OK; Notes AIRP v1, API 2, 2428 bytes OK.
- **Status:** PASS.

## FAIL - Aggregate make test
- **Command:** make test
- **Expected:** Build boot media and execute configured tests.
- **Actual:** Failed creating build/livecd.iso: xorriso: No such file or directory (exit 127). Host tests were not reached by this invocation.
- **Status:** FAIL (missing environment dependency).

## FAIL - Fresh I/O manager host-test build
- **Command:** make -j4 build/io_host_test
- **Expected:** Compile and link the I/O manager host test.
- **Actual:** Link failed: undefined symbol serial_str, referenced by io_selftest in drivers/io_manager.c.
- **Status:** FAIL.

## NOT RUN - Full clean build and QEMU acceptance
- **Command considered:** make clean && make -j4 all && make acceptance
- **Expected:** Clean image build and headless QEMU acceptance.
- **Actual:** Not executed; existing build artifacts were preserved and QEMU was not run.
- **Status:** NOT RUN.

## PASS WITH LIMITATION - Existing image target
- **Command:** make -j4 all
- **Expected:** Build the default image if sources are newer than artifacts.
- **Actual:** make: Nothing to be done for 'all'.
- **Status:** No-op target check only; not evidence of a fresh build.

## FAT32/VFS storage pass — 2026-11-21

- PASS: make -j2 build/fat32_host_test build/vfs_host_test
- PASS: build/fat32_host_test
- PASS: build/vfs_host_test (includes 90 directory creations and 24 simultaneous descriptors)
- PASS: make -j2 build/fat32.o build/vfs.o
- PASS: git diff --check
- FAIL/BLOCKED: make -j2 build/textile.img; link assertion reports __bss_end=0x30650 > 0x30000.
- NOT RUN: QEMU boot with FAT32 media; no disk was formatted or written.


## Kernel BSS recovery follow-up — 2026-11-21

- PASS: `make -j2 build/cursor.o build/cursor_host_test build/textile.img` — kernel ELF/bin and boot image rebuilt; linker assertion passed unchanged.
- PASS: `nm -n build/kernel.elf | grep __bss_end` — `0002f654 B __bss_end`, below 0x30000.
- PASS: `build/cursor_host_test` — cursor host test OK after heap-backed custom cursor storage.
- PASS: `build/fat32_host_test` — all checks passed.
- PASS: `build/vfs_host_test` — all checks passed.
- PASS: `git diff --check`.
- NOT VERIFIED: `tests/smoke.py build/textile.img` was started; no final process completion/result was captured.
- NOT RUN: FAT32 QEMU media test. Existing smoke fixture creates a 30 MiB custom FSDY filesystem, not FAT32.


## FAT32 writable file operations — 2026-11-21

- PASS: `make -j2 build/fat32_host_test` and `build/fat32_host_test` — mount/read/create/replace/write/delete; 700-byte two-cluster payload; both FAT copies agree; duplicate and invalid-name cases rejected.
- PASS: `make -j2 build/vfs_host_test` and `build/vfs_host_test` — VFS hierarchical filesystem tests pass.
- PASS: `make -j2 build/fat32.o build/vfs.o` — freestanding i386 objects compile.
- PASS: `make -j2 build/textile.img` — kernel ELF/bin and boot image link/build successfully.
- PASS: `nm -n build/kernel.elf | grep __bss_end` — `0002f654 B __bss_end`, under the unchanged 0x30000 linker reservation.
- PASS: `git diff --check`.
- NOT PASS: `QEMU=qemu-system-i386 python3 tests/smoke.py build/textile.img` — boot reached developer shell, but custom filesystem `format` reported write failure; later `run` hit `PANIC: AXI window presentation requires AirGraph`.
- NOT RUN: FAT32 QEMU media write/reboot persistence; no `mkfs.fat` tool installed. Host test uses an isolated in-memory FAT32 image.

## Fresh build and QEMU FAT32 persistence — 2026-11-21

- FAIL (diagnostic baseline): `make -B -j2 build/kernel.elf` — reproduced `kernel bss overlaps...`; map showed `__bss_end=0x30654` after the code/data layout shifted BSS to `0x26000`.
- PASS: changed `HWINFO_MAX_PCI` from 128 to 80; `make -j2 build/textile.img` — fresh link and boot image succeeded with the original linker assertions unchanged.
- PASS: `nm -n build/kernel.elf | grep -E '__bss_(start|end)'` — `00026000 B __bss_start`, `0002fd54 B __bss_end`; 0x2AC bytes remain before 0x30000.
- PASS: QEMU boot 1 with disposable `build/qemu-fat32-test.img` — FAT32 MBR partition mounted; VFS test command created `/storage/QEMU.TXT`; serial marker `fat32: persistence test file written` observed.
- PASS: QEMU boot 2 with the same disk image — `fat32verify-test` printed `fat32: reboot persistence verified`.
- PASS: independent image inspection — `QEMU    TXT`, cluster 3, 37 bytes; exact payload `FlairOS FAT32 reboot persistence test`.
- PASS: diagnosed ATA PIO read failure; moved drive selection ahead of STATUS polling in `ata_setup_device()`. QEMU mount/write/reboot validation passed after this change.
- PASS after removing temporary debug logging: `make -j2 build/textile.img`, `build/fat32_host_test`, `build/vfs_host_test`, `git diff --check`; no `fat32-debug` or `ata-debug` markers remain.
- LIMITATION: the final debug-free rebuild was validated by build/host tests; the successful two-boot QEMU persistence run was captured immediately before removing diagnostic-only serial messages. Functional ATA/VFS/FAT32 logic was unchanged by that cleanup.
- NOT PASS: full `tests/smoke.py` acceptance remains blocked by its separate custom filesystem format-write failure and AXI/AirGraph panic. Do not mark the aggregate QEMU smoke suite green.
- SAFETY: only the disposable FAT32 image was written; no actual user media was touched.

 
## Material 3 UI and app validation — 2026-11-21
- PASS: make -j2 build/textile.img after desktop shell changes; kernel link and boot image generation succeeded with existing linker assertions unchanged.
- PASS: make -j2 build/explorer.airp build/settings.airp build/textile.img; Explorer and Settings compiled and packed within reserved AIRP slots.
- PASS: make -j2 build/desktop_chrome_host_test build/vfs_host_test build/fat32_host_test build/image_host_test build/bmp_host_test.
- PASS: desktop_chrome_host_test, vfs_host_test, fat32_host_test, image_host_test, bmp_host_test.
- PASS: python3 tests/airp_apps_test.py build/explorer.airp build/settings.airp build/notes.airp — Explorer (7972-byte image), Settings (4388-byte image), and Notes (2428-byte image) all passed AIRP v1/API 2, entry-point, header, and reserved-slot checks.
- PASS: Python BMP header checks for all eight generated icons: BM signature, exact file size, 64x64 dimensions, and 24-bit pixels.
- PASS: QEMU VNC restart and TCP handshake: 127.0.0.1:5900 returned RFB 003.008; serial log showed normal boot, AirGraph 800x600 desktop, and desktop start.
- PASS: git diff --check.
- LIMITATION: Embedded full-resolution BMP icon object exceeded the fixed kernel BSS reservation. It was excluded from the kernel build; BMPs remain external assets and shell glyphs use AirGraph vectors. No linker reservation was weakened.
- LIMITATION: Battery telemetry is not available from the current firmware/device interface; Settings and taskbar display unknown/unavailable rather than a fabricated charge value.
- NOT RUN: Full aggregate make test and broad QEMU smoke suite; known xorriso and unrelated smoke failures remain separate.
- SAFETY: no commit, staging, push, user-media formatting, or user-media writes.

## Explorer VFS navigation milestone — 2026-11-22
- PASS: make -j2 build/explorer.airp — freestanding i386 compile/link and AIRP packaging passed; airpack reported 9588 bytes.
- PASS: make -j2 build/textile.img — boot image regenerated with the updated Explorer app.
- PASS: build/vfs_host_test — hierarchical VFS host test passed.
- PASS: python3 tests/airp_apps_test.py build/explorer.airp build/settings.airp build/notes.airp — Explorer, Settings, and Notes passed AIRP v1/API 2 validation; reported image payload sizes 9552, 4388, and 2428 bytes respectively.
- PASS: git diff --check.
- PASS: QEMU VNC restarted (PID 10720); TCP handshake on 127.0.0.1:5900 returned RFB 003.008.
- PASS: serial boot log shows vfs: hierarchical root ready, AirGraph 800x600 display activation, and desktop: start.
- NOT YET VERIFIED: manual Explorer interaction in the guest (navigation, grid/details switching, folder creation, text preview, delete confirmation).
- LIMITATION: full aggregate make test and broad QEMU smoke were not run. Existing xorriso and unrelated QEMU smoke failures remain.
- SAFETY: no commit, stage, push, user-media formatting, or user-media writes.

## Explorer final rebuild after nested-folder guard — 2026-11-22
- PASS: make -j2 build/explorer.airp build/textile.img — final Explorer compile/link/AIRP package (9740 bytes) and boot image build succeeded.
- PASS: build/vfs_host_test — all hierarchical VFS host checks passed.
- PASS: python3 tests/airp_apps_test.py build/explorer.airp build/settings.airp build/notes.airp — Explorer payload 9704 bytes, Settings 4388 bytes, Notes 2428 bytes; all AIRP checks passed.
- PASS: git diff --check.
- PASS: restarted QEMU VNC PID 11613; after startup, TCP handshake returned RFB 003.008 and serial log reached VFS initialization and desktop start.
- NOT YET VERIFIED: manual Explorer UI interactions.

## Mobile shell and APK app-management milestone — 2026-11-22

- PASS: `cmake --build --preset mobile -j2` — i386 phone/portrait mobile shell build succeeded.
- PASS: `cmake --build --preset default -j2` — desktop configuration still builds.
- PASS: `make mobile -j2` — independent Makefile mobile build generated `build-mobile-make/textile.img`.
- PASS: `ctest --test-dir build-mobile --output-on-failure` — 24/24 tests passed, including VFS, WinDown, APK parser, DEX, Binder, ashmem, FAT32, AirGraph, and AIRP app validation.
- PASS: `git diff --check`.
- PASS: QEMU boot with portrait 600x750 framebuffer reached serial marker `mobile: Android-style portrait shell started`.
- NOT VERIFIED: Automated QEMU monitor keyboard injection did not produce `mobile: app drawer opened` or `mobile: launching installed APK`; no end-to-end APK launch claim.
- NOT VERIFIED: Visual/touch interaction for the home screen, app drawer, installer, long-press details, desktop Start Menu APK rows, right-click context menu, View App window, uninstall, or install.
- LIMITATION: `/apps` is RAM-backed; no reboot persistence for installed APKs.
- LIMITATION: APK extraction supports stored ZIP entries only; DEX runtime is a limited subset.
- SAFETY: no reset, clean, stage, commit, push, user-media formatting, or user-media writes.

## Final QEMU boot verification — 2026-11-22

- PASS: Mobile image booted under QEMU with the 600x750 portrait framebuffer and serial marker `mobile: Android-style portrait shell started`.
- PASS: Desktop image booted under QEMU with the 800x600 landscape framebuffer and serial marker `desktop: start`.
- Both QEMU processes were deliberately stopped by the 9-second timeout after the boot markers; exit code 124 is the expected timeout result, not a guest crash.
- No panic marker appeared in either captured serial log.
- Interactive APK launch and UI action testing remain unverified.


## Main-only integration reconciliation — 2026-11-22
- PASS: `git merge-base --is-ancestor feature/mobile-shell-app-management main` — confirmed the feature tip was already contained in `main` (same commit `7d3e7ed`).
- PASS: `git branch -d feature/mobile-shell-app-management` — removed only the redundant local branch; current branch remains `main`.
- PASS: `git diff --check` after branch cleanup.
- NOT RUN: fresh mobile/desktop rebuild and CTest rerun. The prior build output directories had been removed to conserve phone storage; `build/` is a Makefile artifact directory, not a CMake build tree (`CMakeCache.txt` absent). An attempted `cmake --build build -j2` exited immediately with that diagnostic and did not compile or modify outputs.
- Existing evidence retained (not rerun in this reconciliation): CMake mobile and desktop builds passed; Makefile mobile build passed; CTest 24/24 passed; QEMU mobile portrait and desktop landscape boot markers passed. Interactive APK/app-management UI flows remain unverified.## Fresh mobile rebuild and live VNC boot — 2026-11-22 17:35

- PASS: `make -B -j2 BUILD=build DEVICE=phone DISPLAY=portrait FLAIROS_SHELL=mobile all` — complete forced compile/link/image build, exit code 0.
- PASS: `llvm-nm build/kernel.elf` shows the `mobile_run` symbol in the linked kernel.
- PASS: QEMU serial log reports `display: AirGraph pixel desktop active (600x750 portrait)` and `mobile: Android-style portrait shell started`.
- PASS: VNC process PID 8823 is alive; `tools/run-vnc.sh status` reports `127.0.0.1:5900`; TCP 5900 connection succeeds.
- PASS: `git diff --check`; branch is `main`.
- PASS: `build/storage.img` size and modification timestamp unchanged from pre-build inspection (31,457,280 bytes; 2026-11-22 16:10:52 -0600). Only `build/textile.img` was regenerated.
- NOT RUN in this rebuild: host CTest suite. Previously recorded 24/24 CTest result belongs to the earlier validation run; do not attribute it to this invocation.
- NOT VERIFIED: interactive touch/keyboard UI behavior, APK install/launch/uninstall, or physical phone boot. The current image remains an i386 BIOS/QEMU target.

## Mobile fullscreen/pointer layering fix — 2026-11-22

- PASS: mobile phone/portrait build and boot-image generation completed (`make -j2 BUILD=build DEVICE=phone DISPLAY=portrait FLAIROS_SHELL=mobile all`; exit 0 after the source cleanup; preceding forced full rebuild also passed).
- PASS: VNC restarted as PID 12045; serial log confirms 600x750 portrait and `mobile: Android-style portrait shell started`.
- PASS: `git diff --check`; current branch `main`.
- PASS: `build/storage.img` remained unchanged; only the existing boot image was regenerated.
- NOT RUN: CTest/host-test suite.
- NOT VERIFIED: post-fix visual Settings launch, pointer visibility while over app content, tap-to-Back behavior, physical touch hardware. The new mobile full-screen path is compiled and booted, but interactive acceptance remains open.

## Spring motion and VNC startup milestone — 2026-11-23

- PASS: `cmake --build --preset default -j2` — i386 desktop build completed (non-fatal existing typedef/unused-helper warnings only).
- PASS: `python3 tools/build_preset.py --preset desktop --arch x86_64` — desktop ELF + multiboot wrapper generated.
- PASS: `python3 tools/build_preset.py --preset mobile --arch aarch64` — mobile ELF generated.
- PASS: `python3 tools/build_preset.py --preset tv --arch aarch64` — TV ELF generated.
- PASS: `cmake --build build-cmake --target runtime64_airp_test -j2` and `./build-cmake/runtime64_airp_test` — runtime/ART/AirP/shell host test passed, including bounded Dock/Control Center and Spotlight animation settling.
- PASS: `ctest --test-dir build-cmake --output-on-failure -R 'runtime64_airp_test|airnim_host_test|sprunkui_anim_host_test'` — 3/3 passed.
- PASS: `FLAIROS_AUTOSTART=0 bash -c 'source ./bashsrc; ...'` — sourceable helper functions loaded without launching GUI/QEMU.
- PASS: `bash -n bashsrc tools/run-x11.sh tools/run-vnc-multi.sh tools/vnc_ctl.py`.
- PASS: changed-file `git diff --check` for the files touched in this milestone.
- PASS: VNC screenshot capture on ports 5900, 5901, 5903, and 5904. Rebuilt x86_64 desktop guest on port 5901 reached `arch=x86_64` and `render=ok 1024x768`; screenshot validated as 1024x768 RGB.
- NOT VERIFIED: VNC pointer/keyboard injection did not change the Dock region or trigger visible UI actions. Interactive Dock magnification, Spotlight, and Control Center still need pointer-path acceptance.
- NOT AVAILABLE: VNC port 5902 was closed; left it stopped rather than risk restarting a 1 GiB guest with constrained phone memory.
- NOTE: whole-worktree `git diff --check` reports an existing trailing blank line at `CMakeLists.txt:417`; this file was not modified in this milestone.
- No staging, commit, push, reset, clean, or user-media writes.


## 2026-11-23 — AArch64 Desktop bashsrc correction

- PASS: `bash -n bashsrc tools/run-x11-aarch64.sh`.
- PASS: `FLAIROS_AUTOSTART=0 bash -c 'source ./bashsrc; declare -F flairos_up >/dev/null'` — helpers load without starting the desktop.
- PASS: root `bashsrc` contains no `termux-wake-lock`, `qemu-system-i386`, or old `tools/run-x11.sh` launch reference.
- PASS: `build-presets/desktop-aarch64/flairos-desktop-aarch64.elf` exists.
- NOT YET RUN: live Termux:X11 launch/boot using `tools/run-x11-aarch64.sh`; do not report visual or guest boot acceptance until tested.
