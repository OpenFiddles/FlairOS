; FlairOS boot protocol constants.
;
; One versioned block at BOOT_INFO_ADDR carries everything the loader learned
; about the machine to the kernel: boot mode, boot drive, the firmware memory
; map, the kernel extent it verified, and the video mode it programmed.
; include/boot.h mirrors this layout field for field; keep them in sync.
;
; Layout (all fields little-endian):
;   +0  magic "BOOT"        +24 kernel LBA
;   +4  protocol version    +28 kernel sectors
;   +8  boot mode           +32 kernel bytes
;   +12 boot drive          +36 LFB physical address (0 = none)
;   +16 E820 entry count    +40 width, +42 height, +44 bpp, +46 pitch
;   +20 E820 array address  +48 VBE version, +52 video memory in KiB
;   +56 flags               +60 checksum of dwords 0..14

%ifndef FLAIROS_BOOTINFO_INC
%define FLAIROS_BOOTINFO_INC

BOOT_INFO_ADDR    equ 0x4000
BOOT_INFO_MAGIC   equ 0x544F4F42        ; "BOOT"
BOOT_INFO_VERSION equ 2

BI_MAGIC        equ 0
BI_VERSION      equ 4
BI_MODE         equ 8
BI_DRIVE        equ 12
BI_E820_COUNT   equ 16
BI_E820_ADDR    equ 20
BI_KERNEL_LBA   equ 24
BI_KERNEL_SEC   equ 28
BI_KERNEL_LEN   equ 32
BI_LFB          equ 36
BI_WIDTH        equ 40
BI_HEIGHT       equ 42
BI_BPP          equ 44
BI_PITCH        equ 46
BI_VBE_VER      equ 48
BI_VRAM_KB      equ 52
BI_FLAGS        equ 56
BI_CHECKSUM     equ 60
BI_SIZE         equ 64

BI_E820_MAX     equ 32

BOOT_FLAG_SERIAL equ 1
BOOT_FLAG_EDD    equ 2
BOOT_FLAG_VIDEO  equ 4

E820_ENTRIES    equ BOOT_INFO_ADDR + BI_SIZE

KERNEL_LBA      equ 7
KERNEL_SECTORS  equ 256
KERNEL_BASE     equ 0x10000
KERNEL_MAGIC    equ 0x4B524C46          ; "FLRK" at KERNEL_BASE + 2
KERNEL_HDR_VER  equ 1

AIRP_MAGIC      equ 0x50524941          ; "AIRP"

%endif
