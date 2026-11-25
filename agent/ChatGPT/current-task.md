# Current Task

- **Current task:** FlairOS mobile shell, APK application discovery, and shell-specific app management.
- **Status:** Mobile shell and APK app-management implementation is present in the `main` working tree. The former feature branch was redundant (same HEAD as main) and has been deleted. Prior mobile/desktop builds and 24/24 CTest results are recorded below. Interactive APK launch and UI action validation still need hands-on QEMU/VNC testing.
- **Repository:** $HOME/FlairOS
- **Branch / HEAD:** `main` at `7d3e7ed`; no feature branch merge commit exists because the feature branch had no unique commits. Changes remain uncommitted in the main working tree.
- **Existing worktree:** Already contained extensive tracked modifications and untracked work from other contributors before this task. All of it was preserved. No reset, clean, broad overwrite, stage, or commit was performed.
- **Shell split:** `kernel/desktop.c` remains the desktop controller; new `kernel/mobile.c` owns the portrait mobile home screen, app drawer, install page, app details, and mobile input loop.
- **Build selection:** CMake now has `FLAIROS_SHELL=desktop|mobile`; the mobile shell requires `FLAIROS_DEVICE=phone` and `FLAIROS_DISPLAY=portrait`. Added CMake preset `mobile` and Makefile `mobile` target. Separate Makefile output path is `build-mobile-make`.
- **App discovery:** Both shells enumerate APKs from VFS `/apps`. Desktop Start Menu lists up to four installed APKs and right-click exposes Open, View App, and Uninstall. Mobile drawer lists up to twelve APKs; long-press opens package details with Open App and Uninstall. Mobile Install APK scans top-level `/storage`.
- **Launch path:** Added AXI launch/install/view wrappers; initialized WinDown lazily so AXI windows can exist in mobile mode; APK launch loop now polls mouse input while presenting. APK install checks full writes and removes partial output.
- **Input:** Registered the existing COM2 serial touch driver. Mobile shell consumes touch bridge events and PS/2/VMMouse pointer events (QEMU/VNC emulation). Physical touchscreen hardware has not been validated.
- **Compatibility limits:** Current boot target is still i386 BIOS, not native ARM64 phone boot. APK extraction is stored-ZIP-only and DEX execution is a limited subset, not general Android compatibility. `/apps` is currently RAM-backed, so installed APK persistence across reboot is not implemented.
- **Builds:** `cmake --build --preset mobile -j2` passed; `cmake --build --preset default -j2` passed; `make mobile -j2` passed and generated `build-mobile-make/textile.img`.
- **Tests:** `ctest --test-dir build-mobile --output-on-failure` passed 24/24; `git diff --check` passed.
- **QEMU:** Mobile serial log confirmed the 600x750 portrait display and `mobile: Android-style portrait shell started`; desktop serial log confirmed the 800x600 landscape display and `desktop: start`. Both guests ran without a panic marker until the deliberate 9-second timeout. Automated monitor keyboard injection did not produce the app-drawer or APK-launch marker; interactive app launch remains unverified.
- **Documentation:** Added `docs/MOBILE_SHELL.md` with build instructions, architecture, behavior, and compatibility/persistence limits.
- **Next:** Validate the actual mobile UI and seeded APK launch in an interactive QEMU/VNC session; verify right-click desktop context actions, install/uninstall, and reboot persistence before calling app management complete.


## 2026-11-22 17:35 — Rebuilt image / live VNC

- **Status:** Fresh mobile phone/portrait image built successfully and is now running in QEMU/VNC.
- **Build:** `make -B -j2 BUILD=build DEVICE=phone DISPLAY=portrait FLAIROS_SHELL=mobile all` (exit 0); rebuilt `build/textile.img` in place, no extra build directory.
- **Live session:** QEMU PID 8823, VNC `127.0.0.1:5900`; boot log confirms 600x750 portrait and `mobile: Android-style portrait shell started`.
- **Storage preservation:** `build/storage.img` was not modified.
- **Validation:** `git diff --check` passed. CTest was not rerun during this rebuild. Interactive APK and physical touchscreen behavior remain unverified.


## 2026-11-22 — Mobile fullscreen and cursor layering follow-up

- **Status:** Implemented mobile-only fullscreen AXI activity rendering and pointer-topmost ordering; rebuilt and restarted VNC.
- **Changes:** Mobile activities now fill the portrait display without desktop titlebar chrome. The pointer is drawn after app content. Bottom navigation area is wired as Back/close. Desktop rendering remains on its existing non-mobile path.
- **Build/boot:** Incremental mobile phone/portrait build passed; QEMU/VNC PID 12045 at `127.0.0.1:5900`; boot marker confirms 600x750 portrait mobile shell.
- **Validation gaps:** No post-fix Settings screenshot or CTest run yet; physical touch behavior remains unverified.


## 2026-11-23 — Corrected root bashsrc to boot AArch64 Desktop

- **User correction:** Root `bashsrc` must boot the AArch64 Desktop guest, not the i386 guest.
- **Startup changes:** Added `tools/run-x11-aarch64.sh`, which builds/selects `desktop-aarch64`, ensures `build/storage-a64.img` exists, and starts `qemu-system-aarch64` with the AArch64 Desktop ELF in fullscreen Termux:X11.
- **Root bashsrc:** Updated `flairos_up` to launch the new AArch64 script and detect an existing AArch64 Desktop QEMU process. XFCE4 and Termux:X11 startup remain intact.
- **Wake lock:** Explicitly does not invoke `termux-wake-lock`; no wake-lock request or repeated wake-lock output is part of startup.
- **Validation:** `bash -n bashsrc tools/run-x11-aarch64.sh` passed; `FLAIROS_AUTOSTART=0` source test passed; grep confirmed root `bashsrc` contains no wake-lock command, i386 QEMU target, or old `run-x11.sh` launcher. The AArch64 Desktop ELF already exists at `build-presets/desktop-aarch64/flairos-desktop-aarch64.elf`.
- **Not yet verified:** The newly configured AArch64 guest has not yet been launched through Termux:X11 in this follow-up, so live fullscreen boot is still pending.
- **Repository safety:** Existing extensive worktree changes were preserved. No staging, commit, push, reset, or clean.
