# Boot architecture

The i386 BIOS path is the only boot path that reaches a desktop. This document
describes it exactly as implemented, and states where the AArch64 entry point
stands.

## Two-stage BIOS boot

A 1.44 MB floppy image (`build/textile.img`) is the boot medium. There is no
UEFI support, and none is claimed.

### Stage 1 — boot sector (`bootloader/stage1.asm`, 512 bytes, LBA 0)

Loaded by BIOS at `0x7C00` in real mode. It does one job:

1. Initialise segments, stack and COM1 (so a boot is observable headless).
2. Read the loader (6 sectors, LBA 1) to `0x0000:0x9000`.
3. Pass the boot drive in `DL` and jump to the loader.

Sector reads go through `bootloader/loader/disk.inc`, which translates an LBA
to CHS geometry. The fixed 1.44 MB geometry keeps this inside the 512-byte
sector; the loader uses the BIOS extended disk interface instead.

### Stage 2 — loader (`bootloader/stage2.asm` + `bootloader/loader/*.inc`)

The loader is loaded at `0x9000` and does the work a boot sector has no room
for:

1. Initialise the serial console and probe extended disk access (`edd.inc`).
2. Enable A20 (BIOS, fast A20, then 8042) and verify it (`memory.inc`).
3. Collect the firmware E820 map into the boot-info block (`memory.inc`).
4. Show the boot menu with a 1.5 s timeout; the live and setup images define
   `FORCE_BOOT_MODE` and skip it (`menu.inc`).
5. Load every image from the table in `images.inc`, in 64-sector chunks, with
   EDD reads and a CHS fallback. Optional slots are probed first: only the
   first sector is read, the `AIRP` magic is checked, and the rest load only on
   a match, so an absent MicroPython costs one sector read.
6. Verify the kernel image before trusting it: the entry stub declares a magic
   (`FLRK`), a header version and its own size in sectors. A mismatched image is
   reported and boot stops instead of executing garbage (`images.inc`).
7. Ask the VBE firmware for a 32-bit linear-framebuffer mode (1024×768, then
   800×600) and record it (`video.inc`). The kernel still programs its own
   display, but falls back to this framebuffer if DISPI probing fails.
8. Publish the checksummed boot-info block, install the GDT, enter protected
   mode and jump to `0x10000` with the stack at `0x90000`.

### Staging layout

| Region | LBA | Destination | Sectors |
| --- | --- | --- | --- |
| Loader | 1 | `0x9000` | 6 |
| Kernel | 7 | `0x10000` | 256 |
| Boot app | 263 | `0x40000` | 128 |
| Filesystem | 398 | — | 2085 |
| Explorer package | 2447 | `0x90000` | 32 |
| Terminal package | 2479 | `0x38000` | 64 |
| Settings package | 2543 | `0x30000` | 32 |
| Notes package | 2575 | `0x34000` | 16 |
| Desktop package | 2591 | `0x94000` | 16 |
| MicroPython (optional) | 2623 | `0x60000` | 257 |

The table in `bootloader/loader/images.inc` is the single description of this
layout; `tests/boot_layout_test.py` parses it and checks the extents against the
image build.

### Boot protocol

`bootloader/bootinfo.inc` and `include/boot.h` define one versioned block at
`0x4000` (protocol version 2). Its 60 header bytes are checksummed and the E820
map follows at `0x4040`:

| Field | Offset | Consumer |
| --- | --- | --- |
| magic `BOOT`, version | 0, 4 | `boot_init` validation |
| boot mode, boot drive | 8, 12 | `boot_mode`, `boot_drive` |
| E820 count, address | 16, 20 | `boot_e820_count`, `boot_e820_entries` |
| kernel LBA, sectors, bytes | 24–32 | `boot_kernel_sectors` |
| LFB address, width, height, bpp, pitch | 36–46 | `boot_video` |
| VBE version, video memory | 48, 52 | `boot_video` |
| flags, checksum | 56, 60 | `boot_flags` |

A block that fails the checksum is ignored, and `kernel/boot/boot.c` falls back
to the version 1 handoff so an older boot floppy still starts.

## Boot modes

`kernel/boot/boot.c` resolves the mode from the handoff block, defaulting to normal
boot when the block is absent or inconsistent:

| Mode | Behaviour |
| --- | --- |
| Normal | Full system, desktop session |
| Live | Runs from removable media without a persistent disk |
| Setup | Runs `kernel/boot/setup.c` instead of the desktop |
| Recovery | Console-focused recovery session |
| Diagnostics | Hardware diagnostics |

Recovery exists because a bad display configuration or a failing driver must not
leave an unbootable machine. The developer console is reachable with
`Ctrl+Alt+D` from the desktop and is the primary recovery path at runtime.

## Handoff contract

The kernel receives three things and nothing else:

| Item | Location | Consumer |
| --- | --- | --- |
| Boot mode, drive | boot info `+8`, `+12` | `kernel/boot/boot.c` |
| E820 memory map | boot info `+16`, entries at `+20` | `drivers/pmm.c`, `kernel/boot/hwinfo.c` |
| Kernel extent | boot info `+24` | `boot_kernel_sectors` |
| Framebuffer | boot info `+36` | `kernel/ui/desktop_gfx.c` fallback |

Everything else the kernel needs it discovers itself. This keeps the firmware
interface small enough to trust.

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
