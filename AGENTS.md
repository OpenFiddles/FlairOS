# Project notes

freecode reads this file before every change. Add anything it should always remember about this project.

## Product direction (decided by the owner)

- **The primary target is 64-bit, and AArch64 and x86_64 are the same product line.** The owner refers to this as "AArch64, 64bit" interchangeably.
- **The i386 build is deprecated.** It still builds and boots and is kept as the reference implementation of a complete session, but it is not the product. Do not spend effort growing the i386 image.
- **This reverses the earlier "i386 is the working target" reading.** Older notes in `docs/architecture/` described i386 as complete-and-bootable with AArch64 as bring-up only. That remains factually true of the code, but it is no longer the intended direction: work should move toward a 64-bit desktop rather than enlarging i386.

## Current facts worth remembering

- i386: retired to `legacy/` (BIOS loader at `legacy/i386/bootloader/`, HAL at `legacy/i386/arch/hal.c`). It still builds and boots, but only on request: `make legacy` or `make legacy-test`. The default `make` builds the 64-bit presets, and CI gates on those plus `make test`; the i386 CI job is manual and non-blocking. Its kernel is pinned at `0x30000` static memory; that ceiling is why `kernel/ui/desktop_theme.c` + `kernel/pkg/airjson.c` are not linked into it.
- AArch64: `cmake --preset arm64-phone` builds `build-arm64/flairos-arm64.bin`; booting it in QEMU `virt` prints the HAL banner and a DEX check, then halts. No display, heap, scheduler or shell yet.
- x86_64: the 64-bit desktop/shell/ART sources (`ui/shells/`, `ui/systemui/`, `runtime/art/`, `kernel/boot/kmain64.c`) compile and are host-tested but have **no bootable image**; `CMakeLists.txt` rejects `FLAIROS_ARCH=x86_64`. The shell code itself is architecture-neutral (it renders into an `ag_surface`).
- Tooling in this workspace disappears after a server restart and must be reinstalled: `nasm`, `qemu-system-x86`/`qemu-system-arm`, `cmake`, `ninja-build`, `gcc-aarch64-linux-gnu`.

## House rules

- Never relax the `0x30000` linker assertion in `kernel/boot/link.ld`. Reduce static footprint instead (see `agent/ChatGPT/decisions.md`).
- Do not claim a capability the build does not reach. Recognition, analysis and execution are separate claims.
- AirGraph draws pixels; WinDown arranges windows. Keep them separate.
- A `.airp` package is a container; an executable is one optional thing inside it. Spec: `docs/airp/FORMAT.md`.
