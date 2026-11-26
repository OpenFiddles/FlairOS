# FlairOS Mobile Shell and APK App Management

## Build and run

CMake preset:

```sh
cmake --preset mobile
cmake --build --preset mobile -j2
ctest --test-dir build-mobile --output-on-failure
```

Makefile alternative:

```sh
make mobile -j2
```

The mobile preset selects the existing i386 BIOS kernel, phone display profile, 600x750 portrait framebuffer, and `FLAIROS_SHELL=mobile`. The mobile shell rejects non-portrait display profiles. The desktop build remains the default:

```sh
cmake --preset default
cmake --build --preset default -j2
```

## Shell split

- `kernel/ui/desktop.c` remains the desktop session and Start Menu controller.
- `kernel/ui/mobile.c` owns the mobile home screen, app drawer, install page, app details, and mobile input loop.
- Both shells use the same VFS `/apps` directory as their installed-APK source of truth.
- WinDown is lazily initialized through `desktop_windown_get()`, allowing AXI application windows to render when the mobile shell is active.
- The kernel selects the shell at compile time with `FLAIROS_SHELL_MOBILE`; it does not run the desktop shell as the default mobile UI.

## App management

### Desktop

The Start Menu enumerates up to four APK files from `/apps` after the built-in entries. Selecting an APK launches it. Right-clicking an installed APK opens a context menu with:

- Open
- View App
- Uninstall

The View App action parses available APK metadata and opens an AXI/WinDown information window.

### Mobile

The home screen provides Files, Terminal, Settings, Notes, and the app drawer. The drawer enumerates up to twelve installed APKs from `/apps`. Tapping launches; long-press opens package details with Open App, Uninstall, and Back actions. The Install APK page enumerates top-level APK files in `/storage` and copies selected packages into `/apps`. A serial touch bridge on COM2 is supported, and PS/2/VMMouse pointer input is used for QEMU/VNC touch emulation.

APK installation checks that the full file was written; partial writes are removed. Both shells re-enumerate `/apps` rather than maintaining separate lists.

## Important compatibility and persistence limits

- This is a portrait shell build on the existing **i386 BIOS** target. It is not yet a native ARM64 phone boot target.
- The current APK path is early compatibility groundwork, not general Android compatibility. ZIP extraction supports stored entries only and the DEX interpreter implements a limited opcode/runtime subset. Most ordinary Android applications, including a full Chrome APK, are not expected to run.
- The current `/apps` directory is RAM-backed. APKs installed there are discoverable in both shells during the current session, but durable installation across reboot needs a persistent package store and recovery/update policy.
- The Install APK page currently scans only the top level of `/storage`.
- Physical touchscreen hardware has not been validated on a bare-metal phone. The optional COM2 touch protocol and QEMU pointer path are available for testing.
- QEMU boot reached the mobile shell. Automated monitor keyboard injection did not produce an app-launch marker, so interactive APK launch, close, install, and uninstall still require hands-on QEMU/VNC validation.

## Validation captured

- `cmake --build --preset mobile -j2`: PASS.
- `cmake --build --preset default -j2`: PASS.
- `make mobile -j2`: PASS; generated `build-mobile-make/textile.img`.
- `ctest --test-dir build-mobile --output-on-failure`: 24/24 PASS.
- `git diff --check`: PASS.
- QEMU mobile serial log reached `mobile: Android-style portrait shell started`.
- No changes were staged, committed, or pushed. Existing work from other contributors was preserved.
