# Issues & Limitations (`agent/Gemini/issues.md`)

- **OPEN — Full Python / MicroPython 128 KiB Slot Limit**:
  - `ports/micropython/` builds to ~584 KiB, exceeding `AIRP_IMAGE_MAX` (128 KiB) and the `APP_LO..APP_HI` (`0x40000..0x80000`, 256 KiB) user slot in `drivers/vm.c`. Full CPython additionally requires `SYS_MMAP`, hierarchical VFS/directories, unified `fd` 0/1/2, `argc`/`argv`/`envp`, signals, and a C99/POSIX `libc` + `libm`.
- **OPEN — `sdk-test` on ARM64 Termux Host**:
  - `make sdk-test` compiles `tests/sdk_host_test.c` with host `cc` (aarch64 on Termux), which fails on x86 `int $0x80` inline assembly in `sdk/flareos.h`. Runs fine on x86_64 CI runners.
- **OPEN — `xorriso` Missing on Termux Host**:
  - `make test` stops at `build/livecd.iso` locally because `xorriso` is not installed in Termux; runs on Ubuntu CI runners.
- **OPEN — Uncommitted Changes from Concurrent Session**:
  - As of 16:47, the local `~/FlairOS` working tree contains uncommitted changes (`include/cursor.h`, `kernel/cursor.c`, `docs/REQUIREMENTS.md`, `tools/qemu_ui.py`, `tools/run-x11.sh`, etc.) not authored by Gemini; left untouched.
- **RESOLVED — Desktop Double-Blit Flicker**:
  - Fixed by removing premature `ag_present()` in `desktop_gfx_present()` (`58f2c88`).
- **RESOLVED — Top Bar / Desktop Icon Overlap & Legacy Text Bleed**:
  - Fixed in `desktop_gfx.c` and `desktop.c` (`58f2c88`).
- **RESOLVED — Mouse Oversensitivity & Missed Clicks**:
  - Fixed with `POINTER_STEP_X=64`/`Y=85` and `mouse_click_latch` (`58f2c88`).
- **RESOLVED — `desktop_chrome_host_test` Failure**:
  - Fixed small-surface guard in `kernel/desktop_chrome.c` and updated test expectations in `tests/desktop_chrome_host_test.c` (`85b1474`).
