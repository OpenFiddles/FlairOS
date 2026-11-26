# Roadmap

Ordered by what unlocks the most, with the honest current state next to each
item. Nothing here is a promise about dates.

## Immediate

### 0. i386 is retired to `legacy/`

Done: the BIOS loader and `arch/i386` live under `legacy/i386/`, `make` builds
the 64-bit presets instead, i386 is reachable only through `make legacy` and
`make legacy-test`, and CI gates on the 64-bit build plus the host tests with
the i386 job manual and non-blocking. The i386 image is still produced and still
boots; it is deleted only once the 64-bit image boots from disk.

### 1. Make the 64-bit target bootable

The direction is decided: **64-bit is the product, and AArch64 and x86_64 are the
same product line.** i386 is a reference implementation, not a destination.

The repository does not yet have a bootable 64-bit desktop, which is the gap
that matters:

| Option | Work required | What you get |
| --- | --- | --- |
| AArch64 first | MMU, valid frame allocator, heap, interrupts, ramfb display, input, then link the existing shell | One 64-bit target on the architecture the owner named |
| x86_64 first | Multiboot2 entry, 64-bit paging, loader, then link the existing shell | Reuses the most already-written 64-bit code |

Either way the 64-bit desktop shell (`ui/shells/`) is architecture-neutral: it
renders into an `ag_surface` and never touches x86 specifics, so it is the shared
prize. The work is almost entirely underneath it — a boot path, memory
management and a display device.

### 2. Do not spend effort on the i386 ceiling

The theme loader is complete and tested but cannot link into i386: about 6.9 KB
of new code against roughly 5.9 KB of headroom below `0x30000`. Since i386 is no
longer the product, the correct resolution is to **apply the package theme on the
64-bit target** rather than to fight for 1 KB of 32-bit static memory.

Keep the assertion. Keep i386 building. Do not grow it.

### 3. Fix the boot-mode handoff mismatch

`stage2.asm` writes the boot mode at `0x4FF0 + 8`; `kernel/boot/boot.c` reads `0x4FF8`.
They must agree. Today the kernel falls back to normal boot, so the bug is
invisible until a non-normal mode is actually needed.

## Near term

### Bootloader as a subsystem

The boot path works but is not yet a subsystem: it is two assembly stages with
hardcoded sector numbers.

- [ ] Boot menu with mode selection, reachable before the kernel
- [ ] Recovery entry that does not depend on a healthy display
- [ ] Boot protocol documented as a stable interface, not just as code
- [ ] Failure diagnostics: report *why* a load failed, not just that it did
- [ ] Package staging driven by a manifest rather than hardcoded LBAs

The last one is the real unlock: adding a staged package currently means editing
`stage1.asm`, the Makefile and the sector map.

### Desktop as a package, fully

- [x] Container format with manifest, resources and integrity
- [x] `FlairDesktop.airp` built and staged at boot
- [x] Theme resource parsed and validated with a host-tested loader
- [ ] Theme actually applied on a bootable target
- [ ] Launcher layout read from `launcher.json` instead of compiled-in tables
- [ ] Strings read from `strings.json`
- [ ] Icons as package resources rather than `.rodata` arrays

### Accessibility and theming

- [x] WCAG contrast measurement with a generated sRGB table
- [x] Out-of-range theme values rejected rather than clamped
- [ ] Light theme as a first-class shipped variant
- [ ] Contrast check surfaced in the UI, not only the serial log

## Medium term

### Application model

- [ ] Multiple concurrent application images instead of one shared slot
- [ ] Relocatable images so the slot is not a fixed address
- [ ] Per-application resource lookup through the package API
- [ ] Permission enforcement for the permissions the manifest already declares

### Android compatibility: stay honest

The staged plan stays staged until each stage is real:

- [x] Stage 1: file recognition
- [x] Stage 2: metadata extraction
- [x] Stage 3: DEX structure analysis
- [ ] Stage 4: broader DEX coverage, verified against real `classes.dex` files
- [ ] Stage 5: executing an Android application end to end

Stages 4 and 5 are not started in any meaningful sense. The current interpreter
executes a verified subset on a five-instruction program.

### 64-bit runtime

- [ ] Fix the `flair_py_run_script` failure in `runtime64_airp_test`
- [ ] Add a bootable 64-bit target
- [ ] Bring up the 64-bit desktop shell on real hardware or emulator

## Long term

- UEFI boot as an additional firmware interface
- SMP
- Demand paging and swap
- A real capability model
- Multi-monitor and per-window scaling

## Deliberately not planned

- Linux distribution conversion. FlairOS is its own system.
- Claimed compatibility with software that has not been executed. Recognition,
  analysis and execution are separate claims and stay separate.
