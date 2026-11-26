# Bootloader

The BIOS boot path for the i386 image. It builds with NASM into one 512-byte
boot sector and one 6-sector loader; `bootinfo.inc` is shared with the kernel.

```
bootloader/
├── bootinfo.inc      boot protocol constants and disk extents (shared)
├── stage1.asm        boot sector: console, load the loader, jump
├── stage2.asm        loader entry: menu, images, verification, handoff
└── loader/
    ├── serial.inc    COM1 console so a headless boot is observable
    ├── disk.inc      CHS sector reads (no BIOS extensions required)
    ├── edd.inc       extended disk access with a CHS fallback
    ├── memory.inc    A20 gate and the firmware E820 map
    ├── menu.inc      boot mode prompt with a timeout
    ├── images.inc    image table, chunked loading, kernel verification
    └── video.inc     VBE linear-framebuffer mode selection
```

Assemble with the include path so the modules resolve:

```
nasm -f bin -I bootloader/ bootloader/stage1.asm -o stage1.bin
nasm -f bin -I bootloader/ -D FORCE_BOOT_MODE=0 bootloader/stage2.asm -o stage2.bin
```

`FORCE_BOOT_MODE` picks the image: `0` normal (interactive menu), `1` live,
`2` setup. The Makefile and CMake both build all three loader variants.

## Boot flow

1. **Boot sector** — segments, stack, COM1, then a CHS read of the loader to
   `0x9000`. It never touches the kernel or the staged packages.
2. **Loader** — A20, E820, boot menu, table-driven image loading, kernel header
   verification, VBE mode. Then protected mode and a jump to `0x10000`.

## Boot protocol

`bootinfo.inc` and `include/boot.h` describe one block at `0x4000`, protocol
version 2. The 60-byte header is checksummed (sum of its fifteen words); the
E820 array follows at `0x4040`. The kernel validates the magic, version and
checksum before using anything, and falls back to the version 1 block at
`0x4FF0` when the checksum does not match.

The kernel image itself starts with a two-byte jump and an eight-byte header:
magic `FLRK` at `+2`, header version at `+6`, and the declared size in 512-byte
sectors at `+8`. The loader refuses an image whose header does not match what it
staged.

## Extents

The table in `loader/images.inc` is the only description of where every image
lives; `tests/boot_layout_test.py` parses it and checks the extents against the
image build, so a layout change has to be made in one place and the test follows.

CHS reads are used by the boot sector because it must fit in 512 bytes. The
loader prefers EDD (`int 0x13 AH=0x42`) and only falls back to CHS when the
firmware does not advertise extended disk access.
