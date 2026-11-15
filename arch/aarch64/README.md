# FlairOS AArch64 native port

This is the native ARM64 bring-up target. It is independent from the i686/v86 image and does not reuse its 16-bit bootloader.

## Interactive build setup

From the repository root:

```sh
python3 scripts/configure-build.py
```

The wizard asks for CPU architecture, device profile (mobile, tablet, desktop, smartwatch, TV, or embedded), and preferred display orientation. It configures a separate build directory and compiles. Use `python3 scripts/configure-build.py --no-build` to configure only.

Install the AArch64 build tools on Debian/Ubuntu with:

```sh
./scripts/setup-aarch64.sh
```

## Manual configure and build

```sh
cmake --preset aarch64
cmake --build --preset aarch64
```

Output: `build-cmake-aarch64/aarch64/kernel.elf`.

Or use:

```sh
./scripts/build-aarch64.sh
```

## Run in QEMU

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

## Current bring-up scope

The scaffold establishes the QEMU `virt` entry point, masks interrupts, clears BSS, installs a 64 KiB stack, enters C, initializes the PL011 UART, and prints a boot banner. It is not yet a complete operating system. Exception vectors, MMU/page tables, GIC interrupt handling, timers, memory management, scheduler, device drivers, and graphics remain future work.

Device profiles currently provide build metadata, not finished hardware support. See [BUILD_TARGETS.md](../../docs/BUILD_TARGETS.md).
