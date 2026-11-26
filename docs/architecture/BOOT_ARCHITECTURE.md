# Boot architecture

The i386 BIOS path is the only boot path that reaches a desktop. This document
describes it exactly as implemented, and states where the AArch64 entry point
stands.

## Two-stage BIOS boot

A 1.44 MB floppy image (`build/textile.img`) is the boot medium. There is no
UEFI support, and none is claimed.

### Stage 1 — boot sector (`bootloader/stage1.asm`, 512 bytes, LBA 0)

Loaded by BIOS at `0x7C00` in real mode. Responsibilities:

1. Initialise segments and stack at `0x7C00`.
2. Read stage 2 (4 sectors, LBA 1) to `0x0000:0x9000`.
3. Read the kernel (256 sectors, LBA 5) to `0x1000:0x0000` (physical `0x10000`).
4. Read the boot app (128 sectors, LBA 261) to `0x4000:0x0000` (physical `0x40000`).
5. Stage the built-in packages from reserved sectors above the filesystem.
6. Pass the boot drive in `DL` and jump to stage 2.

Disk reads use BIOS `int 0x13` with CHS geometry computed from the LBA and
three retries per sector. CHS is used rather than LBA extensions because the
1.44 MB geometry is fixed and this keeps stage 1 inside 512 bytes.

### Staging layout

| Region | LBA | Destination | Sectors |
| --- | --- | --- | --- |
| Stage 2 | 1 | `0x9000` | 4 |
| Kernel | 5 | `0x10000` | 256 |
| Boot app | 261 | `0x40000` | 128 |
| Filesystem | 398 | — | 2085 |
| Explorer package | 2447 | `0x90000` | 32 |
| Terminal package | 2479 | `0x38000` | 64 |
| Settings package | 2543 | `0x30000` | 32 |
| Notes package | 2575 | `0x34000` | 16 |
| Desktop package | 2591 | `0x94000` | 16 |
| MicroPython (optional) | 2623 | `0x60000` | 257 |

The MicroPython slot is probed before loading: only its first sector is read,
the `AIRP` magic is checked, and the remaining sectors load only on a match. An
absent MicroPython therefore costs one sector read rather than a boot failure.

### Stage 2 — protected mode (`bootloader/stage2.asm`, `0x9000`)

1. Collect the BIOS E820 memory map into `0x5000` (count) and `0x5010` (entries).
2. Enable A20 via `int 0x15` with a keyboard-controller fallback.
3. Install the GDT and switch to 32-bit protected mode.
4. Hand control to the kernel entry at `0x10000` with the stack at `0x90000`.

Stage 2 also writes a boot-info descriptor including the selected boot mode.
**Known issue:** stage 2 writes the mode at `0x4FF0 + 8`, while `kernel/boot.c`
reads from `0x4FF8` with a magic check. These must agree; this is tracked in
[ROADMAP.md](ROADMAP.md).

## Boot modes

`kernel/boot.c` resolves the mode from the handoff block, defaulting to normal
boot when the block is absent or inconsistent:

| Mode | Behaviour |
| --- | --- |
| Normal | Full system, desktop session |
| Live | Runs from removable media without a persistent disk |
| Setup | Runs `kernel/setup.c` instead of the desktop |
| Recovery | Console-focused recovery session |
| Diagnostics | Hardware diagnostics |

Recovery exists because a bad display configuration or a failing driver must not
leave an unbootable machine. The developer console is reachable with
`Ctrl+Alt+D` from the desktop and is the primary recovery path at runtime.

## Handoff contract

The kernel receives three things and nothing else:

| Item | Location | Consumer |
| --- | --- | --- |
| E820 memory map | `0x5000`, entries `0x5010` | `drivers/pmm.c` |
| Boot drive | `DL` at entry | `kernel/boot.c` |
| Boot mode descriptor | `0x4FF0` region | `kernel/boot.c` |

Everything else the kernel needs it discovers itself. This keeps the
firmware interface small enough to trust.

## AArch64 entry point

`arch/aarch64/boot.S` sets up a stack, clears BSS and calls `kmain` in
`arch/aarch64/hello.c`. That `kmain` initialises the HAL, prints the
architecture name, verifies a five-instruction DEX program, and halts. It does
not set up a framebuffer, a timer interrupt, a heap, or a scheduler.

`arch/aarch64/ramfb.c` implements QEMU `fw_cfg` ramfb framebuffer setup, but it
is **not part of the build** (`CMakeLists.txt` does not reference it). Treat the
AArch64 port as a HAL bring-up, not a second supported boot path.

Verified behaviour, QEMU `virt` with `cortex-a57`:

```
FlairOS AArch64 HAL ready (aarch64)
FlairOS AArch64 DEX VM verified
```

Then it halts by design.

## UEFI, ARM boot, and x86_64: not implemented

No UEFI application exists. There is no ARM boot protocol implementation
(U-Boot, device tree, or `arm64-stub`). The x86_64 target is rejected by
`CMakeLists.txt` outright. Anything that needs these is a roadmap item, not a
latent capability.
