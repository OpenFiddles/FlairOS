# FlairOS AArch64 native port

This is the native ARM64 bring-up target. It is independent from the i686/v86 image and does not reuse its 16-bit bootloader.

## 1. Install the build tools

On Ubuntu or Debian:

```sh
./scripts/setup-aarch64.sh
```

The setup script installs CMake, Ninja, the AArch64 GNU cross-compiler, binutils, QEMU's AArch64 system emulator, and multiarch GDB.

## 2. Configure and build

From the repository root:

```sh
cmake --preset aarch64
cmake --build --preset aarch64
```

The output is `build-cmake-aarch64/aarch64/kernel.elf`.

Or run the convenience script:

```sh
./scripts/build-aarch64.sh
```

## 3. Run in QEMU

```sh
cmake --build build-cmake-aarch64 --target aarch64_run
```

For GDB debugging, start QEMU paused with a TCP GDB stub:

```sh
cmake --build build-cmake-aarch64 --target aarch64_debug
```

In another terminal:

```sh
gdb-multiarch build-cmake-aarch64/aarch64/kernel.elf
```

Then enter `target remote :1234`, followed by `continue`.

## 4. Manual Make build

```sh
make -C arch/aarch64 CROSS=aarch64-linux-gnu-
```

## Current bring-up scope

The scaffold establishes the QEMU `virt` entry point, masks interrupts, clears BSS, installs a 64 KiB stack, enters C, initializes the PL011 UART, and prints a boot banner. It is not yet a complete operating system. Exception vectors, MMU/page tables, GIC interrupt handling, timers, memory management, scheduler, device drivers, and graphics remain future work.

## Display-orientation policy

The OS UI should be orientation-aware rather than tied to one framebuffer shape:

- Phones default to portrait and rotate automatically when rotation lock is off.
- Tablets support portrait and landscape, with layouts reflowing to the available dimensions.
- Desktop/laptop sessions default to landscape.
- Apps may declare portrait, landscape, or automatic preference; the system's rotation lock takes precedence.
- AirGraph renders using the current display dimensions. WinDown recomputes window placement and layout after a display-orientation change. Do not rotate a stale completed desktop frame as a substitute for relayout.

Orientation is a display/UI policy, not an AArch64-specific feature.
