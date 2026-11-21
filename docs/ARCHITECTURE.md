# FlairOS V2 Architecture

## Foundation

FlairOS V2 uses the upstream Linux kernel as its hardware and process foundation. The initial target is 32-bit x86 (i386) for the v86 emulator.

The build pipeline fetches three upstream projects directly:

1. Linux from kernel.org
2. GNU GRUB from Savannah
3. BusyBox from its official project repository

All three checkouts are shallow clones. GitHub Actions builds them and assembles a single MBR disk image.

## Userspace

The first userspace is intentionally small: static BusyBox, essential mount/runtime directories, an init script, and AirP. This is not a preassembled distribution and does not include a large desktop environment.

## AirP

AirP (AirPacket) is the native package manager. It installs `.airp` application containers and `.airpkg` system packages. The initial implementation provides local install, list, inspect, and remove operations. Network repositories, signatures, dependency solving, and the application execution runtime remain future milestones.

## Graphics and desktop

AirGraph and WinDown remain FlairOS design/subsystem goals. Their former custom-kernel implementations are not linked into the Linux foundation. They will need Linux userspace interfaces and integration work before they can be enabled in this architecture.

## Image layout

The generated raw disk image contains:

- MBR + GRUB boot code
- FAT32 boot partition with the Linux kernel and GRUB configuration
- ext2 root partition with BusyBox, init, and AirP

Debug and release images are built independently.
