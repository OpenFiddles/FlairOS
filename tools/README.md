# tools/

Every script here is either called by the build, CI, the launcher or the docs,
or is a manual helper kept for a reason stated below. Nothing in this directory
is loaded by the kernel; these are host-side tools only.

## Build and disk images

| File | What it does | Used by |
| --- | --- | --- |
| `build_preset.py` | Builds one `presets/` folder into a Multiboot ELF and a raw, BIOS-bootable disk image. | `make presets`, `.github/workflows/ci.yml`, `flairos-start.sh` |
| `make_bootdisk.py` | Lays out the boot sector, the two loader stages and the kernel sectors of the disk image. | `build_preset.py`, `verify_disk_boot.py`, CI |
| `make_uefi_disk.py` | Builds the aarch64 disk: MBR + FAT16 ESP + UEFI loader. | `build_preset.py` |
| `fat16.py` | Minimal FAT16 writer used for that ESP. No CLI of its own. | imported by `make_uefi_disk.py` |
| `bootdisk/stage1.asm`, `bootdisk/stage2.asm` | 16-bit BIOS loader that reads the kernel off disk and enters protected mode. | assembled by `make_bootdisk.py` |
| `make_storage.py` | Creates the sparse `storage.img`, bounded by the host's free disk space. | `make storage-image`, `flairos-start.sh`, VNC launchers |
| `check_size.py` | Fails a build when an artifact exceeds a byte limit; optionally pads it to a fixed size. | `CMakeLists.txt` |
| `verify_disk_boot.py` | Boots a preset from its disk image under QEMU and asserts the captured framebuffer really rendered. | `make verify-disk`, `tests/disk_boot_gate_test.py`, CI |
| `verify_matrix.py` | Boots every preset on x86_64 and aarch64, checks the serial telemetry and saves screenshots. | manual, launched from `flairos-start.sh`; needs QEMU and built presets |
| `build-termux.sh` | Builds on Termux (aarch64) with the phone toolchain. | documented in `BUILD.md`, run in CI |
| `install-toolchain-rootless.py` | Installs the 64-bit build toolchain without root. | `flairos-start.sh` |

## Packaging and code generation

| File | What it does | Used by |
| --- | --- | --- |
| `airpack.py` | Wraps a linked app image in an `.airp` container. | `Makefile`, `CMakeLists.txt`, CI |
| `airpkg.py` | AIRP pack/validate CLI, including the desktop package. | `Makefile`, `tests/airp_pkg_test.py`, `docs/airp/FORMAT.md` |
| `pancake.py` | Packs, lists and extracts Pancake `.pck` resource bundles. | `ported/developer/build_ported.py`, `docs/PANCAKE.md` |
| `build_apps.py` | Builds every `apps/*` project into `build-apps/` and regenerates `include/vfs_apps_gen.h`. | `build_preset.py`, `SKILLS.md` |
| `mkapk.py` | Regenerates the sample APK payloads and `include/apk_samples.h`. Output is checked in and included by the kernel and tests, so this keeps it reproducible. | `kernel/fs/vfs.c`, `kernel/boot/kmain64.c`, `tests/runtime64_airp_test.c` |
| `gen_srgb_table.py` | Generates `include/srgb_linear.h`, the checked-in sRGB linearisation table. | `docs/architecture/GRAPHICS_ARCHITECTURE.md` |
| `generate-bmp-icons.py` | Generates the checked-in 64x64 Material icon pack under `resources/images/icons/`. | `resources/images/README.md` |
| `generate_anim_icons.py` | Generates the checked-in 24x24 animation frames and `include/anim_icons_gen.h`. | header included by `ui/shells/shell_desktop.c` and `shell_mobile.c` |
| `fetch_micropython.py` | Fetches the exact upstream MicroPython tag the port is pinned to. | `make micropython-fetch` |

## Test and inspection helpers

| File | What it does | Used by |
| --- | --- | --- |
| `apk_probe.c` | Host probe that parses a real `.apk` manifest and runs its DEX bytecode; prints every count and diagnostic. | `make apk-probe`, the CI `real-apks` job, `tests/apk_probe_gate_test.py` |
| `qemu_ui.py` | Drives FlairOS in headless QEMU over QMP: absolute mouse, keys, screenshots, serial checks. | `tests/acceptance.sh`, `docs/REQUIREMENTS.md` (IO-7) |
| `vnc_ctl.py` | Pure-stdlib RFB client for screenshots, clicks, drags, typing and scripted tours of a running VNC shell. | `SKILLS.md`, `flairos-start.sh` |
| `qemu_termux_bridge.py` | QEMU ↔ Termux bridge: telemetry v2, HTTP proxy and fixture replay. | `run-vnc-multi.sh` |
| `test_mouse_live.py` | Manual live check that pointer moves and clicks reach both the x86_64 and aarch64 presets; it is the only pointer check that covers aarch64, and no automated gate runs it. | manual; needs QEMU and built presets |
| `battery_bridge.py` | Emits the `BAT <0-100> [0|1]` / `NOBAT` lines on the guest's COM2 serial port that the kernel parses. | the protocol in `include/status.h`, parsed by `kernel/gfx/status.c`; run it beside QEMU on a phone |

## Launchers

| File | What it does | Used by |
| --- | --- | --- |
| `run-vnc.sh` | Starts/stops one preset as a QEMU VNC guest. | `run-vnc-multi.sh`, `run-x11.sh` |
| `run-vnc-multi.sh` | Starts, stops or reports the five VNC targets plus the Termux bridge. | `SKILLS.md`, `flairos-start.sh` |
| `run-x11.sh` | Runs FlairOS in a Termux:X11 window. | documented in `docs/REQUIREMENTS.md` |
| `run-x11-aarch64.sh` | Runs the AArch64 desktop guest in Termux:X11. | `flairos-start.sh`, `docs/DECISIONS.md` |

`__pycache__/` is ignored; nothing here needs installing beyond Python 3, a host
C compiler, and the emulator each tool names.
