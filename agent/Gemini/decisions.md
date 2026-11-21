# Engineering Decisions (`agent/Gemini/decisions.md`)

## 1. Single-Present Compositing in `desktop_gfx.c`
- **Decision**: Removed `ag_present()` from `desktop_gfx_present()`, leaving `desktop_gfx_flush()` as the sole scanout blit per frame.
- **Reasoning**: `desktop_gfx_present()` previously blitted the bare wallpaper/text cells to the VBE LFB before `desktop_gfx_draw_window()`, `desktop_gfx_draw_shell()`, and `axi_gfx_render()` ran, causing 50 Hz full-screen flicker.
- **Consequences**: Eliminates double-blit tearing/flicker while keeping the off-screen `render_surface` architecture intact.

## 2. Interrupt-Time Click Latching (`mouse_click_latch`)
- **Decision**: Added `mouse_click_latch` in `kernel/mouse_init.c` and `mouse_take_clicks()` consumed by `desktop.c` and `axi.c`.
- **Reasoning**: `desktop_run()` sleeps `FRAME_TICKS` (20 ms) between polls; quick taps (especially over VNC on mobile) press and release within 20 ms and were missed by edge-detection on `mouse_buttons` alone.
- **Consequences**: Every button-down edge is preserved across frame sleeps without changing the `int 0x80` `SYS_POINTER` ABI.

## 3. Smooth 1:1 Pointer Scaling (`64` / `85`)
- **Decision**: Set `POINTER_STEP_X = 64u` and `POINTER_STEP_Y = 85u` in `kernel/mouse_init.c` and rendered the pointer at pixel-level normalized coordinates in `desktop_gfx.c`.
- **Reasoning**: Previous `205`/`655` steps moved 2.5 px (X) and 6.0 px (Y) per PS/2 count, causing extreme sensitivity and 2.4× vertical distortion.
- **Consequences**: Uniform ~0.78 px/count movement in both axes on 800×600.

## 4. Stage 2 Interactive Boot Menu Guarded by `FORCE_BOOT_MODE == 0`
- **Decision**: Added a 1.5 s BIOS tick-based boot-mode selector in `bootloader/stage2.asm` only when `FORCE_BOOT_MODE == 0`.
- **Reasoning**: Allows selecting Normal, Live, Setup, Recovery, or Diagnostics interactively on standard floppy/HD boots without slowing down `livecd` (`FORCE_BOOT_MODE=1`) or `bootcd` (`FORCE_BOOT_MODE=2`) media or automated tests.

## 5. GitHub Actions v86 Bundle (`v86-pages.yml`)
- **Decision**: Created `.github/workflows/v86-pages.yml` with `build-v86` (producing `flairos-v86-web` artifact) and non-blocking `deploy-pages` (`continue-on-error: true`).
- **Reasoning**: Ensures CI stays green even before GitHub Pages is enabled in repository settings, while still delivering a ready-to-host `dist/` bundle.
