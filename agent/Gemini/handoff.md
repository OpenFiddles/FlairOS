# Handoff (`agent/Gemini/handoff.md`)

## What Was Accomplished
1. Finished Claude's interrupted `desktop_render.c` + `desktop_chrome.c` work (`de8be79`), verifying dusk dunes wallpaper and traffic-light window chrome in QEMU.
2. Improved `bootloader/stage1.asm` (disk retry) and `bootloader/stage2.asm` (real-mode banner, 1.5 s boot mode menu when `FORCE_BOOT_MODE==0`, multi-method A20 gate).
3. Added Python-readiness syscalls `SYS_TIME` (`39`), `SYS_FS_STAT` (`40`), and `SYS_GETRANDOM` (`41`) to kernel and `sdk/flareos.h`.
4. Set up QEMU VNC (`Makefile` `run-vnc` target and `tools/run-vnc.sh` on `127.0.0.1:5900`).
5. Fixed desktop double-blit flicker, top-bar/icon overlap, legacy text-mode bleed-through, mouse sensitivity (`64`/`85`), and missed fast clicks (`mouse_click_latch`).
6. Added `.github/workflows/v86-pages.yml` (`58f2c88`) and fixed `tests/desktop_chrome_host_test.c` (`85b1474`).

## Current Repository State
- `origin/main` is at `85b1474` (plus this `agent/Gemini/` commit).
- **Important**: The local `~/FlairOS` working tree currently has uncommitted files from another concurrent session (`kernel/cursor.c`, `include/cursor.h`, `docs/REQUIREMENTS.md`, `tools/qemu_ui.py`, `tools/run-x11.sh`, etc.). Inspect `git status` and `git diff` before modifying shared files.

## Important Files
- `bootloader/stage1.asm`, `bootloader/stage2.asm` — Bootloader stages.
- `kernel/desktop.c`, `kernel/desktop_gfx.c`, `kernel/desktop_chrome.c`, `kernel/desktop_render.c` — Pixel desktop compositor & chrome.
- `kernel/mouse_init.c`, `kernel/kernel.c`, `kernel/fs.c`, `sdk/flareos.h` — Input & syscall ABI.
- `.github/workflows/v86-pages.yml`, `web/index.html` — v86 browser launch pipeline.
- `tools/run-vnc.sh` — Termux QEMU VNC launcher.

## Recommended Next Actions
1. Expand `APP_LO..APP_HI` in `drivers/vm.c` and `AIRP_IMAGE_MAX` in `include/airp.h` (or add a disk-backed ELF/AIRP loader from `storage.img`) so `ports/micropython/` (~584 KiB) can boot.
2. Wire `ports/micropython/` to `SYS_TIME` (`39`) and `SYS_FS_STAT` (`40`), then enable `MICROPY_PY_TIME` and `MICROPY_VFS`.
3. Enable GitHub Pages (**Settings → Pages → GitHub Actions**) on `OpenFiddles/FlairOS` if not already enabled so `v86-pages.yml` deploys live.
