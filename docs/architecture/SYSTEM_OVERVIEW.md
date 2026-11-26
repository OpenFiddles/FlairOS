# System overview

FlairOS is a freestanding operating system with its own boot path, kernel,
graphics stack, desktop and application container. It is not a Linux
distribution and does not depend on one at runtime.

## What is actually runnable today

This page exists partly to state that plainly, because the repository contains
code for more targets than it can boot.

| Target | Status | What it does |
| --- | --- | --- |
| i386 (BIOS) | **Complete and bootable** | Full system: bootloader, kernel, framebuffer desktop, applications |
| AArch64 | **Bring-up scaffold** | Serial output and a DEX bytecode check, then halts. No display, no shell |
| x86_64 | **Library/host-test only** | 64-bit desktop, shell and ART runtime sources compile and are host-tested, but no bootable image exists |

The i386 path is the only one that reaches a desktop. AArch64 and x86_64 code is
real and tested, but neither has a working boot target yet. See
[ROADMAP.md](ROADMAP.md) for what closing that gap requires.

## Target direction

The project direction is **64-bit**: AArch64 and x86_64 are treated as one
product line, and 64-bit is where new work goes. The i386 build is a
**reference implementation**, kept because it is the only complete session we
have and because it is a known-good baseline to compare against. It is not the
product, and it is not being extended.

So read the table above as *what the code does today*, not as *where the project
is going*. The gap between those two things is the whole of the roadmap.

## Layers

| Layer | Responsibility | Location |
| --- | --- | --- |
| Firmware interface | BIOS calls, disk reads, memory map, video mode | `bootloader/` |
| Boot management | Boot modes, staging, handoff, recovery | `bootloader/`, `kernel/boot/boot.c` |
| Kernel | Interrupts, syscalls, scheduler, panics | `kernel/` |
| Memory | Physical frames, paging, heap | `drivers/pmm.c`, `drivers/vm.c`, `drivers/kheap.c` |
| Hardware abstraction | Port I/O and per-architecture primitives | `arch/*/hal.c`, `include/hal.h` |
| Drivers | ATA, PS/2, PIT, RTC, serial, speaker, RTL8139 | `drivers/` |
| Graphics | AirGraph pixel rendering, framebuffer | `graphics/`, `kernel/gfx/airgraph.c` |
| Compositing | WinDown window model, SprunkUI widgets | `kernel/ui/windown.c`, `kernel/gfx/sprunkui.c` |
| Desktop | Session shell, dock, panels, taskbar | `kernel/desktop*.c` |
| Filesystem | VFS, FAT32, RAM mode | `kernel/fs/vfs.c`, `kernel/fs/fat32.c` |
| Applications | AIRP packages, ELF loader, syscall ABI | `kernel/pkg/airp.c`, `kernel/pkg/elf.c` |
| Runtime research | APK/DEX analysis, ART-style sandbox | `kernel/pkg/apk.c`, `kernel/pkg/dex.c`, `runtime/art/` |

## Architectural rules

These are constraints, not preferences. Breaking them produces the kind of bug
that is hard to find later.

1. **AirGraph draws pixels; WinDown arranges windows.** AirGraph knows nothing
   about windows. WinDown knows nothing about pixel formats. Anything that mixes
   the two belongs in neither.
2. **The kernel must not panic on user-facing operations.** A compositor call
   with no display returns an error and logs; it does not halt the machine.
3. **A package is a container; the executable is one thing inside it.** A
   container may hold zero executables. See [../airp/FORMAT.md](../airp/FORMAT.md).
4. **Validate before you trust, and trust before you execute.** Package
   checksums are verified before any payload is copied into the app slot.
5. **Static memory has a ceiling on i386.** `kernel/boot/link.ld` asserts the image
   ends before `0x30000`. New static data must fit or the design must change.
6. **Do not claim a capability the build does not reach.** Unsupported means
   reported as unsupported, not silently degraded.

## Reading order

New to the codebase: this page, then [BOOT_ARCHITECTURE.md](BOOT_ARCHITECTURE.md),
then [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) (the address map explains
most constraints), then [DESKTOP_ARCHITECTURE.md](DESKTOP_ARCHITECTURE.md).
