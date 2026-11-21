# Test Log (`agent/Gemini/tests.md`)

Environment: Android Termux (aarch64), `clang --target=i386-none-elf`, `ld.lld`, `nasm`, `qemu-system-i386` 10.2.1.

## 2026-11-21 15:37 — Build & QEMU Desktop/Terminal Verification (`de8be79`)
- **Command**: `make -j4 all`
  - **Expected**: Exit 0, `build/textile.img` created.
  - **Actual**: Exit 0. **PASS**
- **Command**: `$PREFIX/tmp/qdrive2.sh verify "screendump @T@/verify_1.ppm@1;sendkey tab@2;screendump @T@/verify_2.ppm@1;sendkey 3@4;screendump @T@/verify_3.ppm@1"`
  - **Expected**: Boots to pixel desktop, opens Start menu and Terminal with no panics.
  - **Actual**: `serial_verify.log` ended at `desktop: open terminal` / `vm: app space ready`, `NO_PANICS`. Screenshots saved (`2026-11-21_dusk-dunes-desktop.png`, `2026-11-21_terminal-traffic-light-chrome.png`). **PASS**

## 2026-11-21 15:47 — Bootloader, Syscalls & VNC Smoke (`58f2c88` prep)
- **Command**: `make -j4 all storage-image && ./tools/run-vnc.sh start` + Python socket check to `127.0.0.1:5900`
  - **Expected**: Clean build, VNC banner `RFB 003.008\n`, serial boot to `desktop: start`.
  - **Actual**: `VNC_BANNER: b'RFB 003.008\n'`, `$PREFIX/tmp/qemu-vnc.log` reached `desktop: start`. **PASS**
- **Command**: `make sdk-test`
  - **Expected**: Host SDK test passes on x86; fails on ARM64 Termux due to i386 `int $0x80` inline asm.
  - **Actual**: Failed with `error: invalid output constraint '=a' in asm` on ARM64 `cc`. **FAIL (host arch mismatch)**

## 2026-11-21 15:53 — Flicker & Layout Fix Verification (`58f2c88`)
- **Command**: `$PREFIX/tmp/qdrive2.sh fix "screendump @T@/fix_1.ppm@1;sendkey tab@2;screendump @T@/fix_2.ppm@1"`
  - **Expected**: Top bar no longer overlaps `Welcome` icon; Start menu has no legacy text bleed-through.
  - **Actual**: Confirmed visually in `2026-11-21_desktop-layout-fixed.png` and `2026-11-21_start-menu-layout-fixed.png`. **PASS**

## 2026-11-21 16:08 — Host Test Suite (`85b1474`)
- **Command**: `make build/airgraph_host_test build/desktop_render_host_test build/desktop_chrome_host_test build/bmp_host_test build/image_host_test && build/airgraph_host_test && build/desktop_render_host_test && build/desktop_chrome_host_test && build/bmp_host_test && build/image_host_test && python3 tests/boot_layout_test.py`
  - **Expected**: All 5 host C tests and `boot_layout_test.py` exit 0.
  - **Actual**: All 6 checks printed `all checks passed` / `checks passed`, exit 0. **PASS**
