# FlairOS Agent Skills & Toolchain Guide

This repository provides a complete build, packaging, testing, and live VNC visual-interaction toolchain for **FlairOS** (`i386`, `x86_64`, `aarch64`). `tools/README.md` lists every script under `tools/` with what it does and what calls it.

## 1. Live Multi-Arch VNC Servers (`tools/run-vnc-multi.sh`)
Start, stop, or check all 5 QEMU VNC targets (plus the `127.0.0.1:5910` Termux host bridge):
```bash
./tools/run-vnc-multi.sh start   # Builds & starts all 5 VNCs + Termux bridge
./tools/run-vnc-multi.sh status  # Shows PIDs and running/stopped status
./tools/run-vnc-multi.sh stop    # Stops all VNC servers and bridge
```
| Port | Target | Resolution | Notes |
| --- | --- | --- | --- |
| `:5900` | `i386` Desktop | `800x600` | Landscape AirGraph desktop + `build/storage.img` |
| `:5901` | `x86_64` Desktop | `1024x768` | macOS desktop + VMMouse + COM2 Termux bridge (`5910`) |
| `:5902` | `aarch64` Desktop | `1024x768` | macOS desktop + `virtio-gpu` + `virtio-blk` (`storage-a64.img`, 2G RAM) |
| `:5903` | `aarch64` Mobile | `480x800` | Mobile shell (`ui/shells/shell_mobile.c`) |
| `:5904` | `aarch64` TV | `1280x720` | TV shell (`ui/shells/shell_tv.c`) |

Serial boot & input logs live in `$PREFIX/tmp/qemu-vnc-{i386,x86_64,aarch64,mobile-a64,tv-a64}.log`.

## 2. Seeing & Interacting with Live VNC Shells (`tools/vnc_ctl.py`)
Use `tools/vnc_ctl.py` (pure Python stdlib RFB client) to inspect real pixels, move the cursor, drag windows, type keys, and run multi-step UI tours across any running shell:
```bash
# Capture a screenshot of any live shell
python3 tools/vnc_ctl.py screenshot 5902 $PREFIX/tmp/desktop.png
python3 tools/vnc_ctl.py screenshot 5903 $PREFIX/tmp/mobile.png
python3 tools/vnc_ctl.py screenshot 5904 $PREFIX/tmp/tv.png

# Move cursor, click, drag windows, or type text
python3 tools/vnc_ctl.py move 5902 235 715
python3 tools/vnc_ctl.py click 5902 235 715 --out $PREFIX/tmp/after_click.png
python3 tools/vnc_ctl.py drag 5902 150 65 300 120 --out $PREFIX/tmp/after_drag.png
python3 tools/vnc_ctl.py type 5902 "7*6=" --out $PREFIX/tmp/after_type.png

# Run automated 5-step interactive desktop tour (hover Dock, Launcher, Calc, Control Center)
python3 tools/vnc_ctl.py demo 5902 $PREFIX/tmp/vnc_demo
```

## 3. Building Apps into `.airp` (`airp.py` & `tools/build_apps.py`)
```bash
./airp.py doctor                        # Verify clang, ld.lld, llvm-objcopy, git, sdk/
./airp.py init apps/my_app myapp        # Scaffold a new C app
./airp.py build apps/my_app             # Compile & pack i386 .airp
./airp.py build apps/my_app --arch aarch64
./airp.py info apps/my_app/myapp.airp   # Inspect AIRP header
python3 tools/build_apps.py             # Wildcard-build all apps/*/ into build-apps/ & vfs_apps_gen.h
```

## 4. Host Test Suite & Preset Builds
```bash
cmake --build build-cmake --target host-tests && ctest --test-dir build-cmake --output-on-failure
python3 tools/build_preset.py --all
```
