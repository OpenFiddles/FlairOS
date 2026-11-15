# FlairOS build target setup

Run the interactive setup wizard from the repository root:

```sh
python3 scripts/configure-build.py
```

The wizard asks for three independent settings:

1. **CPU architecture** — i686/v86 or native AArch64/ARM64.
2. **Device profile** — mobile phone, tablet, desktop/laptop, smartwatch, television, or embedded/kiosk.
3. **Display orientation** — automatic, portrait, landscape, or square.

It then configures a separate Ninja build directory for that architecture/device pair and compiles it. To configure without compiling, use `--no-build`.

## Important distinctions

- CPU architecture determines the instruction set and toolchain. Device profile determines the intended UI and interaction model. They are not interchangeable.
- The current i686 target remains the existing v86-oriented kernel. The AArch64 target is the native ARM64 bring-up kernel.
- Device profiles are currently build-time metadata exposed as `FLAIROS_DEVICE_*` and `FLAIROS_ORIENTATION_*` compile definitions. They are not claims that every hardware platform already has boot support, drivers, or a complete UI.
- **Smartwatch** means a watch-sized FlairOS device profile. It does not produce an Apple watchOS application or binary.
- AirGraph should render against the active display dimensions. WinDown should recompute layout when orientation or display geometry changes; it should not simply rotate a stale framebuffer.

## Manual CMake configuration

Example: native ARM64 phone build, automatic rotation:

```sh
cmake -S . -B build-aarch64-mobile -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux.cmake \
  -DFLAIROS_ARCH=aarch64 \
  -DFLAIROS_DEVICE=mobile \
  -DFLAIROS_ORIENTATION=auto
cmake --build build-aarch64-mobile
```

Available device profiles: `mobile`, `tablet`, `desktop`, `smartwatch`, `tv`, `embedded`.

Available orientations: `auto`, `portrait`, `landscape`, `square`.
