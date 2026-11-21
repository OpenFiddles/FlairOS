# Gemini Activity Log (`agent/Gemini/what-i-did.md`)

All entries are chronological in local time (`America/Chicago`, `-06:00`).

## 2026-11-21 15:37 — Finished Claude's In-Progress Desktop UI Work
- **Context**: Claude's session hit its limit at 15:34:10 after editing `kernel/desktop_render.c` (procedural dusk-gradient + 3-layer dunes wallpaper with `kmalloc` cache) and `kernel/desktop_chrome.c` (rounded title bar, traffic-light controls, centered title, window-clipped drop shadow) without building or testing.
- **Built**: Ran `make -j4 all` in `~/FlairOS` (exit code `0`).
- **Verified in QEMU**: Ran `qdrive2.sh` to boot `build/textile.img` + `build/storage.img`, open the Start menu, and launch Terminal. Confirmed zero panics in `$PREFIX/tmp/serial_verify.log` and inspected screenshots (`2026-11-21_dusk-dunes-desktop.png`, `2026-11-21_terminal-traffic-light-chrome.png`).
- **Git**: Committed `kernel/desktop_chrome.c` and `kernel/desktop_render.c` as `de8be79` (`desktop: add procedural dusk dunes wallpaper and traffic-light window chrome`).

## 2026-11-21 15:44 — Bootloader Improvements, Python-Ready Syscalls & QEMU VNC
- **Bootloader (`bootloader/stage1.asm`, `bootloader/stage2.asm`, `kernel/kernel.c`)**:
  - Added `cld` and 3-attempt BIOS `int 0x13` reset/retry loop in `stage1.asm`.
  - Added real-mode banner, 1.5 s interactive boot mode menu (`[1] Normal [2] Live [3] Setup [4] Recovery [5] Diag`) when `FORCE_BOOT_MODE == 0`, and multi-method A20 gate enable + verification (`int 0x15 AX=0x2401` -> port `0x92` -> 8042 KBC) in `stage2.asm`.
  - Added serial boot-mode logging for `FLAIR_BOOT_RECOVERY` and `FLAIR_BOOT_DIAGNOSTICS` in `kernel/kernel.c`.
- **Kernel & SDK API (`kernel/textile.h`, `include/fs.h`, `kernel/fs.c`, `kernel/kernel.c`, `sdk/flareos.h`)**:
  - Added `SYS_TIME` (`39`), `SYS_FS_STAT` (`40`), and `SYS_GETRANDOM` (`41`), plus `sdk/flareos.h` inline wrappers (`flare_time_unix`, `flare_time_now`, `flare_fs_stat`, `flare_getrandom`).
- **QEMU VNC (`Makefile`, `tools/run-vnc.sh`)**:
  - Added `run-vnc` target to `Makefile` and created `tools/run-vnc.sh` (`start`/`stop`/`status`) listening on `0.0.0.0:5900` (`display :0`).

## 2026-11-21 15:52 — Fixed Desktop Flickering & Layout Overlap
- **Root cause of flicker**: `desktop_gfx_present()` in `kernel/desktop_gfx.c` called `ag_present()` before windows and shell chrome were drawn, and `desktop_gfx_flush()` called `ag_present()` a second time. Removed the premature `ag_present()` from `desktop_gfx_present()`.
- **Layout & hit-test fixes (`kernel/desktop_gfx.c`, `kernel/desktop.c`)**:
  - Shrunk top bar to `y=6..44`, moved desktop icons to rows `{3, 5, 7, 9}`, widened icon cards to `11*cw`, and updated click hit-testing.
  - Stopped `draw_frame()` from painting legacy VGA text-mode bars, icon labels, start menu text, and `0xDB` block cursor behind SprunkUI.
  - Aligned window close/minimize click regions in `desktop.c` with left traffic-light buttons (`w->x + 1..2`, `w->x + 3..4`).
- **Screenshots**: Saved `2026-11-21_desktop-layout-fixed.png` and `2026-11-21_start-menu-layout-fixed.png`.

## 2026-11-21 15:56 — Fixed Mouse Sensitivity & Missed Clicks
- **Sensitivity (`kernel/mouse_init.c`, `kernel/desktop_gfx.c`)**: Changed `POINTER_STEP_X`/`POINTER_STEP_Y` from `205`/`655` to `64`/`85` (~0.78 px/count in both X and Y on 800×600) and drew the pointer at exact pixel coordinates (`cursor_norm_x`/`cursor_norm_y`).
- **Missed taps (`kernel/mouse_init.c`, `kernel/kernel.h`, `kernel/desktop.c`, `kernel/axi.c`)**: Added `mouse_click_latch` in `mouse_irq_handler()` and `mouse_take_clicks()` so fast clicks/taps occurring within a 20 ms `FRAME_TICKS` sleep are never lost.

## 2026-11-21 16:01 — GitHub Actions v86 Web Workflow & Push
- Created `.github/workflows/v86-pages.yml` to build `build/livecd.iso` + `build/textile.img`, bundle `web/index.html` with v86 runtime (`libv86.js`, `v86.wasm`, `seabios.bin`, `vgabios.bin`), upload `flairos-v86-web` artifact, and deploy to GitHub Pages.
- Committed and pushed `58f2c88` to `origin/main`.

## 2026-11-21 16:07 — Fixed `desktop_chrome_host_test` CI Failure
- Relaxed small-surface guard in `kernel/desktop_chrome.c` from `cw < 5 || ch < 12` to `cw < 2 || ch < 4` (clamping `d >= 2`) so the `160×100` host test surface works.
- Updated `tests/desktop_chrome_host_test.c` assertions for the new title bar (`0x00303544`) and left traffic-light close button (`0x00F0605A`).
- Verified all 5 host C tests + `tests/boot_layout_test.py` passed; committed and pushed `85b1474` to `origin/main`.

## 2026-11-21 16:47 — Created Mandatory Agent Recordkeeping Directory
- Created `agent/Gemini/` with `what-i-did.md`, `current-task.md`, `decisions.md`, `issues.md`, `tests.md`, `handoff.md`, and `screenshots/`.
