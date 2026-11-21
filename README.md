# FlairOS V2

FlairOS V2 is moving to an upstream Linux foundation with a minimal userspace and the native AirP (AirPacket) package manager.

## Current target

- i386 (32-bit x86), suitable for the initial v86 target
- Linux kernel fetched directly from the official kernel.org Git repository
- GNU GRUB built from its official source repository
- Static BusyBox userspace for essential system commands
- AirP for installing and removing FlairOS applications and system packages
- One bootable MBR disk image, with a separate debug and release build

This is a minimal FlairOS system, not an Alpine or other preassembled Linux distribution. KDE Plasma and other large desktop environments are intentionally outside this foundation milestone.

## Build

GitHub Actions is the authoritative build and artifact pipeline. For a local Linux build, install the dependencies listed in `.github/workflows/ci.yml`, then run:

```sh
bash build/build-linux-image.sh debug
```

The image is written to `build-out/flairos-i386-debug.img`. Use `release` for the release profile.

## AirP

AirP manages two package types:

- `.airp`: FlairOS application containers, retained for the AIRP application runtime.
- `.airpkg`: system packages containing a `manifest` and a `rootfs/` tree.

Commands: `airp install`, `airp remove`, `airp list`, and `airp info`.

See [docs/AIRP.md](docs/AIRP.md) and [BUILD.md](BUILD.md).
